"""Build Wolfmark runtime and Windows icon assets from the approved sources.

The source artwork is preserved in assets/design-reference/wolfmark. This script only
trims transparent padding and performs high-quality scaling; it never redraws, recolors,
or otherwise reinterprets the approved Wolfmark identity.
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "assets" / "design-reference" / "wolfmark"
BRANDING = ROOT / "assets" / "branding"
GENERATED = ROOT / "assets" / "icons"

PAW_SOURCE = REFERENCE / "wolfmark-logo.png"
BANNER_SOURCE = REFERENCE / "Wolfmark-banner.png"
MARKDOWN_SOURCE = REFERENCE / "wolfmark-markdown.png"
TEXT_SOURCE = REFERENCE / "wolfmark-text.png"

BANNER = BRANDING / "wolfmark-banner.png"
SYMBOL = BRANDING / "wolfmark-symbol.png"
ICON_SIZES = (16, 24, 32, 48, 64, 128, 256, 512)
DOCUMENT_ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)
DOCUMENT_ICONS = {
    MARKDOWN_SOURCE: GENERATED / "wolfmark-markdown.ico",
    TEXT_SOURCE: GENERATED / "wolfmark-text.ico",
}


def trimmed(source: Path, *, padding: int = 0) -> Image.Image:
    image = Image.open(source).convert("RGBA")
    bounds = image.getchannel("A").getbbox()
    if bounds is None:
        raise RuntimeError(f"Approved asset has no visible pixels: {source}")
    image = image.crop(bounds)
    if padding:
        canvas = Image.new(
            "RGBA", (image.width + padding * 2, image.height + padding * 2), (0, 0, 0, 0)
        )
        canvas.alpha_composite(image, (padding, padding))
        return canvas
    return image


def square_canvas(image: Image.Image, size: int, inset: float) -> Image.Image:
    available = round(size * (1.0 - inset * 2.0))
    scale = min(available / image.width, available / image.height)
    resized = image.resize(
        (max(1, round(image.width * scale)), max(1, round(image.height * scale))),
        Image.Resampling.LANCZOS,
    )
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.alpha_composite(
        resized, ((size - resized.width) // 2, (size - resized.height) // 2)
    )
    return canvas


def validate_outputs() -> None:
    symbol = Image.open(SYMBOL).convert("RGBA")
    if symbol.size != (512, 512):
        raise RuntimeError("The generated Wolfmark symbol must be 512x512")
    if symbol.getchannel("A").getextrema() != (0, 255):
        raise RuntimeError("The generated Wolfmark symbol must retain transparent padding")

    for size in ICON_SIZES:
        icon = Image.open(GENERATED / f"wolfmark-{size}x{size}.png").convert("RGBA")
        if icon.size != (size, size):
            raise RuntimeError(f"Invalid Wolfmark PNG at {size}x{size}")

    expected_app_sizes = {(size, size) for size in ICON_SIZES if size <= 256}
    app_icon = Image.open(GENERATED / "wolfmark.ico")
    if app_icon.ico.sizes() != expected_app_sizes:
        raise RuntimeError(
            f"Windows app ICO sizes differ: expected {expected_app_sizes}, "
            f"got {app_icon.ico.sizes()}"
        )

    expected_document_sizes = {(size, size) for size in DOCUMENT_ICON_SIZES}
    for source_path, icon_path in DOCUMENT_ICONS.items():
        source = Image.open(source_path).convert("RGBA")
        if source.getchannel("A").getextrema()[0] == 255:
            raise RuntimeError(f"Approved document icon has no transparency: {source_path}")
        icon = Image.open(icon_path)
        if icon.ico.sizes() != expected_document_sizes:
            raise RuntimeError(
                f"Windows document ICO sizes differ: expected {expected_document_sizes}, "
                f"got {icon.ico.sizes()}"
            )


def main() -> None:
    BRANDING.mkdir(parents=True, exist_ok=True)
    GENERATED.mkdir(parents=True, exist_ok=True)

    trimmed(BANNER_SOURCE).save(BANNER, optimize=True)
    source_symbol = trimmed(PAW_SOURCE)
    symbol = square_canvas(source_symbol, 512, 0.06)
    symbol.save(SYMBOL, optimize=True)
    for size in ICON_SIZES:
        icon = symbol.resize((size, size), Image.Resampling.LANCZOS)
        icon.save(GENERATED / f"wolfmark-{size}x{size}.png", optimize=True)

    symbol.save(
        GENERATED / "wolfmark.ico",
        format="ICO",
        sizes=[(size, size) for size in ICON_SIZES if size <= 256],
    )
    for source_path, icon_path in DOCUMENT_ICONS.items():
        document_icon = square_canvas(trimmed(source_path), 512, 0.025)
        document_icon.save(
            icon_path,
            format="ICO",
            sizes=[(size, size) for size in DOCUMENT_ICON_SIZES],
        )

    validate_outputs()
    print(f"Built {BANNER}")
    print(f"Built {SYMBOL}")
    print(f"Built {len(ICON_SIZES)} PNG sizes and Windows ICO in {GENERATED}")
    print(f"Built {len(DOCUMENT_ICONS)} approved document ICO assets in {GENERATED}")


if __name__ == "__main__":
    main()
