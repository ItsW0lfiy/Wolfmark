# Android core-portability audit — dev.7

This is groundwork, not an Android release plan. No Android UI framework, Java/Kotlin layer, APK build, editor, or mobile interaction model was selected or implemented.

## What is already portable in design

The following Rust-owned layers contain no intentional Win32 or Qt UI types and are conceptually reusable:

- Comrak parsing and Moonmark's Markdown compatibility preprocessing;
- semantic Markdown nodes and conversion;
- the framework-neutral presentation document/commands, TOC, metrics, and diagnostics;
- syntax-language detection and highlighting policy;
- bounded image cache, request generation/cancellation, and most raster/SVG decoding logic;
- theme tokens and settings values that are not tied to desktop storage;
- the framework-neutral Normal/Maximized/BorderlessFullscreen state model (although those exact modes are desktop concepts and need not be reused by Android).

This separation remains valuable: a future Android presentation can consume Moonmark semantics without importing Qt Widgets or reproducing the Markdown parser.

## Current desktop/path coupling

The current `Backend::open_document(&str)` owns desktop file reading and canonicalization. It assumes a UTF-8 string names a path that `std::fs` can open and that `std::fs::canonicalize` can turn into a stable identity. Image requests likewise carry canonical filesystem path strings and workers reopen those paths. This is appropriate and fast on Windows/Linux, but it is not a complete Android document model.

The Qt layer currently owns:

- file dialogs and drag/drop;
- `QFileSystemWatcher` and external-change scheduling;
- retained desktop window/session widgets;
- Windows HWND/non-client integration behind the existing platform boundary;
- the narrow in-process C ABI consumer.

The package `build.rs` currently supports only Windows and Linux and always builds the Qt desktop adapter for those targets. It deliberately rejects other targets. Therefore the repository cannot currently run `cargo check --target aarch64-linux-android --lib` as a meaningful core-only check without first separating/gating the desktop build step.

## Android document-access boundary

Android's Storage Access Framework can grant a document through a `content://` URI that is not a normal path, may not have a stable canonical filesystem name, and may only be readable through a platform-provided descriptor/stream. Moonmark must not fake that URI as a `PathBuf`.

A later, explicitly approved experiment should introduce a small framework-neutral source boundary around concepts such as:

- stable document identity supplied by the platform;
- display name and optional logical base/resource context;
- read document bytes/text;
- reopen or observe changes when the platform permits;
- resolve an explicitly referenced resource through the document provider or a separately granted URI;
- decode image bytes/streams without requiring the worker to reopen a desktop path.

Desktop `Path`/`std::fs` implementations should remain the direct fast path. The abstraction belongs at file/resource acquisition, not in Comrak or the presentation model.

## Likely Android build/tooling blockers

- Android Rust target and NDK/linker are not installed in the current environment; only `x86_64-pc-windows-msvc` is installed.
- The package build script has no core-only/Android path and currently requires Qt desktop compilation.
- Native dependencies used by syntax highlighting and image/SVG decoding need an Android-target compilation audit.
- A mobile UI, accessibility/selection behavior, lifecycle, clipboard, intent/document-provider integration, and packaging/signing strategy remain undecided.
- Desktop file watching and absolute/parent-path semantics cannot simply be projected onto provider-granted Android documents.

These are expected platform boundaries, not evidence that the semantic/presentation core needs a rewrite.

## Recommended next Android experiment

After the first Windows prerelease is validated, create an explicitly scoped core-build experiment:

1. separate or feature-gate the desktop Qt build step so the Rust library can be checked independently;
2. install/configure an Android Rust target and NDK only with explicit user approval;
3. compile the parser, semantic model, presentation conversion, and byte-based image decode path for `aarch64-linux-android`;
4. prototype a test-only in-memory document/resource source rather than selecting a mobile UI;
5. record any native crate/toolchain blockers before deciding the Android frontend.

Linux remains the next intended desktop platform under the current Qt architecture. This Android audit does not move Android ahead of Linux or promise APK support in dev.7.
