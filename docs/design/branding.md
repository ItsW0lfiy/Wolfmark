# Branding

`assets/branding/moonmark-logo-source.png` is the approved, untouched source. `moonmark-logo.png`, `moonmark-symbol.png`, PNG icon sizes, and `moonmark.ico` are derived native assets. The compact symbol appears in the title bar and empty state alongside live Moonmark text. The complete mark is reserved for suitable documentation or release artwork.

Cargo's Windows resource step embeds `moonmark.ico` in the executable. At runtime Qt reconstructs the application/window icon from that executable resource, so Explorer, taskbar, and Alt+Tab identity do not depend on an absolute developer path or an external icon file. App-local branding images remain available for content drawn inside the custom shell.

Do not redraw, trace, recolor, stretch, or replace the approved mark. Moonmark is independent and must not inherit branding, identifiers, or assets from another application.
