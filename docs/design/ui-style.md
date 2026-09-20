# Wolfmark native UI style — 0.1.0-dev.7

Wolfmark applies the locked visual identity from [the canonical design reference](wolfmark-visual-identity.md) through native Qt 6 Widgets. The shell uses layered charcoal and graphite surfaces, metallic-silver/off-white typography, and restrained crimson interaction accents. It remains a native, broad, document-first reader rather than a themed browser surface or dashboard.

## Shell and navigation

The 252px collapsible sidebar contains real Open/Reload actions, a compact Open Documents list, and the active document's heading outline supplied by Wolfmark's presentation model. The current document receives one narrow crimson marker; hover and inactive states remain neutral. Open-document entries activate retained sessions and expose a quiet close control. Outline entries navigate native document anchors by mouse or keyboard. There is no vault, folder browser, import requirement, Starred, or Archive.

The sidebar collapses automatically below 1000px window width unless the user explicitly toggles it, and hides during F11. The 52px native title bar retains Windows caption controls, a sidebar toggle, parent-directory/filename breadcrumb, compact zoom, and the secondary-actions menu. Branding uses the approved paw at a compact size rather than the full banner. Open and the symbol return to the header when the sidebar is hidden.

The empty state is a compact “Ready to read” prompt with Open and drag/drop guidance. Normal viewing has no permanent profiler/footer; F12 retains diagnostics. Native captions, drag, system menu, file dialog, and watcher behavior are preserved.

## Surface and color hierarchy

- the outer shell is the darkest charcoal;
- navigation and title surfaces use a distinct graphite layer;
- the document surface is a calm dark neutral;
- cards and raised controls are only slightly lighter than their parent;
- primary text is off-white, with neutral silver secondary/muted levels;
- crimson is limited to active navigation, checked/focus state, progress, primary actions, links, and the quote marker.

Wolfmark-controlled UI does not use blue, cyan, teal, navy, azure, indigo, blue-gray, or blue-tinted silver. Syntax highlighting may retain restrained non-blue semantic colors. Original document images and approved brand assets are not recolored.

## Document presentation

- Broad responsive canvas, no article-width cap; 20–48px side padding at 100%.
- Body text stays clean and highly readable. H1/H2 use an editorial Georgia/Cambria/system-serif preference with strong but restrained hierarchy; H3–H6 remain distinct and retain native heading metadata.
- Tables use a graphite header, visible horizontal structure, quieter vertical separators, compact native cells, and native selection.
- Inline code keeps native selectable text over a compact graphite surface. The existing narrow paint decoration supplies horizontal breathing room without inserted source characters or replacement text objects.
- Fences remain one continuous native QTextFrame with restrained graphite depth, language metadata, separator, Copy action, syntax spans, and preserved whitespace. Long lines use the viewport's horizontal scrollbar.
- Quotes use a narrow crimson marker and muted text without becoming a large card. Lists/tasks remain compact; rules stay understated. Images remain native document resources rather than UI cards.

## Controls and settings

Centralized Qt styling covers push buttons, checkboxes, line edits, combo boxes, sliders, scrollbars, menus, tooltips, tabs, trees, focus, hover, pressed, and disabled states. Geometry is softly rounded but compact; controls do not become oversized pills. Primary controls are crimson; secondary controls remain graphite/silver.

Settings uses a 218px left navigation rail and real content pages. In dev.7 these are Updates and About: update preferences, checks, current version, licensing, and implementation facts use actual application state. The layout deliberately omits mockup-only premium, editor, portable-mode, donation, and invented feature controls.

## Windows corners and accessibility

The restored Windows 11 window requests rounded native DWM corners. Maximized and borderless-fullscreen windows request square corners; Normal restoration requests rounded corners again. Wolfmark does not clip QWidget rendering to fake the shape. Native dragging, Snap Layout hit testing, resize, captions, system menus, multi-monitor behavior, and F11 restoration remain part of the existing frame contract.

Qt retains whole-document selection, clipboard, text layout, keyboard behavior, and accessibility plumbing. The outline remains a native tree with accessible labels and keyboard activation. Crimson focus indicators remain visible; the no-blue rule is not implemented by removing accessibility state.

## Zoom

Percentage zoom remains real native layout scaling, not a visual transform. It scales explicit fonts, paragraph/list spacing, tabs, code-frame padding, table-cell padding, numeric widths, and loaded image geometry from immutable 100% metrics. Selection and a top-visible text anchor are retained; the QTextDocument is not replaced.

Use minus/plus, the percentage menu (80/100/125/150/200), Ctrl+wheel, Ctrl+plus/minus, or Ctrl+0 to reset. Zoom persists when a document reloads. Parsing, semantic/presentation regeneration, and mass image decode are not part of zoom.

The authoritative supplied mockups are tracked under `assets/design-reference/wolfmark/`. They define visual direction, not fictional application functionality.
