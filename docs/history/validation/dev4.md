# Moonmark 0.1.0-dev.4 — concept UI and native zoom validation

## Scope

This milestone keeps the single-process Rust/C++20/Qt 6 Widgets/QTextDocument architecture. Comrak, semantic/presentation models, syntax classification, image policy/cache, file behavior, and native window-state semantics were preserved. No dependency, frontend framework, browser engine, runtime, editor, vault, or Android implementation was introduced.

The approved visual concept supplied the graphite sidebar/header hierarchy, restrained typography, broad canvas, and table direction. Its traffic-light captions, scenery, and nonfunctional navigation were not copied. Windows caption behavior remains native. No proprietary implementation code or assets were copied.

## Implemented changes

- A 236px collapsible native sidebar: Open, Reload, current filename, and existing heading outline. Native tree activation supports keyboard and mouse. It automatically collapses below 1000px unless explicitly toggled, and hides in F11.
- A 48px quiet native header: sidebar control, abbreviated parent/filename breadcrumb, zoom, secondary menu, and retained Windows captions. Open and the embedded icon remain available when navigation is collapsed.
- Compact “Ready to read” empty state with Open/drop guidance. Diagnostics remain available through F12, not a mandatory profiler footer.
- Stronger H1/H2 hierarchy and native heading-level metadata; broad 12.75pt body typography remains. Compact quote markers, lists/tasks and thin rules are preserved.
- Tables have faint outer/horizontal borders, weaker vertical separators, graphite headers, 8px vertical/14px horizontal padding, 11.75pt text, and native column alignment. Content-sized numeric columns leave surplus width to text.
- Inline code uses a subdued graphite background, including in tables. Fences retain one native frame with 14px padding, a real language separator, Copy, preserved whitespace, and warm syntax spans; their surface is slightly lighter and subtly bordered.
- True percentage zoom updates existing native fonts, blocks, frames, cell padding, numeric-column widths, list tabs, and loaded image dimensions. Selection and a top-visible text anchor are preserved. It uses immutable 100% metrics rather than repeatedly scaling previous results.
- Percentage menu (80/100/125/150/200), Ctrl+wheel, Ctrl+plus/minus, Ctrl+0, and existing minus/plus buttons share the same scaling path.

## Zoom defect and reproduction

The old QTextEdit zoomIn/zoomOut path changed the default font in whole-point increments, while the constructed document used explicit font sizes. Percentages were therefore inaccurate and visually ineffective.

Before the fix, the mixed visual fixture stayed exactly 2342.5px tall at 100%, 125%, 150%, and 80%; the new metric test failed at every non-100% setting. After the initial fix, the same fixture measured 2342.5/2870.4/3514.6/1938.1px respectively, returning exactly to baseline at 100%. Later typography/table/sidebar changes legitimately altered absolute heights.

The final regression checks visit 100 → 125 → 150 → 100 → 80 → 100. They verify explicit font sizes, emphasis/link formats, paragraph metrics, frame padding, table padding, unchanged text, cursor selection, and unchanged parse/load/construction/image-request counters. The image smoke repeats these checks after all successful assets are decoded.

## Validation environment and results

Windows x64; rustc 1.98.0 (88d9e12ae), cargo 1.98.0 (797e8a9bc), MSVC 14.51, Qt 6.11.2. Build TEMP/TMP were directed to project-local target/tmp. No global installation or configuration changes were made.

| Check | Result |
| --- | --- |
| cargo fmt --all --check | Pass |
| cargo check | Pass |
| cargo clippy --all-targets --all-features -- -D warnings | Pass |
| cargo test | Pass: 20 unit tests, 4 acceptance tests, 1 native smoke matrix |
| cargo build --release | Pass, 58.18s final compilation |
| cargo run -- fixtures/concept-presentation.md --smoke-render | Pass, native launch, selection and code Copy |
| git diff --check | Pass |
| Native zoom/navigation/style/render/image/watcher/window smoke matrix | Pass |
| Packaged icon/render/style/navigation smoke | Pass |
| Qt palette and Rust syntax/theme checks | Pass: no blue/cyan/teal theme colors introduced |

No final Rust or MSVC /W4 warnings were emitted. Git emitted existing LF-to-CRLF normalization notices; line-ending configuration was not changed. The initial zoom regression intentionally failed before the fix. A snapshot harness initially treated empty environment flags as enabled; actual visual inspection exposed the wrong menu captures, the checks were corrected to require value 1, and the matrix was regenerated.

Additional exact executable arguments exercised (native GUI processes were awaited through System.Diagnostics.Process):

```powershell
target/debug/generate_stress_fixture.exe
target/release/moonmark.exe --smoke-images fixtures/generated/image-stress.md
target/release/moonmark.exe --smoke-render fixtures/generated/large-text.md
target/release/moonmark.exe --smoke-zoom fixtures/generated/large-text.md
target/release/moonmark.exe --smoke-layout fixtures/layout-transitions.md
target/release/moonmark.exe --smoke-maximize fixtures/layout-transitions.md
target/release/moonmark.exe --smoke-layout-normal fixtures/layout-transitions.md
cargo run --release --bin renderer_benchmark -- fixtures/generated/image-stress.md
cargo run --release --bin renderer_benchmark -- fixtures/generated/large-text.md
pwsh -NoProfile -File tools/visual_snapshots.ps1
pwsh -NoProfile -File tools/visual_snapshots.ps1 -Executable target/release/moonmark.exe
pwsh -NoProfile -File scripts/package_windows.ps1
```

Packaged Moonmark.exe was run with --smoke-icon, --smoke-render, --smoke-style, and --smoke-navigation against fixtures/concept-presentation.md. PATH was limited to C:\Windows\System32 and C:\Windows; Qt discovery variables were cleared. dumpbin /DEPENDENTS inspected the EXE and every shipped DLL.

## Performance

Local observations, not CI time limits or hardware-independent guarantees. Headless benchmarks use 20 warm runs. GUI process memory is sampled working set, not isolated heap/cache accounting. GUI wall time includes process/Qt startup and smoke waits.

| Fixture / measurement | Result |
| --- | --- |
| Image fixture | 17,885 source bytes, 382 top-level blocks, 255 image references |
| Valid images | 250 loaded, 0 failed decodes, 0 pending; 5 intentional missing references remain placeholders |
| Image dedup/cache | 6 requests; 15,360,000 bytes decoded cache (14.65 MiB) |
| Images + post-decode zoom checks | 654ms from smoke scheduling; 3276ms whole process |
| Image smoke sampled peak working set | 256.38 MiB |
| Image parse p50 / p95 | 0.566 / 1.793ms |
| Image presentation p50 / p95 | 17.311 / 20.245ms |
| Large text | 728,923 source bytes; 10,001 top-level blocks |
| Large parse p50 / p95 | 34.252 / 42.739ms |
| Large presentation p50 / p95 | 9.686 / 11.317ms |
| Large native construction | 565.850ms; one parse, load, construction |
| Large render sampled peak working set | 231.73 MiB |
| Large zoom plus exhaustive metric assertions | approximately 889–950ms per changed percentage |
| Large zoom sampled peak working set | 231.63 MiB |

Large text native heights were 425161.5px at 100%, 520202.1px at 125%, 645241.1px at 150%, and 349035.8px at 80%. Both returns to 100% restored 425161.5px. Every zoom/window-state check reported zero additional parses, file loads, document constructions, and image requests.

Large-document zoom still performs synchronous native formatting/reflow. The exhaustive assertion time is included above, so it is not a pure input-latency measure, but this remains a noticeable-pause risk requiring further profiling. No custom text engine or virtualization was introduced speculatively.

## Visual review

Actual Qt-rendered captures, not mockups, are generated under ignored target/visual-dev4:

empty.png, prose.png, headings-lists.png, tables.png, code.png, narrow.png, wide.png, images.png, concept.png, sidebar-hidden.png, menu.png, selection.png, zoom-80.png, zoom-100.png, zoom-125.png, zoom-150.png, and code-150.png.

All 17 cases were inspected during development. The final Release matrix was regenerated; concept, wide numeric columns, 80%, and 150% were re-inspected after the final table adjustment. Additional intermediate sidebar/table captures remain ignored.

Findings and iterations:

1. Initial navigation typography was too small; sidebar/title sizes were increased without increasing control bulk.
2. Old tables lacked column structure; headers, vertical rules and outer edges were added with lower contrast than primary text.
3. Wide and 80% tables wasted space on short numeric values; numeric columns now retain content-sized widths.
4. Native percentage changes visibly affect headings, code, rows, wrapping and padding; chrome does not scale with content zoom.
5. Selection stays native and neutral. Warm code syntax remains distinct. No Moonmark theme blue was observed; original image/icon colors and OS text rasterization are not recolored.
6. Sidebar collapse restores broad reading space at narrow widths; long code/table overflow remains viewport-level.

The result is closer to the approved concept's hierarchy, but not a pixel-for-pixel reproduction or a claim of user approval. Native code/table corners remain square, and inline backgrounds are tighter than the concept's rounded chips.

## Package and dependencies

| Artifact | Bytes | MiB |
| --- | ---: | ---: |
| Moonmark.exe | 6,339,072 | 6.045 |
| Complete portable folder | 34,936,739 | 33.318 |
| Dependencies/artwork/notices overhead | 28,597,667 | 27.273 |
| Portable ZIP | 15,494,608 | 14.777 |

Largest native dependencies: Qt6Core.dll 10,363,704 bytes, Qt6Gui.dll 9,546,552, Qt6Widgets.dll 6,497,080, qwindows.dll 991,032. Five app-local MSVC CRT DLLs total 1,048,216 bytes. The package also includes the approved artwork, configuration, and existing notices/licenses. No new dependencies or license choices were introduced.

Imports contain Qt, native MSVC support, and ordinary Windows libraries (including the tested host's system ICU), not CLR/hostfxr, JVM, Node, or a browser runtime. Qt itself is app-local. Packaged icon smoke found seven embedded sizes without the old external ICO. No clean VM or minimum-Windows-version certification was performed. Installer creation, static Qt/single-EXE distribution, and release licensing review remain separate work.

The old generated deploy/Moonmark folder and ZIP were replaced by the packaging script after resolving the exact project-local paths; they are reproducible outputs, not user source. No source assets were removed.

## Complete project-file changelog

New files:

- native/qt/document_zoom.h and document_zoom.cpp: baseline native formatting snapshot and proportional scaling/checks.
- native/qt/document_sidebar.h and document_sidebar.cpp: file-first actions and native outline tree.
- fixtures/concept-presentation.md: synthetic concept-oriented tables/prose/quotes/tasks/code fixture.
- docs/DEV4_VALIDATION.md: this report.

Modified files:

- Cargo.toml and Cargo.lock: root package version only, dev.4; no dependency changes.
- build.rs: compile/watch the two contained native helpers through Cargo.
- native/qt/moonmark_qt.cpp: sidebar integration, breadcrumb/empty state, zoom input, native formatting/table refinements, snapshot and smoke coverage.
- native/qt/moon_style.h: centralized sidebar/header metrics and graphite presentation colors.
- native/qt/moon_style.cpp: native navigation/header/control typography and achromatic states.
- fixtures/document-tables.md: current table-style expectation text.
- tests/native_frontend.rs: native zoom and navigation assertions.
- tools/visual_snapshots.ps1: 17 cases, zoom/state controls, explicit process awaiting for Release GUI executables, restored process environment.
- README.md: dev.4 feature/architecture summary.
- NEXT_STEPS.md: review/physical validation and remaining renderer work.
- docs/ARCHITECTURE.md: contained sidebar/zoom ownership.
- docs/ARCHITECTURE_HISTORY.md: accurate discontinued-runtime history and unchanged dev.4 architecture.
- docs/BUILDING.md: dev.4 snapshot workflow.
- docs/NATIVE_RENDERER.md: actual table/zoom pipeline and limitations.
- docs/UI_STYLE.md: current concept translation and typography/layout rules.
- docs/IMAGE_PIPELINE.md: display-only zoom and retained decode behavior.
- docs/WINDOW_FRAME.md: responsive sidebar/fullscreen chrome behavior.
- docs/PACKAGING.md: current embedded-icon behavior, size and verification scope.
- docs/DEV3_VALIDATION.md: small report wording cleanup; historical technical results preserved.

No tracked implementation files were deleted in this milestone. Rust core source, ABI, parser, image pipeline, syntax libraries, branding sources, and caption implementation were preserved. Generated fixtures, native snapshots/settings, build output and portable packages remain ignored and project-local.

## Remaining verification and limitations

- Physical Windows taskbar/snapping, Alt+Tab/icon, drag/resize, mixed-DPI/multi-monitor, and exact monitor transitions need human testing. Automated Normal/Maximized/F11 restoration and geometry checks passed on this host.
- Continuous selection and copy passed native tests; the accessible text interface exists. Real screen-reader/UI Automation traversal and table semantics were not certified.
- Open/Reload, last-directory selection, external-file behavior, local path policy and watcher remain intact; the watcher smoke passed. No physical file-dialog selection was performed in this round.
- Middle-button autoscroll and link gestures are preserved; physical interaction remains part of user review.
- Frame/inline rounding is not simulated. Native viewport-level code/table overflow and native table copy semantics remain.
- Sidebar preference is session-local; no recent-files/vault/settings subsystem was added. Outline navigation is not a scroll-synchronized active-heading tracker.
- Large-document zoom remains synchronous. Increasing zoom may magnify an existing bounded image decode rather than fetching a sharper image.
- Linux remains architecturally supported but was not built/tested on Linux. Android and editing work were not started.
- No intentional source/configuration/software changes outside D:\Projects\Moonmark; explicitly authorized external reference inspection was read-only. Test clipboard content is restored. Ordinary OS/tool-managed activity was not filesystem-audited.

Next: user review of the dev.4 visuals, then physical Windows integration and targeted large-document zoom profiling. No further architecture migration is proposed.
