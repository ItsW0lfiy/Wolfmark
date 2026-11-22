# Wolfmark 0.2.0 development

The active version is **0.2.0-dev.1**. It is not a stable release and must not be
published as 0.2.0 until the acceptance work below is complete. Historical Servo
branches remain intact; Servo is no longer the planned Qt replacement.

## Build interface

Xmake orchestrates the existing Cargo/Rust/C++20/Qt Widgets build. It does not
introduce another compiler pipeline, CMake, QML, JavaScript, or a client runtime.
Direct Cargo commands and aliases remain available.

```powershell
cargo setup                         # existing project-local SDK bootstrap
xmake f -m debug
xmake                               # cargo build --locked
xmake run wolfmark fixtures/wolfmark-visual-test.md
xmake test                          # workspace tests; native GUI tests serialized
xmake check                         # format, check, clippy with warnings denied
xmake f -m release
xmake                               # cargo build --locked --release
xmake package                       # Windows setup, portable ZIP, checksums
xmake clean                         # cargo clean (generated Cargo output only)
```

Packaging retains the existing clean-worktree gate. Building an installer is
separate from installing it, pushing, tagging, or publishing it.

On Windows, `xmake test`, `cargo test-background`, `cargo all`, and `xmake package` use a
separate Windows desktop. The test windows stay off your active desktop and
each run writes a log under `out/tests/background`. Direct `cargo test` still
runs on the current desktop. For an isolated package or setup check, run
`pwsh -File scripts/run_background_windows.ps1 -Action package` or use
`-Action setup-check`.

For a GUI review, run `pwsh -File scripts/run_background_windows.ps1 -Action gui`.
It captures Settings at 100% and 150% scale, Page View and Export, and the
Reader, Source and Live Preview fixtures. Review `out/tests/gui-review` and
`out/visual/dev7`, and check `SETTINGS_FRAME` in the background run log for
the actual window bounds. Widget captures show the client area. Check the outer
frame and per-monitor transitions on the active desktop for manual acceptance.

## Document layout and exports

Use **Document actions → Document Page View / Page setup** or **Print preview**
(`Ctrl+P`). **Export document** uses `Ctrl+Shift+E`. Close the page window to
return to the retained Reader, Source, or Live Preview session.

The page window snapshots the current rendered buffer, including unsaved source
changes, through the normal Comrak/presentation/native-document path. It never
adds page breaks or metadata to the Markdown file. Layout changes do not reparse
Markdown, reload files, or decode images again. Before opening the window,
Wolfmark requests outstanding local images through the existing bounded workers;
preparation can be cancelled and has a 30-second timeout. Missing/blocked images
remain visible placeholders. No remote image is fetched.

Page controls include A4 (default), A3, A5, ISO B5, Letter, Legal, custom dimensions,
portrait/landscape, independent margins, content width, page background, body and
heading fonts, line/paragraph spacing, heading scale, code wrapping, syntax colours,
image resolution, cover, contents, H1 page breaks, headers/footers, first-page
variants, and Arabic/Roman page numbers. Header/footer tokens are `{title}`,
`{author}`, `{date}`, `{page}`, and `{pages}`.

Standard A4, School assignment, Technical report, and Compact notes are built-in
presets. **Apply** stores the selected configuration in the existing authoritative
settings.json; Custom can retain a modified configuration. Ordinary Markdown is
not a settings store. Named user profile libraries and per-document sidecars are
not implemented.

PDF and the preview use the same native pagination and page-paint function.
Selectable text, embedded font resources, images, and native link annotations are
retained. Physical printers use the same painter through Qt Print Support.
Printing respects the selected page range.

DOCX is a real Open XML package containing editable paragraphs, heading styles,
inline formatting, native Word numbering, tables, PNG images, hyperlink
relationships, section dimensions/margins, metadata, and header/footer parts with
page fields. ODT is an explicit OpenDocument package from the same native document,
with editable content, styles, images, metadata, and page layout. Neither requires
Office, Pandoc, a service, or a separately installed runtime to export.

HTML is self-contained and embeds prepared local images as data URIs; raw input
HTML remains escaped text. Text export emits the rendered document's text.
Editable/HTML formats can reflow in their receiving applications; PDF is the
reference for identical preview pagination. EPUB and batch export are deferred.
Glyph coverage depends on installed fonts. Linux CI includes Noto CJK; a system
without a CJK font cannot render those characters correctly. No font pack is
silently downloaded by Wolfmark.

Exports use QSaveFile staging and fail rather than falling back to in-place writes.
The dialog confirms replacement of an existing destination and refuses to export
over the source document. Exporting never saves the source buffer.

## Regression and acceptance

The native smoke suite exercises real picker acceptance through the Open action,
file drops onto an editor, canonical duplicates, persisted recent files, Unicode
and reserved filename characters, invalid UTF-8, missing files, and rename/delete
of an open document and its parent directory. A separate export smoke produces
the five formats, checks page sizes and 80/100/125/150% zoom round trips, and
asserts unchanged parse/load/image-request/construction counters and source bytes.

```powershell
cargo test -- --test-threads=1
cargo test -p wolfmark-maintenance --lib
cargo run -- --smoke-export fixtures/wolfmark-visual-test.md
cargo run -- --smoke-file-opening fixtures/wolfmark-visual-test.md
```

Generated exports and layout metrics live under ignored `out/tests/exports`.
Independent QA checks use Python with `lxml` and `pypdf` (development only):

```powershell
python scripts/fetch_document_schemas.py out/tests/schemas
python scripts/validate_document_exports.py --schemas out/tests/schemas
```

The first command downloads the official ODF 1.3 and ECMA-376 Transitional schemas
into ignored storage; subsequent validation is local. Linux CI runs both commands.
Native page preview paints visible pages directly without a per-page picture or
bitmap cache. Raster resolution is a downsampling ceiling; prepared images are
not upscaled. PNG encoding remains lossless. Unsafe link schemes are made inert
in exports and reported in the preview diagnostics.

Installer registry tests use disposable uniquely named HKCU subtrees and need a
normal developer shell because the restricted agent sandbox denies registry
creation. They never write the live Classes or UserChoice trees.

Stable release gates include independent output-package checks and rendered
review, physical printer/page-range testing, Windows picker/input checks, screen
reader and mixed-DPI checks, long-document/image performance, portable/setup
smoke, Linux compilation/GUI verification, licensing inventory, and all required
format/check/test/clippy/release-build checks. Report measured results and remaining
limitations; existence of a writer or a successful build is not acceptance.
