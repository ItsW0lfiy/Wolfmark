# Wolfmark 0.2.0-dev.1 validation

Recorded 2026-11-18. This is a development milestone, not stable 0.2.0 acceptance.

## Starting state and changes

The checkout began clean on local main `3bff29f`. Fetching found 15 newer user
commits; the task branch `codex/wolfmark-0.2.0` fast-forwarded to remote main
`9ea8d7205d24b5f7d7800e8861794dbc55fa4c73` before implementation. All were retained.
Historical Servo work was preserved and was not continued.

Baseline library tests had 37 passes and one file-URI failure: percent-decoding
before URL parsing turned an encoded `#` into a fragment and truncated the path.
The baseline native editor copy test also failed on this desktop.

The implementation retains Rust/Comrak, the presentation model, C++20 Qt Widgets,
native Reader/Source/Live Preview and Cargo. Xmake delegates to those existing
builders. Qt Print Support is the only new Qt runtime module. DOCX/ODT package
writing adds the MIT-licensed `zip` crate; no Office, Pandoc or online export
service is required.

Changed implementation areas:

- `src/images/paths.rs`: parse file URIs before decoding reserved characters.
- `native/qt/wolfmark_qt.cpp`, `wolfmark_settings.{h,cpp}`: global Open action,
  editor drop routing, explicit errors, failed-session cleanup, canonical recent
  documents, multi-file picker, CLI `--`, output actions and native regressions.
- `native/qt/document_output.{h,cpp}`, `page_preview.{h,cpp}`,
  `document_properties.h`, `document_zoom.{h,cpp}`: physical page layout,
  visible-page painting, page/style dialog, shared PDF/print painter and editable
  package writers. Font selectors were resized after actual visual review.
- `src/archive.rs`, `src/native_api.rs`, `src/lib.rs`, `wolfmark_api.h`: bounded
  in-memory ZIP packaging through the existing ownership-safe C ABI.
- `installer/maintenance/src/{integration,engine,main}.rs` and setup UI/model/
  controller/contracts: disable handler registration and selectively clean
  confirmed legacy entries while protecting other applications and defaults.
- Cargo manifests/lock, `build.rs`, `xmake.lua`, SDK/package scripts: dev version,
  runtime staging, commands, consistent Windows version resources and build
  identity refresh after Git commits.
- `tests/native_frontend.rs`, document-schema scripts and Linux CI: opening,
  export, overwrite, file-lifetime and independent package validation.
- README, FEATURES, ROADMAP, NEXT_STEPS, development/release/distribution and
  licensing documents: current scope, commands, actual status and notices.

## Commands and measured results

Windows uses the existing MSVC toolchain and local Qt 6.11.2 SDK. Linux uses the
existing Debian WSL environment with Qt 6.8.2 and a separate Cargo target folder.
Logs and generated test artifacts are ignored under `out/tests`.

| Check | Result and evidence |
|---|---|
| `xmake f -m debug`; `xmake` | Passed; `xmake-debug-dev020.log` |
| `xmake run wolfmark --smoke-startup-arguments fixtures/text/literal.txt` | Passed; actual arguments arrived, one literal document, zero rejected; `xmake-run-dev020.log` |
| `xmake check` | Passed: `cargo fmt --all --check`, workspace `cargo check --locked`, workspace `cargo clippy --all-targets --all-features --locked -- -D warnings`; `xmake-final-check-dev020.log` and `xmake-layout-check-dev020.log` |
| `xmake test` | 40 library, 5 acceptance, 18/19 native and 9 maintenance tests passed. One native clipboard test failed; see below. `xmake-final-test-dev020.log` |
| Focused mode/opening/export-dialog regressions | Passed after supported Qt activation and isolated test destinations; `modes-activation-dev020.log`, `layout-dialog-final-dev020.log` |
| Native editor copy with Qt offscreen clipboard | Passed exact source/Unicode/NBSP preservation; `editor-copy-final-offscreen-dev020.log` |
| `xmake f -m release`; `xmake` | Passed underlying `cargo build --locked --release`; `xmake-release-dev020.log` |
| `xmake clean` with isolated `CARGO_TARGET_DIR` | Removed the task-created probe: 2 files, 380 bytes. Real build caches retained. `xmake-clean-dev020.log` |
| Debian Cargo check/build and library tests | Passed 40/40; `linux-final-dev020.log`, `linux-test-dev020.log` |
| Debian native opening/export/overwrite | Passed under Qt offscreen; `linux-final-dev020.log`, `linux-font-export-dev020.log`, `linux-final-clean-dev020.log` |
| `scripts/test_setup_ui_contract.ps1` | Passed native fresh/stale update, chosen install target, legacy association flags, current shortcuts, and recording-core dispatch. No real install. `setup-contract-dev020.log` |
| `scripts/fetch_document_schemas.py` | Downloaded official ECMA-376 Transitional and ODF 1.3 schemas to ignored local storage |
| `scripts/validate_document_exports.py --schemas out/tests/schema-download-check` | Windows and font-equipped Linux outputs passed all WordprocessingML/ODF XML schemas, ZIP/XML/HTML checks and independent PDF checks |
| `git diff --check` | Passed after removing trailing whitespace in changed documentation |

The Windows clipboard failure is explicit:
`EDITOR_CLIPBOARD_NATIVE open=0 error=5 owns=0 selection=1 selected_units=58`.
`OpenClipboard` returns access denied in this agent desktop; the selection has
all 58 source units but clipboard retrieval returns zero. Running outside the
restricted filesystem sandbox does not fix it. The same copy regression passes
with Qt's offscreen clipboard. The real-desktop test was neither skipped nor
weakened; a successful normal-user desktop rerun remains a release gate.

The full suite initially also exposed a smoke-test activation assumption. Using
Qt's supported `activateWindow()` and processing activation events made the
mode/focus regression deterministic; the next full suite passed it. No editor
behavior was bypassed.

## Opening and source ownership

Regressions exercise real Ctrl+O keyboard delivery to the focused editor, actual
picker acceptance of a new `.markdown` file, file drops onto Source mode,
canonical duplicate activation without another parse, persisted recent paths,
Unicode/spaces/`#`, mixed startup paths and literal text. Separate Windows tests
cover C-drive temporary files from this D-drive project, externally locked files,
missing/unsupported/invalid UTF-8 files and dash-prefixed names after `--`.

The opening smoke renames an open file, renames its parent directory, deletes the
file and verifies retained content. Export tests compare original source bytes,
dirty state and selection. Isolated registry tests use disposable HKCU subtrees,
protect foreign entries including reused Wolfmark names, and preserve UserChoice
and hash values. No live default association was changed during this task.

## Output and native visual review

The five-page visual fixture passed independent PDF page-count/dimension, Unicode
text, links and embedded-font checks. Custom 210 x 150 mm landscape output and
the shared native printer path at 600 DPI also passed. All five PDF pages were
rendered with Poppler and visually inspected: headings, lists/tasks, native
tables, syntax-coloured code, local images/placeholders, header/footer and page
numbers remain present. The 600-DPI printer PDF was compared with direct PDF.

DOCX opened through `python-docx` as editable paragraphs/headings, one table and
four images; all WordprocessingML parts passed the official schemas. All ODT
content/styles/metadata/manifest parts passed ODF 1.3 schemas. HTML embeds local
images as data URIs; unsafe link schemes remain readable inert text. Raw input
HTML remains inert through the normal renderer.

The actual native screenshot matrix ran with `tools/visual_snapshots.ps1`:
empty, prose/headings/lists, tables, code, images, literal text, narrow/wide,
sidebar hidden, menu, selection and 80/100/125/150% Reader zoom. Source and Live
Preview were additionally captured. New Page/Style/Details/Export tabs and
single-page 80/100/125/150% layouts were captured; the font-control overflow found
in the first review was fixed and re-reviewed. Artifacts are under
`out/visual/dev7` and `out/tests/page-view-*.png` (the existing snapshot tool's
historical directory name was retained).

The page canvas paints only visible pages and does not cache a bitmap/picture
per page. Native text selection and document accessibility remain in Reader;
the separate print-layout canvas does not itself implement text selection.

Debian's original default fonts lacked the CJK glyph used by the fixture. The
initial PDF Unicode check correctly failed. Rechecking with an isolated local
fontconfig using already installed Windows fonts passed. No system font was
installed or global configuration changed. Linux CI now installs Noto CJK.
PDF extraction checks normalize ligatures and whitespace across platform fonts.

## Performance

Release-mode export smoke includes all five formats, a 600-DPI printer PDF,
custom landscape PDF, visible-page painting, paper sizes and five zoom/layout
round trips. These are whole-run measurements, not single-format timings.

| Fixture | Pages | Whole run | Memory |
|---|---:|---:|---|
| 10,000 paragraph blocks | 359 | 16.558 seconds | Not measured reliably in this run |
| 250 loaded image occurrences plus missing/blocked cases | 127 | 6.188 seconds | 208,728,064-byte sampled peak working set (about 199 MiB) |

Both passed unchanged source bytes, selection, parse/load/image-request counters
and native-document construction counts during page changes and zoom. Image
stress discovered 260 occurrences, loaded 250, and retained failed placeholders.
The existing native render/zoom/layout/scroll/watcher matrix also passed in the
full suite. Measurements are local observations, not cross-hardware guarantees.

## Remaining acceptance and limitations

- Stable 0.2.0 is not prepared or published. Keep `0.2.0-dev.1`.
- Physical Windows clipboard, mixed DPI/monitor/taskbar behavior, screen reader,
  physical printers/page ranges and user installer lifecycle still need manual
  acceptance. Screenshots and synthetic native checks do not establish those.
- Independent Word/LibreOffice rendered DOCX/ODT review remains incomplete.
  The bundled DOCX rendering tool was attempted but `soffice.exe` is absent;
  computer-use access to Word was denied. Schema validity and `python-docx`
  loading do not replace that review.
- Editable formats can reflow in other applications. DOCX has native list
  numbering; ODT currently preserves list markers with hanging paragraphs.
  Checklists use printable marks, not interactive Word controls. Contents are a
  generated heading list, not a live page-numbered Word TOC.
- EPUB, batch export, heading numbering, arbitrary manual page-break syntax,
  named custom profile libraries and document-specific metadata sidecars remain
  deferred. Export selected content applies to Reader selections.
- Page preview snapshots the current buffer. External changes during the modal
  dialog require reopening it. Missing/blocked images remain placeholders.
- Packaging results and final Git reconciliation are recorded after the package
  and startup checks; nothing is pushed or installed by those checks.
