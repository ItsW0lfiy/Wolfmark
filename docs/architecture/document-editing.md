# Document sessions and editing

New files open in **Reader**. Each session owns its canonical path, Rust
rendering/image backend, Reader document, one native source document, watcher,
mode, source/render revisions and SHA-256 file fingerprint. Reopening a canonical
path activates its existing tab. A fresh open always begins in Reader.

## Shared source and history

Source and Live Preview share one `QPlainTextEdit`, `QTextDocument` and native undo
stack. Mode changes alter fonts and syntax decorations, never source text.
Reader derives its presentation from the current buffer, including unsaved edits.
It rebuilds after source changes when needed, or safe reload; scrolling and
returning to unchanged tabs do not parse or reconstruct documents.

The editor provides native selection/clipboard, undo/redo, line numbers,
line/column status, wrapping, Tab/Shift+Tab indentation, Markdown syntax formats,
Find/next/previous and source-level bold/italic toggles. Ctrl+S saves,
Ctrl+Shift+S opens Save As, Ctrl+W closes a tab, Ctrl+F opens Find, and
Ctrl+B/Ctrl+I format source while the editor has focus. Ctrl+Y/Ctrl+Shift+Z redo.

Live Preview formats headings, emphasis, strike, links and code. Heading prefixes,
paired emphasis/code delimiters and unambiguous inline-link syntax are minimized
outside their cursor/selection region, and revealed inside it. Positions remain
native UTF-16 source offsets. No rich-text/HTML serialization writes Markdown.

Comrak supplies ranges with the Reader extension options. Analysis uses original
input, rather than Reader's local-path normalization that can insert characters.
It is debounced by 180 ms, uses at most two workers and allows one pending
analysis per editor. Revision checks discard obsolete results. Tasks own input
and cancellation state, never GUI/session pointers. Changed syntax rehighlights
affected blocks without replacing the document. Comrak parsing remains a full
parse after idle time, while native typing is incremental.

Decorations belong to retained native blocks and use relative offsets. Inserting
text or a line above a block therefore does not invalidate its cached formatting.
The performance probe waits for analysis before insertion measurements and reports
the number of blocks reformatted after the localized edit.

Tables, task/list/quote markers, fences, reference-link syntax, frontmatter, inert
HTML and unsupported constructs retain editable source representations. Live
Preview does not provide rendered table editing, checkbox widgets, embedded image
editing or full Obsidian parity. Source remains the complete raw interface.

## Position and lifetime

Presentation commands carry top-level Comrak source lines. Reader indexes their
native cursor positions to map modes to corresponding source blocks rather than
copying scrollbar values. Editing modes retain exact selections. Reader round
trips retain the editor selection when the reading location has not moved.
Mapping inside large containers is approximate.

Literal `.txt` Reader positions map directly through native line numbers. Source
anchoring uses visual-line scrollbar units, rather than Reader's pixel units.
Reload snapshots cursor offsets before replacing text, then matches nearby source
context and the nearest occurrence; repeated paragraphs do not select Chapter 1
merely because it contains the first matching text.

Reader resize, zoom and image delivery retain a visible character and viewport
offset. Smooth motion rebases remaining travel after relayout. Direct input
cancels outstanding semantic navigation. QObject contexts cancel destroyed-view
callbacks; Rust image generations reject obsolete decodes.

The Chapter-1 regression was an unconditional zero-delay scrollbar write in
`DocumentView::load`: reload restored its anchor synchronously, then the callback
reset it. The test reproduced `58 -> 58 -> 0` before the fix and
`58 -> 58 -> 58` afterward. Deferred initialization now schedules resources only.
Wheel destinations are clamped before integer conversion, including reduced
motion and sidebar paths. Smooth motion respects current layout limits.

## File integrity

Editing supports valid UTF-8, optionally with BOM. Invalid UTF-8, NUL and literal
Unicode paragraph separators are explicitly rejected before buffer replacement.
LF/CRLF/CR terminators use native block metadata participating in undo. New lines
inherit nearby conventions. Saving preserves literal blocks, nonbreaking spaces,
trailing whitespace, final-newline presence and untouched mixed endings. Tests
compare bytes after no-op saves and undo, including empty and BOM files.

`QSaveFile`, with direct-write fallback disabled, stages atomic replacement.
SHA-256 fingerprints are checked before staging and immediately before commit.
Failures retain dirty content. This is not an OS-level compare-and-swap lock
against external writers racing the final comparison/rename.

File and directory watchers coalesce changes. Matching fingerprints identify
self-saves. Clean external changes reload only their session; dirty sessions
retain edits and expose conflict status. Explicit dirty Reload requires a discard
decision. Failed reads, missing paths or unsupported replacement encodings never
clear source. Save As offers recovery and refuses targets owned by another tab.
Dirty tab/window close offers Save/Discard/Cancel. There is no autosave, vault,
document database or companion file.

## Settings and verification

The existing Settings dialog has a permanent titlebar entry and Reader & Editor
page. Schema 2 adds word wrapping while retaining schema-1 preferences. Reader
remains the new-document default. Run `cargo test --test native_frontend` for
native editor/session/scroll regressions. Snapshots accept
`WOLFMARK_SNAPSHOT_MODE=source` or `live` with `--smoke-snapshot`.

Native snapshots are actual Qt renders. Physical free-spin, touchpad/IME,
screen-reader, DPI/multimonitor and extended-memory acceptance remain separate
from programmatic widget tests.
