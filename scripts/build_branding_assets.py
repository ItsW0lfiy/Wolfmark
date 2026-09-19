"""Build Moonmark symbol and platform icon assets from the approved raster source.

This script performs deterministic cropping, matting, and neutral grayscale conversion.
It does not redraw, trace, or otherwise reinterpret the approved logo; the source stays
untouched while UI derivatives comply with Moonmark's achromatic theme.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageOps


ROOT = Path(__file__).resolve().parents[1]
BRANDING = ROOT / "assets" / "branding"
SOURCE = BRANDING / "moonmark-logo-source.png"
FULL_LOGO = BRANDING / "moonmark-logo.png"
SYMBOL = BRANDING / "moonmark-symbol.png"
GENERATED = ROOT / "assets" / "icons"

EXPECTED_SOURCE_SIZE = (1536, 1024)
SYMBOL_REGION = (390, 360, 616, 576)
FULL_LOGO_REGION = (365, 340, 1145, 600)
ICON_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)
DOCUMENT_ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)
DOCUMENT_ICONS = {
    GENERATED / "file-preview_MD.png": GENERATED / "moonmark-markdown.ico",
    GENERATED / "file-preview_TEXT.png": GENERATED / "moonmark-text.ico",
}


def to_achromatic(image: Image.Image) -> Image.Image:
    rgba = image.convert("RGBA")
    alpha = rgba.getchannel("A")
    gray = ImageOps.grayscale(rgba)
    return Image.merge("RGBA", (gray, gray, gray, alpha))


def extract_symbol(source: Image.Image) -> Image.Image:
    x0, y0, x1, y1 = SYMBOL_REGION
    rgba = source.crop(SYMBOL_REGION).convert("RGBA")

    # The source is finished artwork on a non-uniform backdrop, not a layered logo
    # file. These bounds were checked against the supplied 1536x1024 source. A 4x
    # mask keeps the original pixels while cleanly excluding only backdrop/shadow.
    scale = 4
    alpha = Image.new("L", (rgba.width * scale, rgba.height * scale), 0)
    draw = ImageDraw.Draw(alpha)

    def local_box(box: tuple[int, int, int, int]) -> tuple[int, int, int, int]:
        left, top, right, bottom = box
        return (
            (left - x0) * scale,
            (top - y0) * scale,
            (right - x0) * scale,
            (bottom - y0) * scale,
        )

    draw.ellipse(local_box((403, 374, 597, 566)), fill=255)
    draw.rounded_rectangle(local_box((516, 397, 604, 562)), radius=5 * scale, fill=255)
    alpha = alpha.resize(rgba.size, Image.Resampling.LANCZOS)
    rgba.putalpha(alpha)
    bounds = alpha.getbbox()
    if bounds is None:
        raise RuntimeError("No symbol foreground was found in the approved source")
    rgba = rgba.crop(bounds)

    canvas_size = 512
    padding = 42
    available = canvas_size - padding * 2
    scale = min(available / rgba.width, available / rgba.height)
    resized = rgba.resize(
        (max(1, round(rgba.width * scale)), max(1, round(rgba.height * scale))),
        Image.Resampling.LANCZOS,
    )
    canvas = Image.new("RGBA", (canvas_size, canvas_size), (0, 0, 0, 0))
    canvas.alpha_composite(
        resized,
        ((canvas_size - resized.width) // 2, (canvas_size - resized.height) // 2),
    )
    return canvas


def validate_outputs() -> None:
    symbol = Image.open(SYMBOL)
    if symbol.size != (512, 512) or symbol.mode != "RGBA":
        raise RuntimeError("The generated symbol must be a 512x512 RGBA image")
    alpha = symbol.getchannel("A")
    if alpha.getextrema() != (0, 255):
        raise RuntimeError("The generated symbol must retain transparent padding")

    for path in (FULL_LOGO, SYMBOL):
        red, green, blue, _ = Image.open(path).convert("RGBA").split()
        if ImageChops.difference(red, green).getbbox() or ImageChops.difference(
            green, blue
        ).getbbox():
            raise RuntimeError(f"Generated UI branding must be achromatic: {path}")

    for size in ICON_SIZES:
        icon = Image.open(GENERATED / f"moonmark-{size}x{size}.png")
        if icon.size != (size, size) or icon.mode != "RGBA":
            raise RuntimeError(f"Invalid generated PNG at {size}x{size}")

    ico = Image.open(GENERATED / "moonmark.ico")
    expected_ico_sizes = {(size, size) for size in ICON_SIZES if size <= 256}
    if ico.ico.sizes() != expected_ico_sizes:
        raise RuntimeError(
            f"Windows ICO sizes differ: expected {expected_ico_sizes}, got {ico.ico.sizes()}"
        )

    expected_document_sizes = {(size, size) for size in DOCUMENT_ICON_SIZES}
    for source_path, icon_path in DOCUMENT_ICONS.items():
        source = Image.open(source_path).convert("RGBA")
        if source.width < 256 or source.height < 256:
            raise RuntimeError(f"Approved document icon source is too small: {source_path}")
        if source.getchannel("A").getextrema()[0] == 255:
            raise RuntimeError(f"Approved document icon source has no transparency: {source_path}")
        icon = Image.open(icon_path)
        if icon.ico.sizes() != expected_document_sizes:
            raise RuntimeError(
                f"Windows document ICO sizes differ: expected {expected_document_sizes}, "
                f"got {icon.ico.sizes()}"
            )


def main() -> None:
    source = Image.open(SOURCE)
    if source.size != EXPECTED_SOURCE_SIZE:
        raise RuntimeError(
            f"Expected approved source size {EXPECTED_SOURCE_SIZE}, got {source.size}"
        )

    GENERATED.mkdir(parents=True, exist_ok=True)
    to_achromatic(source.crop(FULL_LOGO_REGION)).save(FULL_LOGO, optimize=True)
    symbol = to_achromatic(extract_symbol(source))
    symbol.save(SYMBOL, optimize=True)
    for size in ICON_SIZES:
        icon = symbol.resize((size, size), Image.Resampling.LANCZOS)
        icon.save(GENERATED / f"moonmark-{size}x{size}.png", optimize=True)

    symbol.save(
        GENERATED / "moonmark.ico",
        format="ICO",
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)],
    )
    for source_path, icon_path in DOCUMENT_ICONS.items():
        Image.open(source_path).convert("RGBA").save(
            icon_path,
            format="ICO",
            sizes=[(size, size) for size in DOCUMENT_ICON_SIZES],
        )
    validate_outputs()
    print(f"Built {FULL_LOGO}")
    print(f"Built {SYMBOL}")
    print(f"Built {len(ICON_SIZES)} PNG sizes and Windows ICO in {GENERATED}")
    print(f"Built {len(DOCUMENT_ICONS)} approved document ICO assets in {GENERATED}")


if __name__ == "__main__":
    main()
