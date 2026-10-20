# Document experience and editing validation

Implementation and validation: 20–21 October 2026, Windows x64. Application
version remains **0.1.0-dev.7**. This is a development milestone with outstanding
physical acceptance, not a production-readiness declaration.

## 1. Architecture

Each retained document session owns its canonical file identity, authoritative
native source document and undo stack, Reader view, Rust backend/image pipeline,
file fingerprint, watcher, mode, zoom, cursor/selection, outline and revisions.
Source and Live Preview use the same `QPlainTextEdit`/`QTextDocument`; switching
between them changes presentation, not content. Reader derives its semantic and
presentation models from the current source, including unsaved edits.

Rust/Comrak, C++20/Qt Widgets and the framework-neutral presentation model remain
in place. The narrow in-process C ABI is version 4, with memory-source rendering
and source decorations. No dependencies, application runtimes, framework,
package manager or licensing obligations were added. See
[document editing](../architecture/document-editing.md) for ownership and safety
details.

## 2. Scrolling root cause

The demonstrated reset came from `DocumentView::load()`: an unconditional
zero-delay callback wrote the scrollbar to zero. Reload restored its reading
anchor synchronously, then the older callback overwrote that position. The
regression reproduced **before=58, restored=58, after=0**. This proves a delayed
reload/layout reset; it does not establish that every possible physical
free-spin failure had the same cause.

Inspection also found unsafe large wheel-destination conversions and smooth
scroll trajectories that could retain obsolete limits after relayout.

## 3. Scrolling and position fixes

Deferred initialization now schedules resources without writing the scroll
position. The original regression retains **58 → 58 → 58**. Wheel destinations
are bounded before integer conversion, finite values are checked, and smooth
positions/targets respect current limits. Motion is rebased after resize/zoom;
there is no artificial wheel-speed cap.

Reader mode jumps use the existing generation-aware semantic navigation path,
including settling after provisional native layout. New user input cancels
pending navigation. Reload captures offsets before document replacement, then
matches nearby source context and the nearest occurrence. A later-chapter
duplicate-paragraph regression prevents selecting the first identical paragraph.
Literal text maps through native line numbers. Source anchoring uses native
visual-line units rather than Reader pixel units.

Automated checks cover 200-event bursts, extreme signed wheel values, pixel
input, reversal, image-heavy content, resize/zoom, and background session reload.
The physical Logitech free-spin reproduction still requires user verification.

## 4. Source Editor

Source is a real native editable buffer with line numbers, Markdown highlighting,
selection/copy, plain-text paste, Unicode, tabs/indentation, word-wrap preference,
Find/next/previous, bold/italic source toggles, native undo/redo and actual font
zoom. Source clipboard data retains nonbreaking spaces and emoji without
rich-text conversion. Ctrl+S, Ctrl+Shift+S, Ctrl+F, Ctrl+W, Ctrl+B/Ctrl+I and
Ctrl+Y/Ctrl+Shift+Z are integrated with the relevant native actions.

Editing accepts valid UTF-8, with or without BOM. Invalid UTF-8, NUL and literal
U+2029 paragraph separators are rejected explicitly before replacing content.
UTF-16 and other encodings are not supported. There is no replace-all, advanced
editor command palette, compare view or multi-cursor editing.

## 5. Live Preview

The same source document provides editable heading, emphasis, strike, link and
code styling. Safe heading/inline delimiters become visually minimal outside the
cursor/selection region and reappear inside it. UTF-16 source positions remain
authoritative; no HTML/rich-text serialization reconstructs Markdown.

Comrak analyzes original input with the Reader extension options. Analysis is
debounced 180 ms, bounded to two workers and one pending analysis per editor,
and revision-checked before delivery. Workers own input/cancellation state, not
GUI pointers. Cached decorations belong to retained native blocks, so inserting
above a block does not invalidate all subsequent formatting.

Complex constructs deliberately retain editable syntax: tables, task/list/quote
markers, fences, reference links, images, frontmatter and inert raw HTML. This
does **not** implement rendered table editing, clickable task checkboxes,
embedded image editing or full Obsidian behavior. Comrak analysis still parses
the full source after idle time; only typing and decoration application are
incremental.

## 6. Sessions and mode switching

Newly opened files start in Reader. Reopening an existing canonical path activates
its session without reload. Tabs retain independent modes, content, dirty state,
zoom, selection, reading position, outline and backend/watchers. Source and Live
Preview preserve exact editing selections and shared undo history. Reader round
trips retain the editor cursor when the reading location has not moved.

Reader mapping uses top-level source locations, so positions inside very large
containers can be approximate. Session retention is in-process; cross-launch
tab restoration, autosave and crash recovery are not implemented. Sessions hold
their documents until closed, so aggregate memory rises with open large files.

## 7. Native UI

The main interface has real document tabs with close controls and dirty markers,
Save/Save As/Find actions, Reader/Live Preview/Source controls, line/column and
saved/dirty/external-change status. Settings remains discoverable from the
titlebar, including the empty state. The existing settings owner/schema migration
adds Reader & Editor wrapping preferences while retaining earlier values.

Actual Qt renders were reviewed for empty/prose/headings/tables/code/images,
narrow/wide layouts, sidebar, menu, selection, and 80/100/125/150% zoom, plus
Source and Live Preview at multiple widths/zoom levels. Neutral/crimson styling
is retained. Some existing narrow table header columns can wrap short labels
awkwardly; this change did not redesign table sizing. Screenshots do not prove
physical DPI, taskbar, touchpad or screen-reader behavior.

## 8. File safety and history

No-op saves and undo round trips preserve UTF-8 BOM, LF/CRLF/CR (including mixed
endings), trailing whitespace, nonbreaking spaces and final-newline presence.
Line-ending metadata participates in the native undo stack. Save uses
`QSaveFile` with direct-write fallback disabled and SHA-256 checks before staging
and immediately before commit. Failures preserve dirty content.

File/directory watchers coalesce changes; fingerprints identify self-saves.
Clean external changes reload only the affected session. Dirty conflicts retain
edits and offer recovery rather than overwriting. Missing/moved files and decode
failures retain source. Save As updates canonical identity/resource context and
refuses paths owned by another tab. Dirty close/window close/updater handoff
offers Save/Discard/Cancel; explicit dirty Reload requires a discard decision.

There remains a small external-writer race between the final hash check and
atomic rename: this is not an OS-level compare-and-swap lock. Files are not
locked throughout viewing. Native undo history is shared across modes and
retained after save; explicit file reload replaces that history.

## 9. Performance

Release measurements below used the packaged executable on the normal desktop,
without a concurrent build. They are single-run probes, not statistical
benchmarks or physical keystroke latency measurements. Each insertion measurement
contains 64 native insertions after initial analysis completed. Memory is sampled
at 100 ms and can miss shorter peaks.

| Input | Source, 64 inserts | Live, 64 inserts | Changed-buffer Reader refresh | Sampled peak private memory |
| --- | ---: | ---: | ---: | ---: |
| Markdown, 728,923 bytes / 20,003 blocks | 9.273 ms | 156.577 ms | 3.749 s | 314.4 MiB |
| Literal text, 670,000 bytes / 10,001 blocks | 16.982 ms | 22.708 ms | 0.915 s | 155.8 MiB |
| Markdown, 3,420,482 bytes / 20,201 blocks | 31.319 ms | 585.128 ms | 7.108 s | 648.6 MiB |

All three reported stable anchors and unchanged Reader parse/load/construction
counters during Source/Live operations. Six editing-mode switches took 987.832,
10.537 and 834.462 ms respectively. Large-Markdown Comrak analysis took
338.975 ms. Local insertion into the tested plain block required zero extra
decorated blocks; this is not a claim that styled edits require no formatting.

Reader document construction now batches native layout. A separate paired Release
probe measured Reader refresh before/after batching at 3.669/3.218 s (small
Markdown), 2.591/0.896 s (text), and 9.024/6.492 s (large Markdown). Machine load
and run variance apply. The final normal-desktop results above are the relevant
shipping measurements, not the faster values from another run.

**Full changed-buffer Reader refresh remains synchronous and costly.** Large
files retain substantial memory. No long-session leak result or claim of better
performance than VS Code is established. Repeat with
`pwsh -File tools/editor_performance.ps1 -Executable <Wolfmark.exe>`.
Local evidence is in `out/desktop-editor-performance.log` and the paired
`out/editor-before-layout-performance.log` / `out/editor-after-layout-performance.log`.

The final packaged rendering matrix passed nine cases. Reader zoom round trips
reported zero parse/load/construction/image-request deltas and retained native
selection. Large-text zoom actions took about 112–287 ms, including painted
times up to 297 ms; exhaustive verification adds further time. The image probe
loaded 250 occurrences with six canonical requests, zero failures and 15,360,000
cache bytes in 247 ms (excluding its subsequent zoom checks). Rapid-scroll
paint-interval p95 was 16.59 ms for large text, 16.34 ms for the 250-image fixture
and 16.58 ms for mixed features; worst observed intervals were 26.99, 33.26 and
47.68 ms. These synthetic profiles do not measure Logitech hardware input.
Evidence: `out/desktop-render-performance.log`.

## 10. Markdown and platform compatibility

Reader retains the existing Comrak semantic/native presentation pipeline:
headings/outline targets, emphasis/strike, inline/fenced code and Copy, quotes,
nested/task lists, rules, GFM tables/alignment, links, escaping/entities, images
and inert raw HTML. Existing frontmatter/footnote/alert limitations remain.
Raw source is not passed through Reader path normalization before editing.
`.txt` bypasses Comrak. Explicit local path handling, bounded image workers/cache,
generation cancellation and remote-image disabling remain in place.

Native selection, accessibility interfaces, window controls and fullscreen paths
are preserved and covered programmatically where supported. Linux compilation,
IME and physical Windows integration still need their own acceptance; no Android
frontend or alternative framework was added.

## 11. Commands and results

| Command/check | Final result |
| --- | --- |
| `cargo fmt --all --check` | Passed |
| `cargo check --workspace` | Passed |
| `cargo test --workspace -- --test-threads=1` | **64 passed, 0 failed**: 38 library, 5 acceptance, 13 native, 8 maintenance; remaining targets/doc-tests had zero tests |
| `cargo clippy --workspace --all-targets --all-features -- -D warnings` | Passed |
| `cargo build --release` | Passed |
| `cargo build-app` | Passed |
| `cargo package-app` | Passed on normal desktop |
| `pwsh -File scripts/test_windows_setup.ps1` | Passed artifact/checksum/manifest/runtime/portable/startup checks; no lifecycle flag |
| Staged and extracted-portable `--smoke-editor-core fixtures/layout-transitions.md` | Both passed; exact source copy, round trip, undo, contextual markers and conflict checks |
| `pwsh -File tools/editor_performance.ps1 -Executable <staged exe>` | All 3 inputs passed |
| `pwsh -File tools/render_performance.ps1 -Executable <staged exe>` | All 9 geometry/zoom/editor/image/scroll cases passed, each exit 0 |
| `pwsh -File tools/visual_snapshots.ps1` and additional mode snapshots | Passed capture matrix; actual native renders inspected |
| `git diff --check` | Passed |

No final Rust or C++ /W4 warnings were emitted. `CARGO_INCREMENTAL=0` was
process-local during validation. Full desktop tests used project-local TEMP/TMP.
Tests exercise byte preservation, unsupported codecs, failed/read-only/conflicting
saves, undo across modes, native accessibility text, watcher isolation, session
close/cancellation and 30 editors destroyed with pending analysis.

Failure history is retained rather than treated as success: the original reset
test failed before its fix; a large mode-position probe failed before semantic
navigation settling was corrected. An earlier concurrent outline reflow test
failed and one editor-session probe hung; later isolated/serial runs and the
final 64-test run passed, with a bounded session-probe watchdog added. Sandbox
clipboard/HKCU/32-bit NSIS restrictions prevented meaningful runs there; normal
desktop validation passed. A setup test selected an older bootstrap log by
timestamp; the harness now checks the launched PID's log, and passed on rerun.
Installer product code was not changed for that test issue.

## 12. Packaging

Both artifacts were built from clean commit
`5a5d195080591729b7ebe281ad3d844eca9ba113`, build ID `5a5d19508059`.
Later commits change the test harness and documentation only. The payload
manifest records `dirty=false`, x86_64, development channel, 20 payload files.

| Artifact under `out/release/0.1.0-dev.7/` | Bytes | SHA-256 |
| --- | ---: | --- |
| `Wolfmark-Setup-win-x64.exe` | 24,766,179 | `a1ba9bc0f68fdbb1bd423d03a2ec6b1e6201163c5a428ce32ac00a8a2272e6c1` |
| `Wolfmark-portable-win-x64.zip` | 16,725,834 | `a29375692809d516d4210515958fd443146216cc88cbb36f33be65761084fd4f` |

The staged installed payload and extracted portable executable have identical
SHA-256 `1e1966f62fbcfcd54f6dc442f4329603913d0343cc715dd1a6334e248644d7bb`.
Both ran the editor core probe. Portable launch with a system-only PATH, payload
runtime contracts, setup extraction/argument/exit propagation and native setup
action contracts passed. No installed user application was replaced. A real
clean-VM install/update/uninstall lifecycle was **not** executed.

## 13. Local commits

| Commit | Change |
| --- | --- |
| `396ced4` | fix: preserve restored viewport after document reload and extreme wheel input |
| `dd739a8` | feat: render session source and expose Comrak editor source decorations |
| `74bac4a` | feat: add shared native source and contextual Live Preview editing sessions |
| `561f4ee` | fix: synchronize editor zoom status and harden native editor probes |
| `093f633` | fix: retain source locations and incremental decorations through reflow |
| `50ef939` | docs: document native editing and add repeatable large-file probes |
| `5a5d195` | perf: batch native layout and preserve literal source clipboard data |
| `cc81cc6` | test: bind setup startup log checks to launched process ids |

This report is committed separately after the above changes.

## 14. Git state and changed files

Work used `main` throughout, starting at
`3b026e65f19e3c4179234bbe5e155717bdb2a6dd`. Fetched remote `main` remained at
that starting commit; no incoming user changes needed integration. Local `main`
advanced through the focused commits above and this report. The final handoff
records its exact HEAD and ahead/behind count. The checkout remains on `main`;
no task branch, push, tag, GitHub Release or version bump was created.

Changed implementation files:

- `build.rs`; `src/app/state.rs`; `src/native_api.rs`; `src/presentation.rs`.
- `src/markdown/{mod,model,parser,convert,editor}.rs`.
- `native/qt/document_editor.{h,cpp}`; `native/qt/wolfmark_qt.cpp`.
- `native/qt/document_sidebar.cpp`; `native/qt/smooth_scroll_controller.{h,cpp}`.
- `native/qt/wolfmark_api.h`; `native/qt/wolfmark_settings.{h,cpp}`;
  `native/qt/wolf_style.cpp`.
- `tests/native_frontend.rs`; `tools/editor_performance.ps1`;
  `tools/render_performance.ps1`; `scripts/test_setup_startup.ps1`.

Changed documentation: `README.md`, `ROADMAP.md`, `docs/README.md`,
`docs/architecture/{overview,document-editing}.md`, `docs/design/ui-style.md`,
`docs/development/{building,document-editing-validation}.md`, and
`docs/releases/0.1.0-dev.7.md`. Generated probes, logs, screenshots and packages
remain in existing ignored output locations. Local agent instructions remain
untracked and excluded.

## 15. Known limitations

- Complex Live Preview structures remain editable source, not rich interactive
  tables/tasks/images. Contextual delimiter styling is not full Obsidian parity.
- Changed-buffer Reader refresh is full and synchronous; measured large-file
  pauses and memory usage remain material. Idle analysis is a full Comrak parse.
- Positions inside large semantic containers are approximate; there is no
  cross-launch session/undo restoration, autosave or crash recovery.
- UTF-8 only, with explicit unsupported-content rejection; no replace-all,
  multi-cursor or conflict comparison/merge UI.
- External file conflict detection cannot eliminate the final check/rename race.
- Programmatic accessibility and window checks do not replace assistive-client,
  hardware, DPI, IME, long-session or clean-install acceptance.

## 16. Physical acceptance still required

Automated coverage above supports these checks but does not mark them complete.
Use ordinary user-owned test copies, including large Markdown and images.

- [ ] Open large Markdown; confirm Reader is the initial mode.
- [ ] Use the actual Logitech free-spin wheel extremely rapidly.
- [ ] Confirm no unexpected Chapter 1 reset, including after reload/image delivery.
- [ ] Switch Reader → Source near the middle/end and assess location.
- [ ] Edit, save and reopen; compare source bytes and expected changes.
- [ ] Switch Source → Live Preview and retain content/cursor.
- [ ] Edit formatted Markdown directly; assess contextual syntax comfort.
- [ ] Switch Live Preview → Source and verify source correctness.
- [ ] Undo/redo across repeated mode changes and saves.
- [ ] Open multiple tabs; retain independent position/content/selection.
- [ ] Set different modes per tab and revisit each.
- [ ] Close dirty tab/window; exercise Save, Discard and Cancel.
- [ ] Change/move/delete files externally; verify safe reload/conflict/recovery.
- [ ] Scroll quickly while images load, including repeated image occurrences.
- [ ] Resize and zoom 80/100/125/150%, round trip and assess stability.
- [ ] Open Settings from both empty and document interfaces.
- [ ] Change wrapping/settings; close/reopen and check persistence.
- [ ] Test keyboard focus/shortcuts, IME, touchpad, real screen reader, window
      controls/fullscreen and physical DPI/multi-monitor/taskbar behavior.
- [ ] Test actual installed and portable distributions; run clean-VM
      install/update/uninstall acceptance separately from payload smoke.
- [ ] Run an extended multi-document read/edit session and observe memory after
      repeated mode changes, image loads, closes and reopens.
