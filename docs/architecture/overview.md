# Architecture

Wolfmark is a single native process with Cargo as its top-level workflow. **The language boundary is explicit, not "best tool wins": Rust is the application/controller/core, while C++20/Qt is the native presentation and interaction adapter.** Qt APIs may supply rich text layout, widgets, input, accessibility and platform chrome; using Qt is not permission to move authoritative application logic out of Rust.

This is a **normative architecture contract**. The implementation still contains older ownership violations described below; documenting them does not approve them.

```text
ordinary Markdown file
  -> native source session and Rust Comrak parsing
  -> Wolfmark semantic document model
  -> Wolfmark framework-neutral presentation commands
  -> versioned C ABI function table
  -> C++ Qt Widgets presentation adapter
  -> native QTextDocument / QTextEdit display
```

## Rust ownership — required

- application controller, document/session registry, active-document decisions and authoritative source/state
- source loading, encoding decisions, save conflict policy, atomic persistence and settings persistence
- editing commands and persistence policy; the Qt text buffer/undo implementation is a **view implementation**, not the source of application authority
- Comrak parsing, Markdown semantics, presentation data, heading/TOC targets and source-decoration analysis
- Syntect/two-face syntax classification and Wolfmark token palette
- image path/security policy, decoding, deduplication, cancellation and caching
- update/release policy, version selection, artifact verification and decisions
- framework-neutral window-state semantics and core diagnostics, benchmarks and tests

## C++/Qt ownership — permitted

- QApplication, native Qt Widgets shell, sidebar, tabs, titlebar, dialogs and their visual state
- Qt-native QTextDocument/QTextEdit/QPlainTextEdit rendering, text layout, caret/selection, input and undo mechanisms as adapters to Rust-owned state
- styling, typography, code/quote/table presentation, accessibility, clipboard, zoom and smooth scroll
- user interaction signals and file picker presentation that **request** operations from Rust
- UI-thread wrapping of Rust-rendered image pixels into QImage
- native window move/resize/snap/caption handling and platform UI bridge
- installer setup UI, within an explicitly documented controller boundary

**Not permitted in new C++ code:** application policy, authoritative document/session management, final decisions about edits/saving, document or settings persistence, or update policy. An existing Qt implementation does not make an ownership violation acceptable.

## Interoperability

`src/native_api.rs` constructs a versioned table of `extern "C"` functions used by `wolfmark_qt_run`. `native/qt/wolfmark_api.h` mirrors plain-layout buffers, image results, counters and callbacks. Rust-owned buffers are released only through Rust callbacks. Unsafe Rust is confined to documented ABI ownership/pointer operations and the native entry-point call.

This narrow C ABI was selected over CXX-Qt because the existing framework-neutral presentation model already serializes cleanly, the ABI keeps ownership explicit, it avoids generated framework glue, and it preserves one process with Cargo in control. JSON is used only as an in-process presentation-data encoding; it is not a network or browser protocol.

Qt types do not enter the semantic model. The current adapter is replaceable, while QTextDocument supplies mature selection, layout, accessibility plumbing, and copy behavior.

## Retained document sessions — current implementation and migration

**Current (legacy) implementation:** the desktop Qt window owns an ordered set of retained sessions and their widgets, canonical paths, watchers, dirty flags and mode selections. Each has a Rust backend. Returning to an unchanged tab preserves its rendered view without rereading or reparsing. See [document editing](document-editing.md).

**Required target:** Rust becomes authoritative for the session registry, file identity, source edits, dirty/conflict status, persistence and navigation decisions. Qt retains per-session view objects and maps Rust-assigned identifiers to widgets. Migration must be incremental and tested for mixed newlines, unsaved changes, conflict protection, source/Reader position mapping, undo, and mode changes. Do not attempt a blind rewrite of QTextDocument's native UI behavior.

Markdown sessions follow the normal Comrak semantic path. `.txt` sessions are explicitly tagged `plainText` by the Rust presentation contract and carry exact source text; they do not create a Comrak AST, Markdown commands, TOC entries, links, or image requests.

`native/qt/wolf_style.*` owns the locked Wolfmark charcoal/graphite, silver, and restrained-crimson palette, dimensions, and Qt interaction-state styling. `native/qt/wolf_title_bar.*` owns native-painted caption controls plus title-bar move, double-click, and system-menu behavior. Markdown construction remains in the presentation adapter and does not depend on either component.

Windows is primary. Shared Rust logic and most Qt Widgets code are portable; Win32 message handling is confined to the Qt adapter's guarded Windows sections. The Linux build path uses `pkg-config` for Qt6Widgets discovery, but compilation and behavior still require physical validation.

`native/qt/document_sidebar.*` adapts existing outline data without owning document semantics. `native/qt/document_zoom.*` keeps immutable baseline native formats and applies percentage changes to the same read-only QTextDocument. Neither component reparses Markdown or owns image decoding.

## C++ ownership audit (October 2026)

A first-pass static inventory examined all **30** checked-in C++ source/header/test files for persistence, process, network, document-management and Qt-native APIs. It is not a claim that a complete semantic ownership migration has already happened.

- **Presentation/platform adapters:** `document_sidebar.*`, `document_zoom.*`, `smooth_scroll_controller.*`, `windows_window_frame.*`, `wolf_style.*`, `wolf_title_bar.*`, and the ABI declaration headers.
- **Boundary violations needing migration:** `document_editor.cpp` currently implements Qt-side file read/write and encoding/fingerprint/save decisions; `wolfmark_qt.cpp` currently owns authoritative session/path/reload/dirty-state decisions; `wolfmark_settings.cpp` directly persists application settings; `wolfmark_updates.cpp` orchestrates network/download/persistence alongside Rust update policy.
- **Installer requires a separate controller review:** `packaging/windows/setup-ui/src/` includes a native Qt setup window and process/maintenance controller. UI composition belongs in Qt; install/update/uninstall policy and authoritative state should be clearly separated. Test and bootstrap files are included in the inventory.
- **Next migration order:** (1) Rust-owned document/session and source-save APIs (2) Rust-owned settings (3) update controller/transport boundary (4) installer controller boundary. Each migration must preserve existing integration tests, real native Qt rendering, and data-safety behavior.

Crimson Graphite UI layout, controls and QTextFrame quote painting are presentation details and **do not** authorize expanding Qt's application ownership.
