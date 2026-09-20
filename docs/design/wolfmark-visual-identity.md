# Wolfmark visual identity

This document locks the approved visual identity for Wolfmark. Changes to the core identity require explicit user approval.

## Product and source assets

The product name is **Wolfmark**. Wolfmark is free and open-source software; its visual presentation must never imply paid tiers, premium-only functionality, subscriptions, or advertising.

The authoritative source assets are stored in [`assets/design-reference/wolfmark/`](../../assets/design-reference/wolfmark/):

- `wolfmark-logo.png` — the paw mark used for the application and installer icon.
- `Wolfmark-banner.png` — the paw and Wolfmark wordmark for appropriate release or documentation surfaces.
- `wolfmark-markdown.png` — the Markdown document icon.
- `wolfmark-text.png` — the text document icon.
- `ChatGPT Image Sep 20, 2026, 11_27_42 PM (1).png` — approved main-reader visual reference.
- `ChatGPT Image Sep 20, 2026, 11_27_42 PM (2).png` — approved settings visual reference.
- `ChatGPT Image Sep 20, 2026, 11_27_42 PM (3).png` — approved setup visual reference.

The mockups are authoritative for atmosphere, hierarchy, spacing, typography, surfaces, restrained crimson accents, and the relationship between the app and setup experience. Their sample content and feature claims are not specifications. Wolfmark must show real product state and implemented features only.

Derived Windows icons and runtime images must remain faithful crops/scales of these approved sources. Do not redraw, recolor, distort, or replace them.

## Colour and surfaces

Wolfmark is dark-first, not permanently dark-only. The design system must remain capable of supporting a future light theme without implementing one as part of this identity lock.

The dark hierarchy is:

1. outer shell — the darkest charcoal;
2. navigation and title surfaces — medium-dark graphite;
3. document canvas — a calmer, slightly lighter charcoal;
4. raised panels and controls — one step lighter than their parent;
5. typography — off-white, metallic silver, and neutral gray;
6. active state — restrained Wolfmark crimson.

Crimson is reserved for selected navigation markers, primary actions, active toggles, progress, useful focus indication, and limited interactive emphasis. It must not flood the interface. Metallic treatment belongs mainly to the approved branding and subtle detail, not to gradients on every control.

Wolfmark-controlled UI must not introduce blue, cyan, teal, navy, azure, indigo, blue-gray, or blue-tinted silver. This includes links, selection, focus, toggles, progress, hover, pressed states, Qt defaults, and installer controls. Syntax highlighting may use restrained non-blue semantic colours. Accessibility takes priority where behavior is genuinely owned by Windows rather than Wolfmark.

## Typography and geometry

Native UI uses a clean Windows system face such as Segoe UI Variable/Segoe UI. Document headings may use a restrained editorial serif treatment when the system font is available, while body text remains highly readable. No redistributed font is required and no novelty wolf-themed font is permitted.

Controls use compact modern geometry with modest radii, consistent heights, and visible keyboard focus. Avoid giant pills and excessive card nesting. Prefer spacing, alignment, and typography before adding borders or separators.

The restored Windows 11 window requests native rounded corners through DWM. Maximized and borderless-fullscreen states use square screen edges; restoring returns native rounded corners. Qt continues to own the window and Windows continues to own native move, resize, snapping, system-menu, DPI, and monitor behavior. Do not simulate the entire window outline by clipping QWidget content.

## Application shell

The main reader keeps a broad, document-first desktop layout. Its left sidebar uses medium graphite, compact real actions, restrained separators, and a narrow crimson marker for the current item. The document surface remains dominant and visually distinct without becoming a narrow article column. The integrated title area uses the compact paw mark and product name; the full banner does not belong in the normal title bar.

Sidebar items must reflect real open documents and outline data. Do not add decorative folders, archives, slogans, mountains, or fake navigation from the mockup. Empty, error, loading, and disabled states must use the same surface hierarchy and remain useful.

## Document presentation

Wolfmark retains native QTextDocument/QTextEdit rendering, selection, copying, accessibility, and the framework-neutral semantic/presentation model.

- Headings use a strong but readable editorial hierarchy.
- Paragraphs remain spacious without wasting the desktop canvas.
- Blockquotes use a restrained crimson or silver left marker and muted text, not a large card.
- Inline code uses a compact graphite surface with native selectable text.
- Fenced code uses one coherent raised graphite frame with restrained edges and metadata.
- Tables stay compact, with horizontal structure stronger than vertical separators.
- Links and interactive states use restrained crimson rather than blue.
- Images remain document content and are not restyled as decorative application cards.

## Settings and controls

Settings use a left navigation rail and a clear content area with real settings only. The active section receives the same narrow crimson marker as the main navigation. Update/version information must come from real application state. Do not add a portable-mode toggle, donate button, paid tier, editor setting, or invented release data merely because a mockup depicts it.

Push buttons, checkboxes, toggles, combo boxes, edits, sliders, scrollbars, menus, tooltips, tabs, lists, and trees share the graphite/silver/crimson system. Hover, pressed, focus, disabled, checked, and selected states must all be explicit so stock blue Qt states cannot leak through.

## Setup relationship

The WiX 7 Burn bootstrapper remains a native C++20/Qt Widgets application. Setup, progress, completion, maintenance, repair, uninstall, and error states use the same paw, graphite layers, silver type, crimson actions, restrained borders, and rounded control geometry as Wolfmark itself.

Ordinary installation stays compact. The four-step composition in the approved setup mockup is a visual reference, not a requirement to introduce an artificial wizard. Setup must never claim Wolfmark is premium, silently take Windows defaults, or expose features that do not exist.

