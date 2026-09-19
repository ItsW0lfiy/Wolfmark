# Moonmark 0.1.0-dev.3 — redesign and validation

## Scope and source control

This is a Qt Widgets / QTextDocument UI redesign, not an architecture migration. Rust, C++20, Cargo, one native process, Comrak, the semantic/presentation boundary, and the Rust image pipeline remain.

Started on clean local `main` at `782ec77703e771b7242dbd9254c295bc7bfe1885` (dev.2).
Configured origin: `git@github.com:Wolfyisdabest/Moonmark.git`.
Fetched origin/main was `0e4498bd84febc3eec89839d98600f467326d828`; local main was 15 commits ahead, zero behind. A later fetch found no remote change. Existing local commits were preserved; no reset, rebase, squash, branch switch, remote configuration, or push was performed.

Implementation commits, in order:

| Commit | Change |
| --- | --- |
| e33bf7f | Version bump to dev.3 |
| 17682ed | Snapshot harness, table fixture, project-local smoke settings |
| 45e06c1 | Single-row shell, quiet controls, native-painted captions, neutral Fusion palette |
| 8e16618 | Compact empty-state redesign |
| be5292a | Borderless document tables and retained column alignment |
| b28302c | Lighter code/quotes/rules/placeholders and responsive document spacing |
| 97fea61 | Remove oversized structural paragraph gaps around native frames |
| 82e533c | Fix link-click classification; native style, selection, and Copy tests |
| f6ad993 | Actual Qt menu/selection snapshot variants |
| 61c013c | Initial keyboard focus goes to Open instead of Minimize |

The accompanying documentation/report is committed separately.

## Complete tracked-file changelog

No tracked files were deleted.

| File | Change |
| --- | --- |
| Cargo.toml | Moonmark version dev.3; no dependencies added |
| Cargo.lock | Root package version only |
| native/qt/moon_style.h | Titlebar metric reduced to 40px; obsolete command-row height removed |
| native/qt/moon_style.cpp | Fusion plus neutral Accent palette; transparent labels; quiet unboxed controls; compact empty-state styles; lower-contrast 12px scrollbars; obsolete command-strip/boxed-control selectors removed |
| native/qt/moon_title_bar.h | Declares reusable native CaptionButton |
| native/qt/moon_title_bar.cpp | Paints minimize/maximize/restore/close symbols and hover/focus states; maintains accessible names |
| native/qt/moonmark_qt.cpp | Single-row shell, menu, empty state, native table/code/quote/list/placeholder rendering, structural spacing, click gesture handling, initial focus, visual/style/clipboard test hooks, isolated smoke QSettings |
| src/markdown/model.rs | Framework-neutral table-alignment enum and per-table alignments |
| src/markdown/parser.rs | Preserve Comrak column alignment |
| src/markdown/convert.rs | Transfer cell alignment in presentation commands; headless regression test |
| src/presentation.rs | 150% default body line height |
| src/settings/mod.rs | Matching authoritative settings default |
| tests/native_frontend.rs | Check code Copy and native table/style/accessibility smoke |
| fixtures/document-tables.md (new) | Synthetic chapter/path tables, alignment, inline formatting, quotes, rules, nested lists, tasks |
| tools/visual_snapshots.ps1 (new) | Repeatable eight-case native screenshot suite; restores process environment |
| README.md | Current dev.3 shell and renderer description |
| docs/ARCHITECTURE.md | Integrated titlebar actions and caption-control ownership |
| docs/UI_STYLE.md | Full dev.3 visual rules and tokens/metrics |
| docs/NATIVE_RENDERER.md | Table/quote/frame construction and click behavior; accurate limitations |
| docs/WINDOW_FRAME.md | Chrome now consists of one titlebar plus optional diagnostics |
| docs/BUILDING.md | Snapshot paths, environment controls, project-local smoke settings |
| NEXT_STEPS.md | User visual review and physical Windows validation before new features |
| docs/DEV3_VALIDATION.md (new) | This report |

build.rs, native ABI headers, image pipeline code, syntax libraries/palette, branding source assets, icon resources, dependencies, and framework choice were not changed.

Ignored outputs were generated only in project-local `target/` and `fixtures/generated/`: builds, screenshots, smoke settings, test scratch files, and deterministic stress images/Markdown. No private documents were used. No intentional source/configuration edits, installations, or global configuration changes were made outside D:\Projects\Moonmark. Ordinary tool-managed caches and OS activity were not audited. Clipboard smoke checks restore the prior MIME data.

## Visual changes and inspected results

- **Titlebar:** 40px rather than 46px plus a 42px command row. Small embedded symbol, left filename, genuinely available drag area, Open/zoom/menu, and 44×40px caption hitboxes. Caption glyphs are painted consistently instead of relying on font glyph shapes.
- **Commands:** no separate strip. Open and zoom remain visible with a document; Reload, Reset zoom, Fullscreen, and Diagnostics are in the small ellipsis menu. Existing shortcuts remain.
- **Empty state:** 32px symbol, small wordmark, short drag/drop hint and obvious Open action. No giant centered logo stack. Final screenshot confirms Open receives initial keyboard focus.
- **Canvas:** 20–48px responsive side margins, full available desktop width, no 1760px content cap. Body remains 12.75pt; 150% native line height reduces publishing-style looseness.
- **Tables:** no outer/vertical grid or cell fills. Thin horizontal separators, stronger header text, 6px vertical cell padding, 125% row line height, retained left/center/right alignment. Inline code paths have no background. Narrow tables wrap; wide tables use the desktop.
- **Code:** one borderless graphite native frame, 14px padding, neutral metadata, thin real separator, lightweight Copy link. Syntax, indentation, blank lines, and overflow remain. Screenshot review exposed excessive frame-adjacent blank paragraphs, which were corrected.
- **Quotes:** muted text and thin continuous neutral side markers; no filled text rows. Painting decorates Qt blocks without replacing their layout, selection, or accessibility.
- **Rules/lists:** light 1px rules, explicit hanging-marker tab stops, no repeated marker on continuation paragraphs, integrated task glyphs.
- **Images:** existing native loading/cache pipeline retained. Loading/error placeholders use a short left marker and readable text instead of rounded border boxes; nominal placeholder height reduced from 120 to 72px.
- **Scrollbars:** 12px tracks, subdued resting handle, brighter hover; horizontal overflow remains usable.
- **Menus/focus:** actual Qt menu with active selection and full-document selection were captured. Both are neutral grayscale. Accent/Highlight/Link/Text/Button roles are tested in active, inactive, and disabled palette groups. Native OS file dialogs remain OS-owned.
- **Icon:** existing executable resource and QApplication/window icon path preserved. Shell/empty state use that embedded icon rather than adding an external runtime dependency. Icon smoke reports seven sizes.

Actual Qt `QWidget::grab()` captures were opened and inspected, not merely generated:

| File under target/visual-dev3 | Review |
| --- | --- |
| before-tables.png | dev.2-style baseline: heavy grid, boxed inline code, filled quote rows |
| shell-first.png | First single-row shell; exposed initial focus/remaining document issues |
| tables-first.png | Horizontal-only tables and alignment confirmed |
| empty.png | Compact prompt; final initial Open focus confirmed |
| prose.png | Broad prose, emphasis, inline code, neutral link, heading hierarchy |
| headings-lists.png | H3–H6, quote markers, nested markers, tasks, rules |
| tables.png | Chapter/path table, second aligned table, quotes, lists; grid removed |
| code.png | Rust syntax, whitespace, copy/header/rule; frame spacing corrected |
| narrow.png | 720px client layout, tighter margins and wrapping |
| wide.png | 1800px client layout, available width retained |
| images.png | Generated image-heavy document and native embedded image |
| menu.png | Real menu with neutral active item |
| selection.png | Native whole-document selection across heading, prose, tables, quotes and lists |

Screenshots are ignored local review artifacts, not committed build output. This is an inspected implementation baseline, not an assertion that the user has approved its aesthetics.

## Functional and performance evidence

Rust 1.98.0 / Cargo 1.98.0, Windows x64 MSVC, Qt 6.11.2.

Native render checks retain H1–H6, prose/emphasis, code, lists/tasks, rules, tables, links/anchors, local images, and inert HTML through the existing semantic/native pipeline. No parser replacement or HTML rendering was added.

Native Ctrl+A/Ctrl+C passes across blocks. Code Copy is tested through mouse events against the exact original fenced source. The new test initially failed: Qt selected the anchor label on release, while Moonmark treated any selection as a drag. Gesture/anchor tracking fixes that conflict; drag/Shift selection is not treated as link activation.

The native style check verifies borderless cells, retained right alignment, no inline table background boxes, neutral palette roles, read-only state, and a Qt accessible-text interface. It does **not** establish complete UI Automation or screen-reader compatibility.

| Measurement | Result |
| --- | --- |
| 250-image Debug smoke | 255 discovered, 250 loaded, 5 intentional missing references, 0 decode failures, 0 pending |
| Unique decode requests | 6 for repeated PNG/JPEG/WebP/GIF/SVG/large-PNG assets |
| Reported Rust image cache | 15,360,000 bytes (14.65 MiB), not total process memory |
| Debug image completion sample | 2,655ms after all-image queueing |
| Release image completion sample | 402ms after all-image queueing |
| 10,001-block / 728,923-byte text fixture | Native render and whole-document copy passed |
| Native large-text construction | Debug 636.605ms; Release 612.354ms, single samples |
| Image fixture Rust parse p50/p95 | 0.582 / 1.005ms |
| Image fixture presentation p50/p95 | 18.080 / 20.221ms |
| Large-text Rust parse p50/p95 | 43.412 / 52.550ms |
| Large-text presentation p50/p95 | 13.379 / 19.218ms |
| Resize/zoom/maximize/F11 matrix | parse/load/construction/image-request deltas all 0 |
| Maximize/restore smoke | Normal geometry restored; all counters unchanged |
| Normal/F11/Normal smoke | Prior geometry restored; all counters unchanged |
| Maximized/F11/Maximized smoke | Returns maximized; all counters unchanged |
| Release icon smoke | Valid embedded icon, 7 available sizes |

Rust benchmarks use 20 runs; native timings are observations rather than enforced budgets. No rigorous before/after FPS, peak-RSS, or end-to-end first-readable-paint study was performed. Transition counter tests use the stable layout fixture; the separate 250-image smoke tests complete decoding, not physical rapid scrolling. Do not interpret these as a universal responsiveness guarantee.

The final executable is 6,275,584 bytes (5.985 MiB), versus the preserved dev.2 executable's 6,248,960 bytes. The final Release render/selection/Copy smoke was also run with an explicitly awaited child process and exited 0. This is an executable measurement, not a new clean-package audit. No dependency was added, and no licensing change was introduced.

## Validation commands

Run from the project root:

```powershell
cargo fmt --all --check
cargo check
cargo build
cargo test --lib table_alignment
cargo test --test native_frontend -- --nocapture
cargo test
cargo clippy --all-targets --all-features -- -D warnings
cargo build --release
cargo run -- --smoke-render fixtures/code-block-quality.md
./target/debug/generate_stress_fixture.exe
cargo run --release --bin renderer_benchmark -- fixtures/generated/image-stress.md
cargo run --release --bin renderer_benchmark -- fixtures/generated/large-text.md
pwsh -NoProfile -File tools/visual_snapshots.ps1
./target/debug/moonmark.exe --smoke-style fixtures/document-tables.md
./target/debug/moonmark.exe --smoke-render fixtures/code-block-quality.md
./target/debug/moonmark.exe --smoke-render fixtures/document-tables.md
./target/debug/moonmark.exe --smoke-render fixtures/generated/large-text.md
./target/debug/moonmark.exe --smoke-images fixtures/generated/image-stress.md
./target/debug/moonmark.exe --smoke-layout fixtures/layout-transitions.md
./target/debug/moonmark.exe --smoke-maximize fixtures/layout-transitions.md
./target/debug/moonmark.exe --smoke-layout-normal fixtures/layout-transitions.md
./target/release/moonmark.exe --smoke-images fixtures/generated/image-stress.md
./target/release/moonmark.exe --smoke-render fixtures/generated/large-text.md
./target/release/moonmark.exe --smoke-layout fixtures/layout-transitions.md
./target/release/moonmark.exe --smoke-icon
./target/release/moonmark.exe --smoke-render fixtures/code-block-quality.md
git diff --check
git fetch origin
git rev-list --left-right --count origin/main...HEAD
```

Menu and selection snapshots used the documented process environment variables. The final full test/check sequence sets TEMP/TMP to project-local target/tmp for test scratch output. No environment variable was persisted globally.

Final test totals: 20 Rust unit tests, 4 acceptance tests, 1 native integration matrix, zero failed. Formatting, checking, Clippy with warnings denied, Debug build, and Release build pass. No final Rust or C++ warnings were emitted. Git emits its existing LF-to-CRLF checkout notices.

Intermediate failures were not hidden: a snapshot lambda initially shadowed QWidget::width and failed C++ compilation (fixed with this->width); the newly added mouse Copy regression initially failed and led to the interaction fix described above. Both were rerun successfully. Several patch context mismatches made no changes and were resolved with targeted diffs.

## Remaining physical checks / limitations

- No physical/manual mouse session or external screen-reader inspection was completed. Native event tests and Qt-rendered screenshots are the evidence here.
- Windows 10/11 taskbar work area, Alt+Space/system menu details, snap behavior, actual resize cursors, mixed-DPI multi-monitor movement, touchpad interaction, and physical fullscreen restoration still need user/hardware validation.
- No Linux host build was performed. Shared Rust and Qt code remain portable; Windows-specific caption/system integration stays guarded.
- Square native code frames and viewport-wide horizontal scrolling remain. No simulated rounded document cards were introduced.
- Table copy uses native document order, not spreadsheet/TSV semantics.
- Quote decoration does not add a custom accessibility tree; Qt text semantics remain authoritative.
- Existing limitations remain: limited footnote navigation, frontmatter as inert text, ordinary quote treatment for alerts, and no new TOC UI.
- No installer/package rework, editor, Android, tabs, vault, plugins, or unrelated product features were started.
- No proprietary MarkText, Obsidian, or reference-application implementation/assets were copied. No Avalonia, Slint, Iced, CLR, or browser frontend was introduced.

The next step is the user's dev.3 visual review, followed by focused physical Windows integration checks—not another framework change.
