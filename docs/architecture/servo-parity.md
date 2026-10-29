# Servo migration parity checklist

Baseline: Qt main c6c6d63; Servo c966081. Migration branch:
`migration/servo-parity-20261029`. Qt remains the default. No production switch
or removal is authorized until acceptance. Existing untracked Servo Cargo.lock
is retained. Origin currently uses the legacy Moonmark SSH URL; fetch succeeds.

| Area / evidence | Shared Rust | Servo baseline / acceptance test |
| --- | --- | --- |
| Reader: wolfmark_qt.cpp, document_zoom.cpp | markdown/{parser,model,highlight}, presentation | Semantic rendering exists; full-width and txt no-wrap already implemented. Verify fixtures, all font sizes at 65–170%, table and image sizing. |
| Images: architecture/images.md, app/state.rs | images/{paths,loader,cache} | Only paths reused; raw raster data URLs, 2 MiB limit; no bounded decode/cache or SVG. Test formats, unsafe payloads, paths, worker cancellation and animation. |
| Sessions: wolfmark_qt.cpp, document_sidebar.cpp | app/state.rs renders buffers, not session registry | Open/close/history/reload exist; mode resets, zoom shared, no retained scroll/selection/render cache. Test independent session state and duplicate canonical paths. |
| Editing: document_editor.cpp, architecture/document-editing.md | markdown/editor.rs provides UTF-16 source decorations | Source read-only, Live Preview disabled. Port buffer/undo/save/conflict ownership to Rust; retain exact BOM and mixed newline bytes. Test dirty mode transitions, atomic saves and external writers before enabling editing. |
| Search and clipboard: wolfmark_qt.cpp showFind/findNext | No shared search controller | Search placeholder; no code copy. Test forward/backward/wrap and clipboard in actual Servo. |
| Scroll: smooth_scroll_controller.cpp | experimental smooth_scroll.rs | Damped wheel trajectory exists; lacks layout bounds/rebase and middle autoscroll parity. Compare rapid/reversal/direct inputs, reduced motion, image reflow and idle CPU. |
| Frame: windows_window_frame.cpp, wolf_title_bar.cpp | window/{state,win32} | Opt-in winit frame; native default retained. Verify every edge, DPI, Snap, system menu, fullscreen restoration. Linux X11/Wayland separately. |
| Settings: wolfmark_settings.cpp | settings/mod.rs only backend defaults | Native settings persistence/migrations still Qt-owned; port schema/recovery and connect real controls. |
| Updates: wolfmark_updates.cpp | updates.rs release/checksum logic | No Servo adapter. Explicit GitHub-only transport, progress/error/actions need integration. |
| Security: image paths and resource delegates | images/paths.rs | Escaped raw HTML, inert external links, CSP, webview request deny; background requests still need audit and traffic verification. |
| Packaging: Cargo.toml, .cargo/config.toml, installer | maintenance crate and scripts | Separate Servo aliases preserved. Windows installer/portable and Debian runtime packaging unvalidated. |
| Android | Platform-independent semantic core | Not supported. Requires Android Servo embedding, lifecycle/surface/input/IME, SAF URI access and packaging; desktop dialogs/path assumptions must be abstracted. |

## Verification gates (not inferred from compilation)

- [ ] Qt/Servo actual screenshots at 1648 x 928, small/large and multiple DPI.
- [ ] Reader, Source and contextual Live Preview GUI scenarios.
- [ ] Safe save/close/reload/conflict byte-preservation scenarios.
- [ ] Windows physical scrolling/window/clipboard/IME testing.
- [ ] Linux X11 and Wayland builds and GUI testing.
- [ ] Offline traffic capture including engine background requests.
- [ ] Comparable release startup, idle CPU/RAM, document/image/edit/switch/resize/zoom timings and installed size.
- [ ] Packaging/install/uninstall/update and rollback rehearsal.

## Rollback and acceptance

Bare Cargo commands continue to build Qt. Servo is only selected by the existing
explicit aliases. Keep Qt packaging and CI intact. A future default-switch PR
must supply evidence for the gates above; do not treat this checklist as approval.
