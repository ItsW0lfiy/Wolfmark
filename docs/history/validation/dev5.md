# 0.1.0-dev.5 — Rendering Polish & Performance

Validated on Windows x64 with Rust 1.98.0, MSVC 14.51, and project-local Qt 6.11.2. The approved dev.4 shell, sidebar, captions, document width, heading hierarchy, table treatment, and architecture were retained. No dependency was added.

## Repository baseline

Starting HEAD and fetched origin/main: `1f49f29944d0f7a2579226b5e57821799ae8581c`. The working tree was clean on `main`. A finishing fetch found no new remote commits. All work stayed on main; no history was rewritten and nothing was pushed.

Logical implementation commits:

- `4ea4abe` — separate dev.5 version bump.
- `3f2b9fe` — failing image-gap reproduction and separate interactive zoom timing.
- `8a62406` — image-only line-height fix and native regression entry point.
- `c558b3c` — inline-code decoration, fixture, and visual cases.
- `495b027` — zoom/repaint and image worker/delivery profiling.
- `22d5be3` — scaled-format reuse and batched native layout updates.
- `0d663fa` — exact inline copy, resize/reload image geometry, and fail-fast profiling script.

This report and the accompanying documentation changes are committed separately after validation. Use `git log` for that final documentation commit's ID.

## Inline code

Inline code stays ordinary QTextDocument text with the existing monospace character format and graphite background. A marked character-format property lets the viewport extend the surface around each native text-line segment. Qt supplies line geometry and cursor positions; the added edge has approximately 2.5 logical pixels of horizontal breathing room, 1 pixel vertical extension, and restrained rounded corners. It never paints over the native glyph rectangle.

No spaces, invisible padding characters, raster text objects, or replacement text engine were introduced. Selected spans defer to Qt's native selection paint, including partial selections. Trailing wrapping whitespace is excluded from decoration. The fixture exercises multiple spans, wrapping, prose, tables, lists, and quotes. Ctrl+C was checked for all 17 inline spans in the fixture, with exact equality to their underlying native text. Whole-document Ctrl+A/C and fenced-code Copy also pass.

Limitation: this is decorative padding, not a true inline box model. It does not reserve additional advance width around tightly adjacent punctuation. Native wrapping, selection, accessibility, and table behavior take priority over perfect box geometry. Selected code uses the ordinary selection surface rather than decorative rounding.

## Image whitespace: reproduced cause and fix

The standalone-image builder used proportional line height 125%. Qt applied the extra 25% to the bitmap height. The image block's bounding rectangle itself looked correct, but the next block started too far below it. Comparing only block height to image height would miss this bug.

The synthetic fixture reproduced 115.8 pixels of unexplained leading after a 463-pixel image, and 18 pixels after a 72-pixel placeholder. The same discrepancy appeared before a heading, rule/heading, and paragraph. The corrected geometry test failed with exit 6 before the fix.

Standalone image blocks now use 100% line height, retaining their explicit 6/13 margins. No negative margin or document-specific adjustment was used. The final test reports zero excess after subtracting declared block margins, within a 2-pixel tolerance. It covers initial placeholders, loaded images, a narrow/wide resize, explicit reload, and 80/100/125/150% zoom. The private source document was not accessed; the provided screenshot's layout failure was reproduced using generated public fixture imagery.

## Performance method

Release executable, same fixtures and machine, separate processes, no build running during the retained measurements. These are representative observations, not CI time limits or statistically controlled hardware benchmarks. Logs are local ignored files: `target/dev5-profile-before.txt`, `target/dev5-profile-after.txt`, and `target/dev5-final-performance.txt`.

`interactive_us` includes the zoom action, format updates, and immediate native layout work. `painted_us` adds a synchronous viewport repaint. `elapsed_us` additionally includes exhaustive format/selection/plain-text verification. The latter can take roughly one second on 10,001 blocks and is not the normal zoom path. The no-op 100% check alone spent about 450–510ms in verification. No expensive verification was moved into normal interaction.

### Interactive zoom, milliseconds

| Transition | Normal before | Normal final | 10,001 blocks before | 10,001 blocks final |
| --- | ---: | ---: | ---: | ---: |
| 80 → 100 | 4.04 | 2.35 | 148.58 | 120.23 |
| 100 → 125 | 18.65 | 13.24 | 145.45 | 127.51 |
| 125 → 150 | 18.05 | 15.80 | 159.10 | 121.82 |
| 150 → 100 | 6.51 | 1.52 | 156.98 | 117.80 |

Including viewport repaint, the large-document before/final times in the same order were 156.56/126.27, 158.24/141.84, 169.04/132.17, and 165.15/125.92ms. Another optimized run measured 113.63–118.89ms for these interactive transitions. Synchronous work is reduced, not eliminated.

The main large-text cost remains walking and applying native formats. Dev.5 computes each distinct scaled format once per zoom instead of repeating property transformations for every occurrence. Qt layout is suspended while applying text, frame/table, width, and image geometry changes, then enabled once. Existing explicit font sizes and hierarchy remain authoritative; default-font-only Qt zoom is not a valid replacement for this document model.

All tested zoom transitions report parses +0, file loads +0, document constructions +0, and image requests +0. Text and selection are retained; native format checks pass. No source/presentation regeneration or higher-resolution decode policy was introduced.

### Image-heavy document

255 occurrences: 250 valid, five intentionally missing; six underlying assets, six requests; zero failures/pending results; cache cost 15,360,000 bytes (14.65 MiB), within the unchanged 128 MiB budget.

Before optimization, two Qt delivery batches totaled 51.81ms, including 45.09ms of image geometry work and 5.69ms of QImage copying. The first optimized run totaled 16.02ms; the final run totaled 20.61ms, including 3.84ms of geometry and 7.79ms copying. Available results are applied with layout batching, and unchanged dimensions skip format writes. Buffers, canonical deduplication, four-worker pool, generation cancellation, paths, and remote-image policy are unchanged.

Image-heavy interactive zoom improved from 157.49/62.93/51.44/138.34ms to 34.09/12.93/7.94/25.91ms for 100→125, 125→150, 150→100, and 80→100 respectively.

**Total initial completion did not show a reliable improvement:** 341ms before, 397ms and 416ms after. The SVG/large-raster worker times varied from approximately 310/314ms to 385/397ms; other workers took 1–6ms, and queue waits stayed below 3ms. Completion is measured from the smoke's queue-all phase until no decodes remain, excluding subsequent zoom verification. Decoder variation dominates that figure. The measured UI-thread saving is real in these samples; claiming an overall loading speedup would not be justified. No decoder/output-quality changes were made speculatively.

### Headless benchmark

Unchanged Rust semantic/presentation benchmark, 20 runs:

- 728,923-byte / 10,001-block text: parse p50/p95 67.298/80.493ms; presentation 18.286/29.654ms.
- 17,885-byte / 382-block image fixture: parse 0.701/1.151ms; presentation 27.937/32.407ms.

These are current-machine observations, not evidence of a semantic-parser optimization; that code was not changed.

## Visual and automated validation

Passed:

- `cargo fmt --all --check`
- `cargo check`
- `cargo test`: 20 unit tests, four acceptance tests, one expanded native smoke matrix; no failed tests.
- `cargo clippy --all-targets --all-features -- -D warnings`
- `cargo build --release`
- `cargo run -- --smoke-render fixtures/inline-code-quality.md`
- `pwsh -File tools/render_performance.ps1` with `MOONMARK_PROFILE=1`
- `pwsh -File tools/visual_snapshots.ps1`: all 21 captures generated and inspected.
- `pwsh -File scripts/package_windows.ps1`
- Packaged executable render/code Copy, style/palette/accessibility, image geometry/reload/zoom, and layout/window-counter smokes with PATH restricted to Windows system directories and Qt discovery environment removed.
- `dumpbin /dependents deploy/Moonmark/Moonmark.exe`
- `git diff --check`

Final builds produced no Rust or C++ /W4 warnings. Git prints normal LF-to-CRLF normalization notices. During investigation, one early unregistered smoke mode was terminated and corrected; one Release rebuild hit Windows executable locking because a smoke still ran. The build was rerun successfully after it exited. Those failed/stale runs were not used as final validation or retained performance comparisons.

Snapshots are ignored under `target/visual-dev5`: inline-prose-table, inline-wrap, inline-selected, image-gap-after, empty, prose, headings-lists, tables, code, narrow, wide, images, concept, sidebar-hidden, menu, selection, zoom-80/100/125/150, and code-150. An additional packaged Release snapshot, `release-image-gap.png`, was inspected against `target/visual-dev4/dev5-gap-before.png`: the excessive image-to-heading gap is gone, and the second image now reaches its rule/heading with ordinary spacing.

Review found the same broad grayscale dev.4 shell, table columns/separators, code frames, header/sidebar, and zoom hierarchy. Inline paths remain compact inside tables; wrapped segments no longer leave a trailing empty outline. Selected prose/code/tables retain neutral native highlighting. No blue theme values or dependencies were added. Existing document-wide horizontal scrolling for wide code/tables remains unchanged.

Physical mixed-DPI/multi-monitor, taskbar/snap behavior, real screen-reader output, and manual drag-selection on diverse content remain unverified in this environment. Automated window-state, native text accessibility interface, keyboard copy, and selection checks are not substitutes for those physical checks. Linux was not built in this Windows-only round.

## Package

- Moonmark.exe: 6,352,896 bytes (6.06 MiB).
- Portable folder: 34,950,563 bytes (33.33 MiB).
- ZIP: 15,500,508 bytes (14.78 MiB).
- App-local dependencies remain Qt Core/Gui/Widgets, qwindows, and MSVC runtime libraries. Imports show native Qt/MSVC/Windows dependencies, not CLR/hostfxr or browser runtimes. No new runtime or framework was introduced.

The generated dev.4 deployment folder/ZIP were replaced by reproducible dev.5 artifacts. No source assets were deleted. A clean VM installation test was not performed.

## Complete tracked file changes

- `Cargo.toml`, `Cargo.lock`: version only.
- `native/qt/moonmark_qt.cpp`: inline decoration/copy probes, image leading fix, geometry regression, batching/unchanged-dimension guard, phase timing, dev.5 screenshot path.
- `native/qt/document_zoom.cpp`, `native/qt/document_zoom.h`: baseline format IDs, per-zoom scaled-format reuse, timing.
- `src/images/loader.rs`: opt-in worker timing only; decode policy unchanged.
- `tests/native_frontend.rs`: image geometry and inline-code smoke coverage.
- New `fixtures/image-layout-regression.md`, `fixtures/inline-code-quality.md`.
- New `tools/render_performance.ps1`; `tools/visual_snapshots.ps1` adds four affected cases.
- `README.md`, `NEXT_STEPS.md`, `docs/BUILDING.md`, `docs/NATIVE_RENDERER.md`, `docs/IMAGE_PIPELINE.md`, and this new report: current milestone, behavior, measurement instructions, limitations, validation.

No tracked files were deleted. No production framework, runtime, branding asset, sidebar/window implementation, parser, or file policy was replaced. No outside-project configuration or software was modified.
