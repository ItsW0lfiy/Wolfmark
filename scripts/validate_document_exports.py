"""Independent validation of the native visual fixture's generated exports.

Run after --smoke-export fixtures/wolfmark-visual-test.md. ZIP/XML/HTML checks
use the standard library; PDF text/link/font checks additionally use pypdf.
"""
import json
import argparse
from pathlib import Path
import posixpath
import unicodedata
from xml.etree import ElementTree as ET
from zipfile import ZIP_STORED, ZipFile


def validate(root: Path, schemas: Path | None = None) -> None:
    report = json.loads((root / "layout.json").read_text(encoding="utf-8"))
    for name in ("document.docx", "document.odt"):
        with ZipFile(root / name) as package:
            assert package.testzip() is None, name
            assert len(package.namelist()) == len(set(package.namelist())), name
            for part in package.namelist():
                if part.endswith((".xml", ".rels")) or part == "[Content_Types].xml":
                    ET.fromstring(package.read(part))
                if part.endswith(".png"):
                    assert package.read(part).startswith(b"\x89PNG\r\n\x1a\n"), part
    w = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"
    with ZipFile(root / "document.docx") as package:
        document = ET.fromstring(package.read("word/document.xml"))
        assert document.findall(f".//{w}tbl"), "Missing editable Word table"
        assert document.findall(f".//{w}numPr"), "Missing native Word lists"
        styles = {p.get(w + "val") for p in document.findall(f".//{w}pStyle")}
        assert {f"Heading{n}" for n in range(1, 7)} <= styles
        size = document.find(f".//{w}sectPr/{w}pgSz")
        assert abs(int(size.get(w + "w")) / 20 - report["widthPoints"]) < 1
        assert abs(int(size.get(w + "h")) / 20 - report["heightPoints"]) < 1
        assert b"PAGE" in package.read("word/footer1.xml")
        assert b"NUMPAGES" in package.read("word/footer1.xml")
        relations = ET.fromstring(package.read("word/_rels/document.xml.rels"))
        for relation in relations:
            if relation.get("TargetMode") == "External":
                continue
            target = posixpath.normpath(posixpath.join("word", relation.get("Target")))
            assert target in package.namelist(), target
        assert any(n.startswith("word/media/") for n in package.namelist())
        numbering = ET.fromstring(package.read("word/numbering.xml"))
        for level in numbering.findall(f".//{w}abstractNum[@{w}abstractNumId='1']/{w}lvl"):
            assert level.find(w + "lvlText").get(w + "val") == f"%{int(level.get(w + 'ilvl')) + 1}."
        assert all(int(size.get(w + "val")) > 0 for size in document.findall(f".//{w}sz"))
    with ZipFile(root / "document.odt") as package:
        assert package.infolist()[0].filename == "mimetype"
        assert package.infolist()[0].compress_type == ZIP_STORED
        assert package.read("mimetype") == b"application/vnd.oasis.opendocument.text"
        ns = {"fo": "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0",
              "style": "urn:oasis:names:tc:opendocument:xmlns:style:1.0"}
        styles = ET.fromstring(package.read("styles.xml"))
        page = styles.find(".//style:page-layout-properties", ns)
        assert page.get("{" + ns["fo"] + "}page-width") == "210.000mm"
        assert page.get("{" + ns["fo"] + "}page-height") == "297.000mm"
        assert any(n.startswith("Pictures/") for n in package.namelist())
    html = (root / "document.html").read_text(encoding="utf-8")
    assert "data:image/png;base64," in html
    assert "wolfmark-image:" not in html and "wolfmark-copy:" not in html
    assert "<script" not in html.lower()
    assert "Wolfmark visual rendering fixture" in (root / "document.txt").read_text(encoding="utf-8")
    assert (root / "document.pdf").read_bytes().startswith(b"%PDF-")
    if schemas is not None:
        from lxml import etree
        word_schema = etree.XMLSchema(etree.parse(str(schemas / "ooxml-schemas/wml.xsd")))
        odf_schema = etree.RelaxNG(etree.parse(str(schemas / "OpenDocument-v1.3-schema.rng")))
        manifest_schema = etree.RelaxNG(etree.parse(str(schemas / "OpenDocument-v1.3-manifest-schema.rng")))
        with ZipFile(root / "document.docx") as package:
            for name in package.namelist():
                if name.startswith("word/") and name.endswith(".xml"):
                    word_schema.assertValid(etree.fromstring(package.read(name)))
        with ZipFile(root / "document.odt") as package:
            for name in ("content.xml", "styles.xml", "meta.xml", "META-INF/manifest.xml"):
                validator = manifest_schema if name.startswith("META-INF") else odf_schema
                validator.assertValid(etree.fromstring(package.read(name)))
        print("All WordprocessingML and OpenDocument XML parts passed the official schemas.")
    try:
        from pypdf import PdfReader
    except ImportError:
        print("ZIP/XML/HTML/text validation passed; PDF text/links/fonts NOT CHECKED (install pypdf or use the bundled runtime).")
        return
    pdf = PdfReader(root / "document.pdf")
    assert len(pdf.pages) == report["pages"]
    assert all(abs(float(p.mediabox.width) - report["widthPoints"]) < 1 for p in pdf.pages)
    def searchable_text(pages):
        return " ".join(unicodedata.normalize("NFKC", "\n".join(p.extract_text() for p in pages)).split())

    text = searchable_text(pdf.pages)
    assert "Wolfmark visual rendering fixture" in text and "月" in text
    assert "source unchanged" not in text  # UI diagnostics must not enter output.
    assert any(p.get("/Annots") for p in pdf.pages), "Missing PDF links"
    embedded = False
    for page in pdf.pages:
        fonts = page["/Resources"].get_object().get("/Font", {})
        for font in fonts.get_object().values():
            font = font.get_object()
            for descendant in font.get("/DescendantFonts", [font]):
                descriptor = descendant.get_object().get("/FontDescriptor")
                if descriptor and any(key in descriptor.get_object() for key in ("/FontFile", "/FontFile2", "/FontFile3")):
                    embedded = True
    assert embedded, "No embedded PDF font found"
    native_print = PdfReader(root / "native-print.pdf")
    assert len(native_print.pages) == len(pdf.pages)
    assert "Wolfmark visual rendering fixture" in searchable_text(native_print.pages)
    custom = PdfReader(root / "custom-landscape.pdf")
    assert all(abs(float(p.mediabox.width) - 210 * 72 / 25.4) < 1 and
               abs(float(p.mediabox.height) - 150 * 72 / 25.4) < 1 for p in custom.pages)
    print(f"Validated DOCX/ODT package structure, HTML resources, and PDF text/links/fonts across {len(pdf.pages)} pages, including the 600 DPI Qt printer path.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path, default=Path("out/tests/exports"))
    parser.add_argument("--schemas", type=Path)
    args = parser.parse_args()
    validate(args.directory, args.schemas)
