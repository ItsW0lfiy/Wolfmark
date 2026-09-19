# Architecture

Moonmark is a single native process with Cargo as its top-level workflow. Rust and C++20 are approved first-class implementation languages; subsystem ownership follows technical fit rather than a mandated language percentage.

```text
ordinary Markdown file
  -> Rust document session and Comrak parsing
  -> Moonmark semantic document model
  -> Moonmark framework-neutral presentation commands
  -> versioned C ABI function table
  -> C++ Qt Widgets presentation adapter
  -> native QTextDocument / QTextEdit display
```

## Rust ownership

- file reads, document identity, reload counters, and source lifetime
- Comrak parsing and Markdown extension handling
- semantic and presentation models
- heading IDs and TOC targets
- Syntect/two-face syntax classification and Moonmark token palette
- local image canonicalization and remote-image policy
- bounded image decode scheduling, deduplication, generation cancellation, and LRU cache
- release metadata selection, SemVer comparison, and SHA-256 artifact verification
- diagnostics, benchmarks, fixtures, and framework-neutral window-state semantics

## C++/Qt ownership

- QApplication and the Qt Widgets shell
- custom title bar with contextual document actions, F12 diagnostics row, empty state, file dialog, drag/drop, and file watcher
- collapsible document navigation using the existing framework-neutral TOC, with native tree accessibility and heading anchors
- native QTextDocument construction through QTextCursor, QTextFrame, QTextTable, and native formats
- selection, clipboard, link activation, zoom/reflow, scrollbars, and middle-button autoscroll
- presentation-time image placement and UI-thread conversion of Rust RGBA results to QImage
- Windows frameless-window hit testing and system integration
- typed settings persistence and asynchronous Qt Network release transport

## Interoperability

`src/native_api.rs` constructs a versioned table of `extern "C"` functions and calls `moonmark_qt_run`. `native/qt/moonmark_api.h` mirrors plain-layout structures for buffers, image results, and counters. Rust-owned buffers are released only through the supplied Rust callbacks. The only handwritten unsafe Rust block is the documented call across this ABI.

This narrow C ABI was selected over CXX-Qt because the existing framework-neutral presentation model already serializes cleanly, the ABI keeps ownership explicit, it avoids generated framework glue, and it preserves one process with Cargo in control. JSON is used only as an in-process presentation-data encoding; it is not a network or browser protocol.

Qt types do not enter the semantic model. The current adapter is replaceable, while QTextDocument supplies mature selection, layout, accessibility plumbing, and copy behavior.

## Retained document sessions

The desktop window owns an ordered set of open-document sessions. Each session has one canonical path, one Rust backend, one native `DocumentView`/`QTextDocument`, one outline, and one file watcher/debounce timer. The active session is only a `QStackedWidget` selection; switching it does not call the Rust open function or rebuild the native document. Closing a session releases its widget, watcher, native resources, and Rust image pipeline. Opening a canonically identical path activates the existing session.

Markdown sessions follow the normal Comrak semantic path. `.txt` sessions are explicitly tagged `plainText` by the Rust presentation contract and carry exact source text; they do not create a Comrak AST, Markdown commands, TOC entries, links, or image requests.

`native/qt/moon_style.*` owns the New Moon palette, dimensions, and Qt interaction-state styling. `native/qt/moon_title_bar.*` owns native-painted caption controls plus title-bar move, double-click, and system-menu behavior. Markdown construction remains in the presentation adapter and does not depend on either component.

Windows is primary. Shared Rust logic and most Qt Widgets code are portable; Win32 message handling is confined to the Qt adapter's guarded Windows sections. The Linux build path uses `pkg-config` for Qt6Widgets discovery, but compilation and behavior still require physical validation.

`native/qt/document_sidebar.*` adapts existing outline data without owning document semantics. `native/qt/document_zoom.*` keeps immutable baseline native formats and applies percentage changes to the same read-only QTextDocument. Neither component reparses Markdown or owns image decoding.
