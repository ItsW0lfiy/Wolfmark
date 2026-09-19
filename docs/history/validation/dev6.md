# Moonmark 0.1.0-dev.6 validation

Validation date: 2026-09-11. Platform: Windows x64. The initial dev.6 work started at `81796b372d25ae51a722e7d7957e7f84e059d220`; the final motion/rendering hardening round started from pushed HEAD `b502aedadf2ecfb95b723f5f6720936606dd138f`.

## Product behavior

- One process retains one Rust backend, native `DocumentView`/`QTextDocument`, outline, watcher, debounce timer, zoom, scroll, selection, and image state per open document.
- Opening a canonical duplicate activates its existing session. Closing the final session returns to the empty state. No single-instance lock or inter-process routing was added, so independent Moonmark processes remain supported.
- `.txt` files bypass Comrak, headings, links, images, and the outline. The literal fixture preserves Markdown-looking characters, line breaks, tabs, Unicode, spaces, selection, and long horizontal lines.
- An inactive plain-text session's watcher reloaded that session once while the active Markdown session's load, parse, and image-request counters remained unchanged. The plain-text parse delta remained zero.
- Heading navigation uses a direct anchor-position map, resolves current geometry with deterministic top breathing room, and lands immediately. Image delivery, resize, and zoom correct the same semantic destination while top-visible character/offset anchoring protects ordinary layout changes; no queued animation or stale pixel coordinate is retained.
- Document and outline wheel motion preserve partial angle deltas in one continuous floating-point trajectory. Repeated input extends the live target without resetting velocity, reversal changes that trajectory coherently, and there is no pixels-per-frame cap. Pixel/touchpad scrolling, scrollbar dragging, keyboard paging/Home/End, heading links, outline activation, tab restoration, and layout corrections remain direct.
- The controller advances from the real viewport paint event requested by a precise adaptive timer: 16 ms normally and 8 ms only during high distance/velocity. The final sub-pixel tail settles within 1.25 px rather than producing isolated one-pixel staircase updates.
- Visible-image discovery is coalesced to roughly 15 Hz during motion with velocity-aware look-ahead. Completed image format/layout application is deferred until owned wheel motion settles. Custom code-frame corner geometry is cached across paints and rebuilt only after layout mutation; quote and inline-code work remains bounded to visible blocks.
- Sidebar width uses a short native animation that reverses from its actual partial width. `MOONMARK_REDUCED_MOTION=1` makes owned transitions immediate for accessibility and deterministic testing.

## Renderer review

The code-block review found one continuous low-contrast graphite frame with an integrated neutral language header, real separator, quiet Copy action, syntax color, preserved blank lines, and no hard outer stroke. Qt's native `QTextFrameFormat` has no corner-radius property, so a narrow post-paint mask rounds only the four frame corners while the frame geometry, text, selection, copy, scrolling, and accessibility remain native.

Tables retain immediate column tracking through softened vertical separators, slightly stronger header/horizontal structure, minimal outer framing, compact numeric columns, native selection, and inline-code formatting. They do not read as spreadsheet controls.

The prose inline-code screenshot exposed a residual punctuation-like end pixel from antialiased path subtraction. The follow-up fix clips each horizontal cap outside a small glyph-overhang guard and leaves Qt's native character background vertically authoritative. It changes neither text, copy output, wrapping, selection, nor accessibility. Wrapped prose, lists, quotes, and table cells were rechecked after the fix.

The existing image-layout regression remains fixed. Loaded images and placeholders report zero unexplained excess block height after declared margins across reload, narrow/wide resize, and 80/100/125/150% zoom.

## Performance measurements

Release measurements are local samples, not hard CI thresholds. The rapid-scroll trace records changed-scroll paints in a bounded in-memory buffer and prints one summary after the run; it does not perform per-frame console output. Physical acceptance remains visual because compositor presentation cannot be inferred solely from QWidget paint events.

| Case | Result |
| --- | --- |
| Retained two-document switch, 40 alternations | 323 microseconds average |
| Direct anchor lookup | 2–5 microseconds in repeated local motion smokes |
| Rapid-scroll input to first changed paint | 17.0–17.3 ms across final Release text/image/mixed samples |
| 10,000-block rapid scroll | paint interval p50 8.29 ms, p95 16.22 ms, p99 32.23 ms, worst 33.02 ms; 4/2/0/0 intervals over 16.67/25/33.3/50 ms |
| 250-image rapid scroll | paint interval p50 8.15 ms, p95 16.21 ms, p99 31.59 ms, worst 31.85 ms; 3/2/0/0 intervals over 16.67/25/33.3/50 ms |
| Mixed code/table/image rapid scroll | paint interval p50 8.92 ms, p95 17.03 ms, p99/worst 32.54 ms; 5/1/0/0 intervals over 16.67/25/33.3/50 ms |
| Paint CPU cost (text / image / mixed) | p95 5.80 / 1.54 / 1.98 ms; worst 6.41 / 2.34 / 3.36 ms |
| Controller interval worst (text / image / mixed) | 18.13 / 17.22 / 18.36 ms; frame-position advancement is paint-synchronized |
| Image visibility scan rate during rapid motion | 15–16 scans across 124–135 controller frames in the large fixtures; p95 scan cost 0.100 ms |
| Sidebar-width reversal | 12 samples; largest observed step 47–49 px; reversed without resetting to an endpoint |
| 650,000-character literal text construction, Release | 485,811 microseconds; parse count 0 |
| Image stress semantic parse, 20 runs | p50 0.635 ms; p95 1.441 ms |
| Image stress presentation construction | p50 22.111 ms; p95 25.088 ms |
| 10,000-block semantic parse, 20 runs | p50 49.995 ms; p95 65.346 ms |
| 10,000-block presentation construction | p50 14.671 ms; p95 21.717 ms |
| Image stress completion | 250 loaded, 5 intentional missing, 0 decode failures, 6 canonical requests, 15.36 MB cache, 404 ms completion / 600 ms total smoke |
| One moderate document after 3 s | 120.35 MiB working set; 45.99 MiB private |
| Four moderate retained documents after 3 s | 122.12 MiB working set; 48.05 MiB private |
| Four documents including large text and image stress | 196.66 MiB working set; 126.14 MiB private |

Normal-document zoom action/paint times remained in the low tens of milliseconds. The 10,000-block fixture required approximately 110–118 ms of synchronous format work and approximately 1.0–1.18 seconds total including Qt relayout and exhaustive verification. All zoom cases reported parse, file-load, document-construction, and image-request deltas of zero. This large-document relayout is the main known performance limitation.

## Native and package checks

The complete Cargo test run passed 21 core unit tests, 4 acceptance tests, 2 native frontend tests, and documentation tests. Strict Clippy passed with `-D warnings`. The C++20 `/W4` build emitted no compiler warning. Release build and portable assembly passed. The smoke harness verified Moonmark's exact Ctrl+A/C and code-Copy payloads internally; Windows clipboard ownership was unavailable to these automated child processes, so physical clipboard integration remains a manual check rather than being mislabeled as verified.

The packaged application was tested with `PATH` limited to `C:\Windows\System32;C:\Windows` and Qt discovery variables removed. Icon, render/selection/code-copy, style, retained multi-document switching, layout-transition counters, and 250-image stress smokes passed.

The portable output contains 16 files:

- `Moonmark.exe`: 6,458,368 bytes (6.16 MiB)
- complete folder: 35,091,308 bytes (33.47 MiB)
- dependency/assets/legal overhead: 28,632,940 bytes (27.31 MiB)
- compressed ZIP: 15,561,112 bytes (14.84 MiB)

The largest app-local dependencies are Qt6Core (10,363,704 bytes), Qt6Gui (9,546,552), Qt6Widgets (6,497,080), qwindows (991,032), and the app-local MSVC runtime set (1,048,216 combined). `dumpbin /DEPENDENTS` found Qt, MSVC CRT, and Windows system libraries; system ICU is supplied by Windows. No CLR/hostfxr, JVM, Node.js, browser engine, WebView2, Chromium, or Qt WebEngine dependency is present.

## Visual artifacts inspected

Ignored snapshots under `target/visual-dev6` cover: empty, prose, headings/lists, tables, code, inline prose/table, inline wrap, inline selection, plain text, image gap, image stress, narrow, wide, concept, sidebar hidden, menu, whole-document selection, zoom 80/100/125/150, code at 150%, and a Markdown-plus-text multi-document window.

Inspection found an achromatic broad document surface, clear active/inactive file entries, literal text presentation, integrated code metadata, genuinely rounded code-frame corners, quieter table framing, retained neutral selection, and no theme blue. The user-reported prose inline-code end pixel was visible before the clipped-cap correction and absent afterward.

## Commands

```powershell
cargo fmt --all --check
cargo check
cargo test
cargo clippy --all-targets --all-features -- -D warnings
cargo build --release
cargo run --release --bin renderer_benchmark -- fixtures/generated/image-stress.md
cargo run --release --bin renderer_benchmark -- fixtures/generated/large-text.md
pwsh -File tools/render_performance.ps1 -Executable target/release/moonmark.exe
pwsh -File tools/visual_snapshots.ps1 -Executable target/debug/moonmark.exe
pwsh -File scripts/package_windows.ps1
git diff --check
```

## Manual validation still required

Automated Qt/Win32 checks do not replace physical Windows 10/11 validation of Snap, Alt+Space details, taskbar work area, mixed-DPI multi-monitor transitions, real high-resolution touchpad input, screen-reader/UI Automation output, or prolonged animation frame pacing. Linux remains architecturally wired through guarded platform code and `pkg-config`, but was not compiled on this Windows host. Android, editor, installer, updater, signing, and WinGet implementation were not started.
