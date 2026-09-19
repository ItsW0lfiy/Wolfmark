# UI style — New Moon, 0.1.0-dev.6

Moonmark translates the approved lunar concept into native Windows controls: a near-black canvas, graphite navigation/header layers, silver text, restrained separators, and broad document width. It does not copy the illustration's macOS captions, decorative lunar scenery, or nonfunctional navigation.

## Shell and navigation

The 236px collapsible sidebar contains real Open/Reload actions, a compact Open Documents list, and the active document's heading outline supplied by Moonmark's presentation model. Open-document entries activate retained sessions and expose a quiet close control. Outline entries navigate native document anchors by mouse or keyboard. There is no vault, folder browser, import requirement, Starred, or Archive. The sidebar collapses automatically below 1000px window width unless the user explicitly toggles it, and hides during F11. The 48px native titlebar retains Windows caption controls, a sidebar toggle, parent-directory/filename breadcrumb, compact zoom, and the secondary-actions menu. Open and the embedded symbol return to the header when the sidebar is hidden.

The empty state is a compact “Ready to read” prompt with Open and drag/drop guidance. Normal viewing has no permanent profiler/footer; F12 retains diagnostics. Native captions, drag, system menu, file dialog, and watcher behavior are preserved.

## Document presentation

- Broad responsive canvas, no article-width cap; 20–48px side padding at 100%.
- Body: 12.75pt, 150% line height. H1/H2 use 2.05/1.60 scale; H3–H6 remain distinct. Native heading-level metadata is retained.
- Tables: 1px outer border and horizontal separators, weaker 1px vertical separators, graphite header, 8px vertical / 14px horizontal cell padding, 11.75pt text and native alignment. Purely numeric columns in mixed tables reserve their content width instead of consuming surplus desktop space.
- Inline code: compact monospace with a neutral graphite background, including inside tables. A narrow custom paint pass adds only horizontal rounded breathing room around Qt's native character background; it never inserts artificial characters or replacement text objects. Qt's native background remains vertically authoritative so prose spans do not gain a stray lower edge.
- Fences: one continuous low-contrast graphite QTextFrame, no hard outer stroke, 16px padding, small neutral language metadata, actual separator, quiet Copy action, grouped warm syntax spans, and preserved whitespace. Square corners remain an explicit native limitation, not a simulated rounded overlay. Long lines use the viewport's horizontal scrollbar.
- Quotes: narrow neutral markers, muted native text, no filled card. Lists/tasks retain compact hanging markers; rules stay understated. Images remain integrated native document resources, not UI cards.

## Zoom

The percentage is actual native layout scaling, not a transform. It scales explicit fonts, paragraph/list spacing, tabs, code-frame padding, table-cell padding and numeric widths, and loaded image geometry from immutable 100% presentation metrics. Relative line heights and thin rule/border formats stay unchanged. Selection and a top-visible text anchor are retained; the QTextDocument is not replaced.

Use minus/plus, the percentage menu (80/100/125/150/200), Ctrl+wheel, Ctrl+plus/minus, or Ctrl+0 to reset. Zoom persists when a document reloads. No parsing, semantic/presentation regeneration, or mass image decode is associated with zoom.

## States and accessibility

Neutral Fusion palette roles and centralized styling cover selection, links, focus, hover, menu, tree navigation, and 12px scrollbars. Warm syntax colors and the restrained red close hover are the only deliberate UI/document exceptions; neither uses blue/cyan/teal. Original image content and the approved icon are not recolored. Native dialogs remain OS-owned.

Qt retains whole-document selection, clipboard, text layout and accessibility. The outline uses a native tree with accessible labels and keyboard activation. Automated text-interface checks do not replace screen-reader/UI Automation or physical mixed-DPI testing.

See [dev.6 validation](../history/validation/dev6.md) for actual screenshot review, measurements, and limitations.
