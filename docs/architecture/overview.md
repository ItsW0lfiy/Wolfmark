# Architecture

Wolfmark is a single native process with Cargo as its top-level workflow. Rust and C++20 are approved first-class implementation languages; subsystem ownership follows technical fit rather than a mandated language percentage.

```text
ordinary Markdown file
  -> native source session and Rust Comrak parsing
  -> Wolfmark semantic document model
  -> Wolfmark framework-neutral presentation commands
  -> versioned C ABI function table
  -> C++ Qt Widgets presentation adapter
  -> native QTextDocument / QTextEdit display
```

## Rust ownership

- headless file loading and rendering counters (native sessions read/save source buffers)
- Comrak parsing and Markdown extension handling
- semantic and presentation models
- heading IDs and TOC targets
- Syntect/two-face syntax classification and Wolfmark token palette
- local image canonicalization and remote-image policy
- bounded image decode scheduling, deduplication, generation cancellation, and LRU cache
- release metadata selection, SemVer comparison, and SHA-256 artifact verification
- diagnostics, benchmarks, fixtures, and framework-neutral window-state semantics

## C++/Qt ownership

- QApplication and the Qt Widgets shell
- authoritative source buffers, native undo, safe saving, tabs and editing modes ([details](document-editing.md))
- custom title bar with contextual document actions, F12 diagnostics row, empty state, file dialog, drag/drop, and file watcher
- collapsible document navigation using the existing framework-neutral TOC, with native tree accessibility and heading anchors
- native QTextDocument construction through QTextCursor, QTextFrame, QTextTable, and native formats
- selection, clipboard, link activation, zoom/reflow, scrollbars, and middle-button autoscroll
- presentation-time image placement and UI-thread conversion of Rust RGBA results to QImage
- Windows frameless-window hit testing and system integration
- typed settings persistence and asynchronous Qt Network release transport

## Interoperability

`src/native_api.rs` constructs a versioned table of `extern "C"` functions used by `wolfmark_qt_run`. `native/qt/wolfmark_api.h` mirrors plain-layout buffers, image results, counters and callbacks. Rust-owned buffers are released only through Rust callbacks. Unsafe Rust is confined to documented ABI ownership/pointer operations and the native entry-point call.

This narrow C ABI was selected over CXX-Qt because the existing framework-neutral presentation model already serializes cleanly, the ABI keeps ownership explicit, it avoids generated framework glue, and it preserves one process with Cargo in control. JSON is used only as an in-process presentation-data encoding; it is not a network or browser protocol.

Qt types do not enter the semantic model. The current adapter is replaceable, while QTextDocument supplies mature selection, layout, accessibility plumbing, and copy behavior.

## Retained document sessions

The desktop window owns an ordered set of open-document sessions. Each has one canonical path, Rust backend, Reader document, shared Source/Live Preview editor, outlines and file/directory watcher. The active tab selects retained widgets without disk reads or reparsing. Closing releases native resources and the Rust image pipeline and cancels source analysis delivery. Canonically identical paths activate the existing session. See [document editing](document-editing.md) for source ownership and saving.

Markdown sessions follow the normal Comrak semantic path. `.txt` sessions are explicitly tagged `plainText` by the Rust presentation contract and carry exact source text; they do not create a Comrak AST, Markdown commands, TOC entries, links, or image requests.

`native/qt/wolf_style.*` owns the locked Wolfmark charcoal/graphite, silver, and restrained-crimson palette, dimensions, and Qt interaction-state styling. `native/qt/wolf_title_bar.*` owns native-painted caption controls plus title-bar move, double-click, and system-menu behavior. Markdown construction remains in the presentation adapter and does not depend on either component.

Windows is primary. Shared Rust logic and most Qt Widgets code are portable; Win32 message handling is confined to the Qt adapter's guarded Windows sections. The Linux build path uses `pkg-config` for Qt6Widgets discovery, but compilation and behavior still require physical validation.

`native/qt/document_sidebar.*` adapts existing outline data without owning document semantics. `native/qt/document_zoom.*` keeps immutable baseline native formats and applies percentage changes to the same read-only QTextDocument. Neither component reparses Markdown or owns image decoding.
