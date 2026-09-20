# Branding assets

The approved source artwork lives in [`assets/design-reference/wolfmark/`](../../assets/design-reference/wolfmark/). `wolfmark-logo.png` is the application paw, `Wolfmark-banner.png` is the complete wordmark, and `wolfmark-markdown.png` / `wolfmark-text.png` are the approved document icons.

`scripts/build_branding_assets.py` trims transparent padding and scales those sources into `assets/branding/wolfmark-banner.png`, `assets/branding/wolfmark-symbol.png`, multi-resolution PNGs, and the three Windows ICO files. The script does not redraw or recolor the artwork.

Cargo embeds `assets/icons/wolfmark.ico` into `Wolfmark.exe`. At runtime Qt reconstructs the application/window icon from the executable resource, so Explorer, taskbar, and Alt+Tab identity do not depend on an absolute developer path or external icon file. The installer uses the same app icon and registers the approved Markdown and text document icons.

The compact paw belongs in the title bar, taskbar, executable, and installer. The full banner is reserved for documentation or release surfaces with enough room. Do not stretch, recolor, trace, or replace the approved artwork.

See [Wolfmark visual identity](wolfmark-visual-identity.md) for the locked design rules.
