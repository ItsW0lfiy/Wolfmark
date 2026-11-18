"""Fetch official, versioned document schemas into ignored QA storage.

Development/CI tool only. Wolfmark and its exporters never download schemas.
"""
from io import BytesIO
from pathlib import Path
from urllib.request import urlopen
from zipfile import ZipFile
import argparse


def download(url: str, target: Path) -> bytes:
    if target.exists():
        return target.read_bytes()
    with urlopen(url, timeout=60) as response:
        data = response.read(32 * 1024 * 1024 + 1)
    if len(data) > 32 * 1024 * 1024:
        raise ValueError("Schema download exceeds the 32 MiB QA limit")
    target.write_bytes(data)
    return data


def fetch(root: Path) -> None:
    root.mkdir(parents=True, exist_ok=True)
    for name in ("OpenDocument-v1.3-schema.rng", "OpenDocument-v1.3-manifest-schema.rng"):
        download("https://docs.oasis-open.org/office/OpenDocument/v1.3/os/schemas/" + name, root / name)
    archive = download("https://ecma-international.org/wp-content/uploads/ECMA-376-4_5th_edition_december_2016.zip", root / "ECMA-376-4.zip")
    base = (root / "ooxml-schemas").resolve()
    base.mkdir(exist_ok=True)
    with ZipFile(BytesIO(archive)) as outer:
        with ZipFile(BytesIO(outer.read("OfficeOpenXML-XMLSchema-Transitional.zip"))) as schemas:
            for entry in schemas.infolist():
                if entry.is_dir():
                    continue
                target = (base / entry.filename).resolve()
                if not target.is_relative_to(base) or entry.file_size > 10 * 1024 * 1024:
                    raise ValueError("Unsafe schema archive member")
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(schemas.read(entry))
    download("https://www.w3.org/2001/xml.xsd", base / "xml.xsd")
    # ECMA imports the W3C XML namespace without a location. Supply the local
    # authoritative schema for deterministic, network-free validation.
    for path in base.glob("*.xsd"):
        # Preserve the original schema bytes/comments; change only the missing
        # import location rather than serializing the specification afresh.
        data = path.read_text(encoding="utf-8")
        data = data.replace('namespace="http://www.w3.org/XML/1998/namespace"/>',
                            'namespace="http://www.w3.org/XML/1998/namespace" schemaLocation="xml.xsd"/>')
        path.write_text(data, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", nargs="?", type=Path, default=Path("out/tests"))
    fetch(parser.parse_args().destination)
