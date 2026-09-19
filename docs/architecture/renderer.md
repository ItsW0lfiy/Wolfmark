# Native renderer

Comrak 0.54 parses Markdown in Rust. CommonMark is the baseline, with the GFM table, strikethrough, task-list, and autolink extensions deliberately enabled. Moonmark converts its semantic DocumentModel to framework-neutral PresentationDocument commands. The Qt adapter builds QTextDocument through native cursor, block, frame, table, and character APIs. Markdown is never converted to HTML; raw HTML remains inert source.

Supported presentation includes H1–H6, paragraphs, soft/hard breaks, bold/italic/combined emphasis/strike, inline code, inline/reference/collapsed/shortcut links, autolinks, anchors/outline, nested ordered/unordered/task lists, quotes, rules, fences, syntax highlighting, tables with alignment, and local images. Footnotes have basic textual presentation, frontmatter remains inert source, and GitHub alerts receive ordinary quote presentation rather than dedicated alert semantics.

## Compatibility profile

Moonmark aims for broad practical Markdown compatibility through one coherent parser profile; it does not claim every Markdown dialect. Standard CommonMark syntax and the enabled GFM extensions above are supported through Moonmark's semantic and native presentation layers. Local inline image and link destinations additionally accept literal filesystem spaces without requiring angle brackets or percent encoding. That compatibility step is limited to local-looking destinations outside code and leaves ordinary remote URLs, reference definitions, and existing `<...>` / `%20` forms to Comrak.

| Feature | Parsing | Native presentation |
| --- | --- | --- |
| CommonMark blocks and inlines | Comrak baseline | Supported |
| GFM tables, strikethrough, tasks, autolinks | Enabled Comrak extensions | Supported |
| Inline/reference/collapsed/shortcut links | Comrak baseline | Supported; destinations retained |
| Footnotes | Enabled Comrak extension | Basic text, without backlink navigation |
| YAML frontmatter | Enabled delimiter recognition | Inert source text |
| Raw HTML | Parsed, never executed | Inert source text |
| Literal-space local destinations | Narrow Moonmark parser-input normalization | Existing local resolver and presentation |
| GitHub alerts | Not specially enabled | Ordinary blockquote content only |

Obsidian wiki links and embeds, GitHub website integrations, dedicated GitHub alert/callout styling, description lists, math, emoji shortcodes, and other dialect-specific extensions are not enabled. They may be evaluated individually later; there is no dialect/profile framework in the current early-development renderer.

Comrak accepts optional titles on ordinary links and images. Image titles cross Moonmark's semantic/presentation boundary; link destinations and content are retained, but the current native viewer does not yet expose link titles as tooltips. Rich footnote navigation/backlinks and dedicated alert presentation are later renderer work rather than implicit compatibility claims.

Code remains one graphite QTextFrame with metadata, a real separator, preserved whitespace, and Copy. Rust classifies syntax once during presentation construction; unknown languages fall back to plain monospace. The palette uses warm amber, sage, orange, cream, red, and silver, never blue/cyan/teal.

QTextEdit supplies continuous mouse/keyboard selection. Ctrl+A selects the native document; Ctrl+C preserves document order while stripping object/table separator characters. Links activate on click gestures, not drag or Shift-selection. Heading links and the sidebar navigate the same native anchors. Qt's accessible text interface remains present.

Tables retain native cell selection and left/center/right alignment. Their graphite header, faint outer/horizontal lines, weaker vertical lines, and modest padding support scanning. Numeric columns in mixed tables have content-sized widths; remaining columns use Qt's text layout and available desktop space. Inline-code character backgrounds stay within the text flow.

## Percentage zoom and lifecycle

DocumentZoom captures immutable 100% native formats once after construction. Zoom updates existing text/block/frame/cell formats and image display geometry, with no Markdown parse, semantic rebuild, presentation regeneration, or QTextDocument replacement. It preserves the cursor selection and a top-visible text anchor. Width changes simply reflow the same document; decoded images are reused.

The old default-font zoom path was ineffective because the renderer assigns explicit text sizes. It also approximated percentages as whole font-point steps. Dev.4 replaces it with proportional scaling of the actual native formats, including code/table/list metrics. Ctrl+wheel and keyboard shortcuts use the same path as the percentage menu.

Qt has no CSS-style radius/padding API for inline character backgrounds. Dev.5 extends their native graphite surface with a small rounded painted edge using Qt's own line/cursor geometry. The underlying characters, shaping, wrapping, and accessibility remain native; no padding spaces or image/text objects are inserted. Selected spans defer entirely to native selection painting. This is decorative breathing room, not a true inline box model: it does not reserve additional advance width between tightly adjacent characters. Trailing wrapping whitespace is excluded from decoration.

Standalone images use 100% line height and explicit block margins. The previous 125% line height added unintended leading proportional to image height (115.8px for a 463px image), despite the image block's bounding rectangle reporting the correct image height. The regression compares the following block's position against the image block's bottom and declared margins, across loading and zoom.

Long code uses the document's horizontal scrollbar rather than per-block overflow. Very large documents still require native format updates and layout during zoom; profiling separates action/paint time from exhaustive verification.

Dev.6 removes the hard code-frame stroke and uses one low-contrast graphite `QTextFrame` with integrated language metadata, a quiet real separator, native syntax spans, and the existing Copy anchor. `QTextFrameFormat` has no corner-radius API; a contained viewport paint pass masks only the frame's square corner wedges to the document color. It does not rasterize or replace the native text/frame. Tables retain subtle horizontal structure while using still quieter vertical and outer separators.

Heading anchors remain real `QTextCharFormat` named anchors attached to heading blocks and are indexed directly by anchor name during document construction. Explicit navigation resolves current cursor geometry, applies a deterministic landing inset, and moves directly. Reflow after resize, zoom, or image completion corrects the live semantic anchor, so provisional image geometry does not become a stale destination. Resize, zoom, image delivery, and reload also retain a top-visible character plus its viewport offset; this avoids raw-scrollbar anchoring when layout height changes.

Custom inline-code and quote decoration starts at the first visible text block and stops below the viewport. Code-frame corner geometry is cached after layout mutations and searched by the visible vertical range during paint, avoiding a full frame-geometry traversal on every scroll paint.

Literal text uses a separate presentation mode. The Qt adapter inserts the exact source into one read-only native document with a monospace fallback stack and no wrapping, preserving tabs, whitespace, long lines, selection, clipboard behavior, and zoom without Markdown interpretation.
