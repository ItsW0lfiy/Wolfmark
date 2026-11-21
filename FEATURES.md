# Wolfmark — Feature Tracker

> **Last reviewed:** 2026-11-18
> **Status baseline:** `0.2.0-dev.1` development, based on `9ea8d72`; see [acceptance results](docs/development/wolfmark-0.2.0-validation.md). Stable acceptance remains incomplete.
> **Purpose:** Track what exists, what still needs work, and ideas not yet committed to implementation. This is a *feature inventory*, not a promise of release dates.

## How to read this file

- **[x] Implemented in source** — present in the current `main` codebase or explicitly described by its implementation documentation. This **does not** mean independently verified on the user's PC or released.
- **[ ] Planned** — intended feature or work not complete on `main`.
- **[ ] PARTIAL** — some functionality exists, but a broader requirement is not finished.
- **[ ] BUG / BLOCKER** — known reported regression or a boundary violation requiring attention.
- **[ ] CANDIDATE** — explored or proposed, **not** locked as a product requirement.
- **[ ] VERIFY** — implementation is claimed or present, but needs real-world testing before acceptance.
- **[ ] SUPERSEDED** — historical plan that must **not** be reintroduced as a current requirement.

These checkboxes describe the **Qt-based `main` branch**. Features implemented only in the [Servo experiment](https://github.com/ItsW0lfiy/Wolfmark/tree/experiment/servo-embedding-poc-20261026) are **not** marked complete. A documentation-only change must not be treated as a functioning feature. Before checking an item off, verify implementation, regression tests, and any necessary actual GUI behavior.

**Product principle:** Renderer-first, file-first, viewer-first, all-in-one Markdown app. Feature growth must **not** make the default UI bloated. Ordinary files remain the source of truth, not a required vault or Wolfmark-owned document database. Official first-party functionality stays in the core app and is free.

---

## 1. Critical bugs and safety boundaries

- [x] Fix window-wide Open shortcut routing, editor file-drop interception, silent opening errors, and failed sessions being retained. Real picker acceptance and drops are regression-tested.
- [x] Native opening regressions cover `.md`, `.markdown`, `.txt`, Unicode/spaces/reserved characters, CLI arguments, multiple documents, canonical duplicates, and persisted recent documents.
- [x] Missing/invalid/unreadable-file errors, cross-drive opening, and the command-line `--` delimiter have focused Windows regressions.
- [x] Optional Windows Open with and Default Apps registration for `.md`, `.markdown`, and `.txt`. Windows retains control of default selection.
- [ ] **VERIFY:** Exercise Open with and Default Apps on a normal Windows desktop after install, update, repair, modify and uninstall.
- [x] Opening regression renames an open document and its parent directory, deletes it, and checks unchanged content/source bytes. Physical installer lifecycle acceptance remains separate.
- [x] Source-level external modification/conflict detection for opened documents and unsaved changes.
- [x] Source-level atomic save through `QSaveFile` with direct-write fallback disabled.
- [ ] **VERIFY:** Test save/reload conflicts, preservation of dirty buffers, interrupted saves, concurrent external edits, and error recovery on real Windows filesystems.
- [ ] **VERIFY:** Treat document input, local image references, links, exports, repositories, and extension data as untrusted; retain clear and tested security boundaries.

## 2. Platform architecture and build tooling

- [x] Existing single-process application built with Rust, C++20, Qt 6 Widgets, and native `QTextDocument`.
- [x] Existing Cargo-controlled Qt build, linking and Qt runtime staging through `build.rs`.
- [x] Xmake is the preferred project command interface, delegating to the retained Cargo/native Qt build.
- [x] Document Xmake debug/release, run, test, check, package and clean commands; existing `cargo setup` bootstraps the SDK.
- [x] Preserve Cargo SDK discovery and runtime staging; add Qt Print Support to linking and packaging.
- [x] Xmake and direct Cargo share the same dependency ownership and output cache.
- [ ] **CANDIDATE:** Evaluate which existing Rust/C++ boundary abstractions should remain, simplify, or be rewritten when technically beneficial.
- [ ] **CANDIDATE:** Evaluate selective or extensive Qt Quick/QML migration and a Widgets/QML hybrid, with actual rendering/performance/input comparisons.
- [ ] **CANDIDATE:** Adopt other Qt modules where they materially improve functionality (e.g., image, SVG, concurrency, printing, platform integration).
- [ ] Continue Windows and Linux portability during architectural changes; do not rewrite working components solely to increase the percentage of C++ or Rust.
- [ ] **SUPERSEDED:** No project-authored C++.
- [ ] **SUPERSEDED:** Rust must always own application policy and all authoritative document state.
- [ ] **SUPERSEDED:** Cargo is required to be the overall build orchestrator.
- [ ] **SUPERSEDED:** Servo must replace Qt. Retain the experimental branch/history as reference, **not** a production feature or active migration requirement.
- [ ] **SUPERSEDED:** A CMake-based rewrite is required. **Xmake is preferred**; CMake is not the planned primary manager.

**Current language policy:** C++ (including majority-C++ applications), Rust, QML, and useful Python/PowerShell tooling are allowed. Keep what performs well; rewrite Qt Widgets parts into other Qt technologies if that produces a measurable benefit. The target is functionality and maintainability, not a language ratio.

## 3. Files, documents, and sessions

- [x] Markdown `.md`/`.markdown` and literal `.txt` handling in the Qt source.
- [x] Native Open file picker, window-wide `Ctrl+O`, drag-and-drop and command-line opening, with runtime regressions (see section 1).
- [x] Multiple retained document sessions in a single process.
- [x] Per-document native renderer/editor objects and switching without unnecessary reloads.
- [x] Close and reload opened document actions.
- [x] Canonical-path duplicate-document detection.
- [x] Per-document Reader/Source/Live mode and zoom/scroll preservation while the session is retained.
- [x] File watching and clean-change reload with dirty-change conflict indicators.
- [ ] Better cross-restart restoration of open documents, their positions, modes, selected panels, and search state.
- [x] Persisted canonical recent documents with menu and empty-screen access.
- [ ] Recent-folder history beyond the retained last open directory.
- [ ] Reopen recently closed documents.
- [ ] Pin/retain important documents.
- [ ] Quick Open covering recent and available documents.
- [ ] Configurable behavior for restore-last-session vs clean startup.
- [ ] Improved multi-document back/forward history where useful.
- [ ] **CANDIDATE:** Single-instance application and forwarding file opens into an existing process; current milestone permits multiple processes.
- [ ] **VERIFY:** Handle moved/renamed/deleted paths without losing unsaved content, keeping forbidden locks, or falsely claiming the file.
- [ ] Keep standalone file opening equally first-class even as folder features grow.

## 4. Folder Mode, explorer, and repositories

- [ ] Optional open-folder mode, distinct from opening a single file.
- [ ] Filesystem explorer sidebar for Markdown, text, and supported images.
- [ ] Recursive folder navigation with lazy loading for large directories.
- [ ] Fast folder browsing, filename filtering, and include/exclude patterns.
- [ ] Folder-scoped full-text search.
- [ ] Folder watching and selective refresh.
- [ ] Canonical/symlink handling without owning/locking a directory.
- [ ] Repository-aware navigation without requiring a Git vault or repository rewrite.
- [ ] **CANDIDATE:** Optional archive inspection/browsing.
- [ ] **CANDIDATE:** Optional external WolfSearch integration for Quick Open or deeper search; do not make it required.
- [ ] Explicitly distinguish file-first and folder-source workflows in state, UI, and persistence.

## 5. Markdown reader and rendering

- [x] Existing Comrak-based Markdown parsing and a framework-neutral Rust presentation model.
- [x] Native `QTextDocument` Reader layout and text selection/copy.
- [x] Headings, paragraphs, emphasis, strikethrough, links, horizontal rules, and anchors in the current Markdown presentation.
- [x] Ordered/unordered lists, task-list support, and nested-list structure in current renderer.
- [x] Blockquotes and restrained native styling.
- [x] Tables with native layout and graphite headers/separators.
- [x] Fenced code blocks, inline code, and syntax highlighting.
- [x] Literal `.txt` Reader handling without parsing the text as Markdown.
- [x] Percentage zoom using the existing native document instead of recreating it.
- [ ] Continue GFM/CommonMark fidelity and advanced nesting compatibility.
- [ ] Improve large-table width, cell alignment, selection, and overflow behavior.
- [ ] Improve list/task-list, blockquote, and code-fence appearance and consistency.
- [ ] Improve heading/anchor navigation and link inspection.
- [ ] Improve source-line/semantic position mapping and reflow stability.
- [ ] Richer frontmatter presentation without losing raw metadata.
- [ ] Missing/broken-resource diagnostics in the rendered document.
- [ ] **CANDIDATE:** Mathematical notation and diagram rendering.
- [ ] **CANDIDATE:** Richer callouts/admonitions, highlight syntax, footnotes, Wiki links, and embeds once explicitly adopted.
- [ ] Keep raw HTML inert or deliberately restricted unless a new, reviewed policy is approved.
- [ ] Use one coherent rendering architecture; don't quietly ship incompatible secondary Markdown renderers.

### Markdown compatibility profiles

- [ ] Profile selector in settings.
- [ ] CommonMark profile.
- [ ] GitHub Flavored Markdown profile.
- [ ] Wolfmark curated-default profile.
- [ ] Extended profile for consciously supported extras.
- [ ] Custom profile with extension toggles: tables, task lists, strikethrough, autolinks, footnotes, frontmatter, highlights, Wiki links, callouts, math (only where implemented).
- [ ] Keep syntax interpretation independent of theme and formatting preferences.
- [ ] Verify compatibility against fixtures; a profile must not advertise syntax Wolfmark does not support.

## 6. Images and dedicated image viewing

- [x] Local image path resolution for relative, parent, absolute, `file:///`, percent-encoded, spaced, and Unicode paths.
- [x] PNG/JPEG/WebP decoding and GIF **first-frame** display; SVG static rasterization with `resvg`.
- [x] Bounded background decode workers, generation cancellation, and per-document LRU image cache.
- [x] Visible/nearby image prefetch and deduplicated decode requests for repeated assets.
- [x] Deferred/batched UI delivery and image geometry changes without redundant decoding on zoom/resize.
- [x] Missing/unsupported images display placeholders instead of triggering unsafe requests.
- [x] Remote HTTP(S) images are blocked from fetching in the current document pipeline.
- [ ] **PARTIAL:** Better animated GIF and animated WebP playback; current code only uses first GIF frame.
- [ ] **CANDIDATE:** Additional formats such as AVIF, where safe and useful.
- [ ] Improve huge image responsiveness, memory usage, and zoom quality.
- [ ] Dedicated standalone **image viewer** as part of core Wolfmark.
- [ ] Open supported individual image files from an Open dialog, Folder Mode, or arguments.
- [ ] Image canvas: fit-to-window, zoom, pan, fullscreen, and next/previous.
- [ ] Browse images according to their directory or explorer order.
- [ ] Open an image in its external/default viewer (e.g., PicView), reveal it in the filesystem, and copy its path.
- [ ] Keep the embedded Markdown image pipeline and the standalone viewer complementary, not competing implementations.

## 7. Reader navigation, search, and split panes

- [x] Native heading outline/sidebar and heading navigation.
- [x] Smooth native scrolling, per-session position handling, and Reader zoom.
- [x] Basic Find navigation in the native editor/current UI.
- [ ] **PARTIAL:** More comprehensive in-document search result highlighting, result counts, and per-document search memory.
- [ ] Command palette.
- [ ] Configurable keyboard shortcuts.
- [ ] Reopen/quick-open and multi-document history controls.
- [ ] Source + rendered split pane.
- [ ] Two-document comparison split pane.
- [ ] Independent pane scroll, zoom, navigation, search, and history where sensible.
- [ ] Optional synchronized scrolling across panes.
- [ ] Link inspection, broken-link diagnostics, and lightweight hover previews.
- [ ] **CANDIDATE:** Wiki-style links and backlinks/outgoing-link navigation.
- [ ] **CANDIDATE:** Related-document and graph/relationship navigation.

## 8. Editor and Live Preview

- [x] Reader, Live Preview, and Source as distinct document modes in the native UI.
- [x] Shared Source/Live native text buffer and undo history.
- [x] Editing via `QPlainTextEdit`, contextual Markdown decorations, and line/column UI.
- [x] Save, Save As, dirty state, undo/redo, and source-safe file saving in code.
- [x] Ctrl+B/Ctrl+I formatting, indentation, source Find, and other native shortcuts in code.
- [x] Live Preview contextual minimization/revelation of supported Markdown markers around caret/selection.
- [x] Editor preservation of BOM, mixed line endings, whitespace, and final-newline behavior as documented.
- [x] User-visible external-change conflict handling in native document sessions.
- [ ] **VERIFY:** Physical typing, clipboard, grapheme/Unicode, IME composition, selection, and undo/redo fidelity on Windows.
- [ ] **PARTIAL:** Extend Live Preview coverage beyond heading/emphasis/link/code source decorations (complex constructs currently remain raw).
- [ ] Find **and replace** including safe replacement behavior.
- [ ] More robust source-line jumping and caret placement across mode changes.
- [ ] Optional Source + Reader split editing.
- [ ] Configurable preferred editor style and appearance.
- [ ] **CANDIDATE:** More deeply integrated, MarkText-like directly rendered editing.
- [ ] Keep initial Reader lightweight when editor infrastructure is unused; never force editing on a read-only workflow.

## 9. Application UI and window behavior

- [x] Current charcoal/graphite + silver + restrained-crimson design identity.
- [x] Custom frameless Qt Widgets window and title bar.
- [x] Window minimize, maximize/restore, close, drag, and resize implementation.
- [x] Normal/maximized/F11 borderless-fullscreen state separation in code.
- [x] DWM rounded-corner policy and Win32 Snap eligibility/hit-test implementation in code.
- [x] Collapsible sidebar, open-document navigation, heading outline, and responsive collapse.
- [x] Visible Open, Reload/document actions, Settings, mode switching, and diagnostics controls.
- [x] Native smooth/interruptible wheel scrolling and middle-button autoscroll indicator.
- [x] Reduced-motion override for existing scrolling/navigation motion.
- [ ] **VERIFY:** Real Windows Snap Layout hover, Snap Assist, multi-monitor/mixed-DPI movement, taskbar, corners, system menu, title-bar controls, and resizing.
- [ ] **VERIFY:** Exact approved desktop UI screenshot parity and interactive visual details on different sizes/DPI.
- [ ] **CANDIDATE:** Rewrite selected shell pieces in QML/Qt Quick while keeping native text editing where strongest.
- [ ] **CANDIDATE:** Qt Widgets/QML hybrid or more QML-heavy UI after performance and input validation.
- [ ] More polish for context menus, selection, accessibility, focus, animations, settings, and theme transitions.
- [ ] Preserve the approved no-duplicate-document-selector rule: no redundant horizontal tabs or second Open dropdown by default.
- [ ] Preserve bottom-left wolf/mountain identity artwork and restrained titlebar layout.
- [ ] Keep document canvas visually dominant; advanced features should not require always-visible clutter.

## 10. Customization and theme system

- [x] Existing Wolfmark native dark visual styling.
- [x] Existing settings infrastructure for word wrap, last-open-folder, and updater options.
- [ ] User controls for font family, font size, line spacing, padding, content width, and heading scale.
- [ ] Code font and syntax theme preferences.
- [ ] Image sizing, table appearance, and link presentation preferences.
- [ ] Optional light/dark/system mode and multiple built-in themes.
- [ ] Configurable scrolling, animation, zoom, and reduced-motion preferences.
- [ ] Hide/show/reorder/pin interface elements: sidebar, documents, outline, search, properties, backlinks, annotations, Git history, file explorer, breadcrumbs, top bar, action buttons, status, editor and image controls.
- [ ] Distinguish **enabled** (callable), **visible** (shown), and **pinned** (prominent).
- [ ] Keyboard/command access to enabled features even when their buttons are hidden.
- [ ] UI presets (minimal Reader and power-user layouts) and safe reset to defaults.
- [ ] **CANDIDATE:** Full user-customizable UI via a controlled, versioned interface API.
- [ ] **CANDIDATE:** CSS-editor ambition from the Servo-era discussions, now needing a **new Qt-native design**; arbitrary web-CSS customization is **not automatically possible in QML/Widgets**.
- [ ] If custom themes/layouts are supported, recover cleanly from broken customization and preserve user data.

## 11. Metadata, knowledge tools, and document relationships

- [ ] YAML/frontmatter properties viewer and user-friendly editing/presentation where approved.
- [ ] Document details/metadata panel.
- [ ] Tag and body-hashtag discovery, nested tags, and search/filter by tag.
- [ ] Search/filter by metadata property keys and values.
- [ ] Outgoing-link listing and invalid-link diagnostics.
- [ ] **CANDIDATE:** Backlinks and related-document discovery.
- [ ] **CANDIDATE:** Non-intrusive document relationship graph.
- [ ] Keep optional document knowledge functions compatible with standalone files and ordinary folder/repository workflows.

## 12. Git history and comparison

- [ ] Detect Markdown documents inside Git repositories without taking repo ownership.
- [ ] List document commit history and relevant commit metadata.
- [ ] Open/render historic Markdown revisions.
- [ ] Compare working file vs historic revision and two selected revisions.
- [ ] Line-based source diffs.
- [ ] **CANDIDATE:** Semantic/rendered Markdown diffs and changed-heading summaries.
- [ ] Git history remains read-only initially; no arbitrary hooks/shell execution during inspection.

## 13. Annotations and reading aids

- [ ] Text highlights.
- [ ] Notes anchored to text selections or headings.
- [ ] Bookmarks/read-later markers.
- [ ] Annotation sidebar, search, visibility controls, and jump-to-position.
- [ ] Re-anchor annotations safely after document content changes.
- [ ] Annotation import/export.
- [ ] Select an annotation storage model that never secretly modifies source Markdown.

## 14. Printing, export, and publishing

- [x] Native Qt print dialog and shared page painter; physical printer acceptance remains pending.
- [x] A4/A3/A5/B5/Letter/Legal/custom page preview, portrait/landscape, single/continuous pages, page navigation and fit/zoom.
- [x] Page/style/metadata controls and four presets, persisted independently of Markdown source.
- [x] Native PDF, editable DOCX/ODT, standalone HTML and plain-text export. Official XML schemas and independent PDF checks supplement native regressions.
- [ ] Image export for rendered pages/diagrams where relevant.
- [ ] Multiple-document/chapter export with ordered sections.
- [x] Generated heading contents, optional cover and H1 page breaks, headers/footers, first-page variants and page numbering. Contents are a static heading list, not a live paginated Word TOC.
- [x] Embed prepared local images in DOCX/ODT and standalone HTML; preserve safe links and anchors. Unsafe schemes are inert and reported.
- [ ] EPUB export.
- [x] Export preview, diagnostics, source-overwrite guard and explicit replacement confirmation.
- [x] Reuse existing Comrak, presentation and native document; page changes do not reload/reparse or request images again.
- [ ] Named custom profile libraries, document-specific sidecars, heading numbering and batch export.
- [ ] **VERIFY:** Independent Word/LibreOffice rendered DOCX/ODT acceptance and physical printer testing.

## 15. Installer, portable distribution, and updates

### Windows setup and maintenance

- [x] Existing NSIS 3.12 + Rust maintenance + Qt setup-host packaging/build scripts.
- [x] Existing Windows portable ZIP, installer payload, manifest, and checksum production tooling in source.
- [x] Existing installer lifecycle concepts: install, repair, update, uninstall, rollback/transaction journal.
- [ ] **PARTIAL / SUPERSEDED DISTRIBUTION DESIGN:** Move from the older bundled offline-payload setup toward a **small online setup EXE** using verified remote releases; keep the portable ZIP.
- [ ] Verify online bootstrap install, upgrade, repair, modify, reinstall, uninstall, and interrupted/recovery paths.
- [ ] Use one Wolfmark-branded maintenance experience, including uninstall rather than generic visible uninstaller UI.
- [ ] Installed Apps entry, Start Menu shortcut, and optional desktop shortcut; verify actual shipped behavior.
- [ ] Silent setup/uninstall and safe upgrades.
- [ ] Exact semantic version, commit, architecture, channel, build metadata, manifests, and SHA-256 verification.
- [x] Installer can add Wolfmark as a Windows file-opening candidate. Default selection remains a separate Windows user action.
- [ ] **VERIFY:** Execute complete lifecycle tests on clean Windows installations and older versions.
- [ ] Preserve portable ZIP as a valid independent no-registration distribution.

### Update delivery

- [x] Existing optional asynchronous GitHub release checks with cached/ETag behavior in source.
- [x] Settings for startup update checks and prerelease inclusion.
- [x] Existing release metadata selection and checksum-verification code paths.
- [ ] **VERIFY:** Correct official GitHub Releases endpoint, published asset naming, and production release handling.
- [ ] **VERIFY:** Download, verify, and hand off exact installed setup / portable ZIP without silent execution or unsafe overwrites.
- [ ] Hardening for offline/corrupt/missing/interrupted downloads and asset rollback.
- [ ] Explicit stable/preview/development channel model as releases evolve.
- [ ] Optional WinGet publication (`ItsW0lfiy.Wolfmark` proposed identifier).
- [ ] **CANDIDATE:** Authenticode signing if practical without making paid services mandatory.
- [ ] No hidden telemetry, mandatory account, forced restarts, or silent installation.

## 16. Cross-platform plans

### Windows

- [x] Existing primary Windows desktop Qt application and Win32 window adapter in source.
- [ ] Complete physical Windows GUI acceptance, high-DPI, input, and accessibility checks.
- [ ] Complete install/update/repair/portable release acceptance and optional WinGet publication.

### Linux

- [x] Qt/Linux compilation path and filesystem-aware code are implemented in source.
- [ ] **VERIFY:** Native Linux build, run, files, image handling, UI, clipboard, file watching, and font/rendering fidelity.
- [ ] Wayland/X11 behavior, window-manager integration, and accessibility verification.
- [ ] **CANDIDATE:** Distribution through DEB, RPM, AppImage, Flatpak/Flathub or a useful subset; final formats not locked.
- [ ] Linux-native installation/update/uninstall behavior without taking unwanted file associations.

### Android

- [ ] Android application and mobile-first UI (currently **not implemented**).
- [ ] Touch-friendly Reader, outline, search/navigation, and image viewing.
- [ ] Android Storage Access Framework / document provider support including `content://`.
- [ ] Mobile customization and shared portable application logic.
- [ ] **DECISION OPEN:** Scope of Android editing and choice of Qt/mobile presentation approach.
- [ ] APK publishing and **CANDIDATE** F-Droid distribution if the finished build meets its policies.

### Other platforms

- [ ] **CANDIDATE ONLY:** macOS support; not a committed platform target.

## 17. Optional mod loader and extensibility

- [ ] Publish a deliberate, versioned Wolfmark core API/hooks/events/commands useful to a **separate optional Mod Loader**.
- [ ] Loader itself handles mod discovery, loading/unloading, manifests, compatibility, dependencies, permissions, failures, API versions, lifecycle, and optional updates.
- [ ] Mods can contribute only through controlled/approved interfaces; prevent unexpected file, network, or process authority.
- [ ] Normal Wolfmark must work without a loader, if disabled, incompatible, or crashed.
- [ ] Official product features (renderer, image viewer, editing, exporting, searches, customization, etc.) belong in **core Wolfmark**, not mandatory mods.
- [ ] **DECISION OPEN:** Exact loader transport/runtime/API and mod UI contribution model.

## 18. Accessibility, performance, and validation

- [x] Existing Rust benchmark/fixture tooling, Qt smoke checks, image stress tests, and native performance probes in repository.
- [x] Existing instrumentation around smooth scrolling, layout, images, zoom, and render workloads.
- [ ] Keyboard-complete operation and meaningful focus indicators across all modes/panels.
- [ ] Screen-reader and platform accessibility tests.
- [ ] Scalable/high-DPI UI, high contrast, touchpad precision, and reduced-motion acceptance.
- [ ] Verify cold/warm startup, first render, retained document switching, large text/Markdown, image-heavy documents, native reflow, smooth scrolling, zoom, and memory use.
- [ ] Measure folder/search indexing and future editor/export workloads before broad rollout.
- [ ] Test no silent external network resource fetching from untrusted Markdown.
- [ ] Regression tests for mixed line endings, BOM, Unicode, file renames/deletions, unexpected permissions, broken assets, external changes, and data-loss risks.
- [ ] Full Windows and Linux UI/regression gates before declaring platform parity.
- [ ] Runtime/packaging comparisons when considering a Widgets/QML hybrid or large Qt rewrites.

## 19. Permanent product principles and rejected/superseded proposals

- [x] GPL-3.0-only source license and current free/open-source project identity.
- [ ] **ONGOING POLICY:** All official first-party features free, with no paid edition, feature paywall, trial expiration, or mandatory subscription.
- [ ] **CANDIDATE:** Optional donations, which never change access/functionality.
- [ ] **ONGOING POLICY:** A simple, uncluttered default Reader even after adding many advanced capabilities.
- [ ] **ONGOING POLICY:** No mandatory vault, proprietary workspace conversion, or forced repository ownership.
- [ ] **ONGOING POLICY:** No involuntary file-handler claims or unnecessary read/write locks.
- [ ] **ONGOING POLICY:** Do not add permanent UI buttons for every feature; prefer contextual actions, a command palette, and optional panels.
- [ ] **SUPERSEDED:** Servo-only future frontend, the anti-C++ restriction, Rust-only application ownership, and Qt Widgets immutability.
- [ ] **SUPERSEDED:** Full offline setup as the preferred installer (portable ZIP remains).
- [x] Optional Windows Open with registration is an existing feature. Removing an existing feature requires explicit user authorization.
- [ ] **SUPERSEDED:** A second horizontal document-tab strip and redundant Open dropdown in the approved sidebar design.
- [ ] **SUPERSEDED:** Move official first-party capabilities into an optional mod merely to keep Wolfmark's codebase artificially small.

---

## Notes on status and maintenance

1. This tracker is **not** a green-light list for blind implementation. `[x]` means code/documentation evidence on the Qt `main` branch, not that the user has validated it. A reported bug overrides apparent source-level completion until reproduced and resolved.
2. The existing [ROADMAP.md](ROADMAP.md), [architecture overview](docs/architecture/overview.md), [document editing](docs/architecture/document-editing.md), [images](docs/architecture/images.md), [windowing](docs/architecture/windowing.md), [packaging](docs/distribution/packaging.md), and [updater](docs/distribution/updates.md) contain historical design/implementation details. Some architectural and installer statements there now **conflict with later user decisions**; update them through separate reviewed changes rather than silently treating old policies as current.
3. This file tracks **current `main`**. Work on experimental branches is not complete until integrated and verified on the production branch.
4. Maintain feature statuses as code, tests, accepted design decisions, and real platform evidence evolve. Move settled feature details into issues/appropriate architecture docs when useful; keep this file as the discoverable all-feature overview.
