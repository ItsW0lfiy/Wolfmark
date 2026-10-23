# Wolfmark native UI style — 0.1.0-dev.7

Wolfmark applies the locked visual identity from [the canonical design reference](wolfmark-visual-identity.md) through native Qt 6 Widgets. The shell uses layered charcoal and graphite surfaces, metallic-silver/off-white typography, and restrained crimson interaction accents. It remains a native, broad, document-first reader rather than a themed browser surface or dashboard.

## Shell and navigation

The 240px collapsible sidebar contains real Open/Reload actions, a compact Open Documents list, and the active document's heading outline supplied by Wolfmark's presentation model. The current document receives one narrow crimson marker; hover and inactive states remain neutral. Open-document entries activate retained sessions and expose a quiet close control. Outline entries navigate native document anchors by mouse or keyboard. There is no vault, folder browser, import requirement, Starred, or Archive.

The sidebar collapses automatically below 1000px window width unless the user explicitly toggles it, and hides during F11. The 48px native title bar retains Windows caption controls, a sidebar toggle, a parent-directory/filename breadcrumb, a working Find entry, and Settings. A 48px contextual command row below the document tabs contains the three editing modes, Save/status, zoom, and the secondary-actions menu. Branding uses the approved paw at a compact size rather than the full banner. Open and the symbol return to the header when the sidebar is hidden.

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

Settings uses a 218px left navigation rail with Updates, About and Reader & Editor pages. A permanent titlebar Settings button opens the existing dialog. Editor wrapping is a real persisted preference; new documents always begin in Reader. The original titlebar remains dedicated to window-level navigation and Settings. When a document is open, the separate contextual command row exposes a three-button Reader / Live Preview / Source selector with the current mode visibly checked; Save appears when required, and zoom plus the document-actions menu stay available. Save As and Find remain available from the document-actions menu and keyboard shortcuts; the titlebar Find entry invokes the same working search interface. Tabs appear only when two or more documents are open, with restrained close controls; the sidebar's Open Documents list remains available as before.

## Windows corners and accessibility

The restored Windows 11 window requests rounded native DWM corners. Maximized and borderless-fullscreen windows request square corners; Normal restoration requests rounded corners again. Wolfmark does not clip QWidget rendering to fake the shape. Native dragging, Snap Layout hit testing, resize, captions, system menus, multi-monitor behavior, and F11 restoration remain part of the existing frame contract.

Qt retains whole-document selection, clipboard, text layout, keyboard behavior, and accessibility plumbing. The outline remains a native tree with accessible labels and keyboard activation. Crimson focus indicators remain visible; the no-blue rule is not implemented by removing accessibility state.

## Zoom

Percentage zoom remains real native layout scaling, not a visual transform. It scales explicit fonts, paragraph/list spacing, tabs, code-frame padding, table-cell padding, numeric widths, and loaded image geometry from immutable 100% metrics. Selection and a top-visible text anchor are retained; the QTextDocument is not replaced.

Use minus/plus, the percentage menu (80/100/125/150/200), Ctrl+wheel, Ctrl+plus/minus, or Ctrl+0 to reset. Zoom persists when a document reloads. Parsing, semantic/presentation regeneration, and mass image decode are not part of zoom.

The authoritative supplied mockups are tracked under `assets/design-reference/wolfmark/`. They define visual direction, not fictional application functionality.

## Crimson Graphite shell redesign (in progress)

The first redesign stage is the native shell: graphite titlebar, functional Find and Settings,
a separate contextual mode/zoom command row, refined multi-document tabs and a compact
sidebar action row. Controls keep their existing Qt signal/shortcut behavior. This stage
is not a complete implementation of the approved visual concept; Markdown presentation,
secondary dialogs and installer chrome remain separate design milestones. The application
continues to default newly opened documents to Reader. Do not implement visually convincing
but nonfunctional controls to imitate the reference image.


## October 2026 — dialog frame and mode-width validation

Settings now uses a dedicated modal Qt dialog frame with Wolfmark's own
48px graphite titlebar, actual caption buttons, and Windows native-frame
hit testing. Accepted/rejected dialog behavior, settings persistence, and
normal keyboard navigation remain unchanged. The top-level Windows border
is not left to the operating system accent color. On Windows, window
drag, caption controls, resize and Snap interactions require physical
verification in addition to Linux offscreen screenshot tests.

Reader / Live Preview / Source segments use font-metric-derived minimum
widths so Live Preview cannot be clipped by a fixed narrow control at
larger DPI scales. The automated shell control test asserts visible
caption widths and exercises all three modes, then Find.


## Crimson Graphite full redesign

See [Crimson Graphite implementation and acceptance](crimson-graphite-2026-10.md) for the
concrete Qt layout contract, interaction rules, rendering constraints, Rust/C++ ownership,
and platform-specific verification matrix. The redesigned Settings option rows, native
code decorations, real multi-document actions, and installer chrome follow the same
graphite/crimson system. The mockup is a visual target, not permission to add inert
controls or invent user state.
