# Moonmark roadmap

The roadmap records intended product direction, not release dates or promises.

Moonmark is being built as a **renderer-first, file-first, all-in-one Markdown application**. It takes inspiration from applications such as FeatherMD, Obsidian, MarkText, and other strong Markdown workflows, but Moonmark should keep its own architecture, renderer, interaction model, and visual identity.

The central product rule is simple:

> **Add capability without making the default interface look bloated.**

Feature growth must not require visible interface growth. Advanced capabilities should use progressive disclosure, contextual UI, commands, shortcuts, optional panels, and user-controlled visibility. A user who only wants to open and read a Markdown file should still be able to use Moonmark as a quiet, simple reader even after the application becomes much more capable.

Moonmark remains based around ordinary files. Features may understand folders, repositories, links, metadata, and related documents, but Moonmark should not require users to adopt a proprietary vault or database just to use the application.

Official features designed and maintained as part of Moonmark belong in **core Moonmark**. The project should not move desirable first-party functionality into mods merely to keep the core artificially small. Mods exist for functionality outside the curated Moonmark product vision.

---

## Completed foundation — `0.1.0-dev.6`

The current development baseline includes:

- retained multi-document viewing in one process
- literal `.txt` viewing alongside Markdown
- smooth, interruptible document navigation and restrained native motion
- native heading outline/navigation
- code-block and table visual refinement
- local image rendering and image-heavy document handling
- native document selection/copy behavior
- percentage zoom using the existing native document
- file watching and retained per-document state
- GPL-3.0-only project licensing and release documentation
- a broad, document-first desktop layout
- Moonmark's restrained lunar/graphite visual identity

This milestone is still an early foundation. The long-term roadmap is intentionally much larger than the current implementation.

---

# Near-term release and distribution work

## Current milestone — `0.1.0-dev.7` — First Windows Release & Shell Integration

Turn the native development checkout into reproducible Windows release artifacts and prepare the first GitHub development prerelease.

Planned work includes:

- accept `.md`, `.markdown`, and `.txt` shell/command-line arguments with spaces and Unicode
- use the explicitly approved Inno Setup installer engine
- install under Program Files
- register Moonmark in Installed Apps / uninstall
- Start Menu shortcut
- optional desktop shortcut
- safe upgrades between Moonmark versions
- Windows Open With integration
- file associations for supported document types
- include required native libraries, plugins, CRT files, assets, notices, and licenses
- preserve the portable ZIP as a separate distribution
- keep installed and portable builds on the same application architecture
- produce deterministic artifact names and SHA-256 checksums
- prepare conservative prerelease notes without publishing remotely
- add typed installed/portable settings and a bounded GitHub release-check/download handoff
- verify exact release assets with the published SHA-256 manifest before offering installation or portable replacement
- audit the Rust/core boundary for later Android document-provider work without selecting a mobile UI

## `0.1.0-dev.8` — Updater hardening and release-channel validation

Harden the initial dev.7 update-delivery path against real published releases and expand it only where release experience justifies the work.

Planned behavior:

- validate conditional release checks and cached metadata against the first published prerelease
- keep checks asynchronous, bounded, quiet when current, and disableable
- validate installed setup handoff and portable ZIP handoff end to end
- harden interrupted download, corrupt checksum, missing asset, and offline recovery behavior
- keep stable, preview, and development channels explicit as the release model grows
- preserve explicit user consent before download and installation
- consider Authenticode signing later if practical and explicitly configured

## Around `0.1.0-dev.9` — WinGet

Prepare optional WinGet publication using the conceptual identifier `ItsW0lfiy.Moonmark`.

Before publishing:

- validate installer metadata
- validate silent install and uninstall behavior
- validate upgrade continuity
- validate package hashes
- validate publisher identity
- ensure installed and portable distributions remain clearly separated

---

# Core renderer roadmap

Moonmark's renderer is the foundation of the application. Other features should build around it rather than replacing it with a second unrelated rendering path.

Planned renderer work includes:

- continuously improve CommonMark and GFM rendering fidelity
- support selectable Markdown compatibility profiles without creating separate renderer implementations
- maintain strong heading hierarchy and document structure
- improve nested lists and task-list presentation
- improve blockquote presentation
- improve table layout, sizing, alignment, selection, and large-table behavior
- improve fenced code blocks and syntax highlighting
- improve inline-code presentation
- improve link and anchor behavior
- improve local image layout
- improve very large image handling
- improve animated image handling where practical
- preserve support for relative and absolute local image paths where safe
- improve broken-resource diagnostics
- improve huge text-only document behavior
- improve huge Markdown document behavior
- improve image-heavy Markdown performance
- keep image decoding/cache work bounded
- avoid reparsing documents for irrelevant layout changes
- avoid unnecessary image re-decoding during resize/zoom
- improve native zoom and reflow performance
- continue profiling large-document QTextDocument/layout costs
- preserve native selection, copying, accessibility, and text interaction
- improve source-position/semantic-position mapping where useful for future editing and diagnostics
- investigate additional Markdown-adjacent rendering features such as math and diagrams when they can be integrated without compromising Moonmark's architecture or renderer quality
- consider richer frontmatter presentation while keeping raw metadata available
- keep raw HTML inert or deliberately constrained unless a future design explicitly changes that policy

Moonmark should prefer a smaller number of well-integrated renderer features over a large collection of fragile rendering hacks.

---

# Markdown compatibility profiles

Moonmark should be able to interpret ordinary Markdown according to selectable compatibility profiles while continuing to use one parser/semantic-model/renderer architecture.

The goal is not to emulate every Markdown application perfectly. The goal is to let users choose how broadly Moonmark interprets Markdown syntax and to combine useful extensions without forcing the document into a proprietary format.

Planned profile direction includes:

- **CommonMark** — conservative standards-oriented Markdown behavior
- **GitHub Flavored Markdown** — CommonMark plus the supported GFM feature set such as tables, task lists, strikethrough, and autolinks
- **Moonmark** — Moonmark's curated default feature set, combining broadly useful supported syntax while remaining predictable and portable
- **Extended** — opt into additional Moonmark-supported Markdown-adjacent syntax as those features are implemented
- **Custom** — advanced per-feature controls for users who want to decide exactly which syntax extensions are enabled

Profile selection should configure parser behavior rather than switch to unrelated rendering engines. Where the parser already exposes individual extension options, profiles should be built from those options instead of duplicating parser logic.

Potential custom controls may include:

- tables
- task lists
- strikethrough
- autolinks
- footnotes
- frontmatter
- future highlight syntax
- future Wiki-style links
- future callouts/admonitions
- future math syntax
- other deliberately adopted Markdown extensions

Compatibility and appearance must remain separate concepts. A Markdown profile decides **what syntax means**; themes and document appearance decide **how the resulting document looks**. Users should be able to combine any supported compatibility profile with any supported Moonmark visual theme or presentation configuration.

Application-specific syntaxes such as Wiki links, callouts, embeds, highlight syntax, or math should only enter a compatibility profile after Moonmark deliberately implements and validates them. A profile must not imply compatibility that the parser and renderer do not actually provide.

The normal settings UI should keep this simple: expose a compact profile selector, with detailed extension switches hidden behind **Custom** or another advanced surface. This keeps compatibility powerful without turning the default settings page into a wall of Markdown feature toggles.

---

# Reader and navigation roadmap

Moonmark should grow from a good single-document reader into a strong Markdown navigation application without forcing a workspace model.

Planned directions include:

- in-document search
- search result navigation and highlighting
- quick open for recently/openly available documents
- command palette
- configurable keyboard shortcuts where practical
- recent files
- recent folders
- stronger drag-and-drop behavior
- richer open-document management
- tab-like or equivalent multi-document navigation without visually overcrowding the shell
- pinning or retaining important documents
- reopen recently closed documents
- duplicate-document detection using canonical identity
- better session restoration
- restore scroll position per document
- restore zoom per document
- restore relevant sidebar/panel state
- restore search state where useful
- configurable startup restoration behavior
- split view for comparing two documents or two positions
- source-and-rendered split viewing
- independent pane scroll/search/history where appropriate
- optional synchronized scrolling where useful
- back/forward document navigation
- stronger heading navigation
- link inspection
- broken-link diagnostics
- hover or lightweight link previews where they remain visually restrained
- Wiki-style link support if adopted
- backlinks / incoming-link discovery if adopted
- related-document navigation
- optional document relationship/graph tooling if it can remain non-intrusive and does not redefine Moonmark as a vault application

Folder/repository-aware features should remain optional. Opening one standalone Markdown file must remain a first-class workflow.

---

# File and document exploration

Moonmark may become much stronger at navigating ordinary collections of Markdown while keeping the filesystem authoritative.

Planned directions include:

- optional file/folder explorer
- fast folder browsing
- lazy loading for large folders
- filtering by filename
- full-text search across a selected folder/source when practical
- include/exclude patterns
- safe handling of symlinks and canonical paths
- folder-level refresh/file watching where practical
- support for repositories without requiring repository ownership by Moonmark
- optional archive viewing/research if it fits the product cleanly
- clear distinction between a single opened file and a deliberately opened folder/source

Moonmark should not silently turn every opened file into a managed workspace.

---

# Metadata, properties, and knowledge features

Moonmark can borrow useful ideas from knowledge-oriented Markdown applications without requiring a vault/database workflow.

Possible core features include:

- YAML/frontmatter property viewing
- user-friendly property presentation
- tag discovery
- body hashtag discovery
- nested tag presentation
- filtering/searching by tag
- filtering/searching by property name/value
- document metadata/details panel
- backlinks
- outgoing links
- related-document discovery
- document link diagnostics
- lightweight graph/relationship views where genuinely useful

These features should operate on ordinary Markdown files and selected folders/repositories. They should not require converting documents into a Moonmark-owned format.

---

# Integrated editor roadmap

Moonmark remains **viewer-first**, but a future editor is planned as an integrated optional capability inside the same application.

Read-only viewing must remain complete and valid without enabling editing.

Planned editor directions include:

- raw/source Markdown editing
- explicit viewer/editor switching
- rendered preview switching
- keyboard shortcut for preview/editor switching
- source + rendered split preview
- optional live preview where performance and correctness are acceptable
- a future MarkText-like editing mode where rendered presentation and editing feel closely integrated
- configurable preferred editing style
- safe save behavior
- Save / Save As
- unsaved-change indicators
- external-change detection while edits are unsaved
- conflict handling instead of silently overwriting external changes
- undo/redo
- find/replace
- source-line navigation
- Markdown-aware editing conveniences
- optional formatting commands
- preserve ordinary files and normal OS file semantics
- do not permanently write-lock documents just because editing exists
- reuse Moonmark's normal parser/semantic model/renderer for preview rather than maintaining a separate unrelated renderer
- avoid loading heavy editor infrastructure when the editor is unused

The editor should expand Moonmark's lifecycle from reading into reading + writing without turning Moonmark into an editor-first IDE.

---

# Git and document history tools

For Markdown stored in Git repositories, Moonmark may provide read-oriented repository tooling without trying to become a complete Git client.

Potential core features include:

- show commit history for the current Markdown document
- render historical versions of a document
- compare current and historical versions
- compare two selected revisions
- line-based source diffs
- investigate rendered/semantic Markdown diffs
- show commit metadata relevant to the document
- show changed headings/sections where practical
- keep Git operations read-only unless a future roadmap explicitly expands the scope
- do not execute arbitrary repository hooks or shell commands as part of ordinary history viewing

---

# Annotations and reading tools

Moonmark may support reader annotations without requiring changes to the source Markdown.

Potential features include:

- highlights
- notes attached to selections
- notes attached to headings
- bookmarks / read-later markers
- document annotation list
- annotation search
- jump back to annotated positions
- safe re-anchoring after a document changes
- annotation import/export
- configurable visibility of annotations

If annotations are implemented, Moonmark should clearly distinguish app-managed annotation data from the user's Markdown file.

---

# Export and publishing tools

Moonmark should eventually become useful not only for displaying Markdown but also for producing polished output from it.

Potential core features include:

- printing
- PDF export
- HTML export
- image export for diagrams/rendered content where appropriate
- multi-document export
- ordered chapter/document collections
- automatic table of contents for exported collections
- page breaks / chapter separation
- link and anchor rewriting for exported collections
- image/resource collection
- EPUB export
- export presets
- export preview
- export diagnostics for missing assets or unsupported content

Export should reuse Moonmark's renderer/semantic model as much as practical rather than becoming a separate rendering product hidden inside the application.

---

# Customization and settings

Heavy customization is a planned first-class Moonmark capability.

The default configuration should remain intentionally simple, but users should be able to decide how much interface they want to see.

Planned settings include:

## Markdown compatibility

- compact compatibility-profile selector
- CommonMark profile
- GitHub Flavored Markdown profile
- Moonmark curated profile
- Extended profile as Moonmark adopts additional syntax
- Custom profile with advanced per-extension controls
- keep parser compatibility independent from visual themes and document appearance
- make compatibility changes predictable and clearly scoped to syntax interpretation

## Document appearance

- font family
- font size
- line spacing
- document padding
- preferred content width
- wide/full-width document behavior
- heading scale
- code font
- code theme/presentation options
- image sizing behavior
- table presentation options
- link presentation options
- theme options
- light/dark/system behavior if adopted

## Motion and interaction

- reduced-motion preference
- animation enable/disable
- navigation motion preferences
- scrolling behavior where practical
- zoom behavior

## Interface visibility

Users should be able to choose which interface elements are visible.

Potential toggles/reordering include:

- sidebar
- open documents section
- outline
- search section
- properties section
- backlinks section
- annotations section
- Git/history section
- file explorer
- breadcrumbs
- tabs/document switcher
- top-bar controls
- document action buttons
- status indicators
- editor controls
- image controls
- optional panels

Where practical, modules should support ordering, collapsing, pinning, and default-open/default-closed behavior.

Moonmark should distinguish between concepts such as:

- **enabled** — the feature exists and can be invoked
- **visible** — the feature has persistent UI
- **pinned** — the feature receives prominent placement

Hiding a button should not necessarily disable the underlying feature. Keyboard shortcuts and the command palette should remain useful for users who want a nearly chrome-free interface.

The long-term customization goal is that a minimal user and a power user can run the same Moonmark build with dramatically different visible UI density.

---

# Anti-bloat UI principle

Moonmark may become feature-rich, but it should not become visually noisy by default.

Permanent product rules for new features:

- do not add a permanent toolbar button merely because a feature exists
- prefer contextual actions where possible
- prefer the command palette for infrequently used actions
- allow advanced panels to stay hidden until invoked
- allow users to hide optional UI
- keep the document canvas visually dominant
- keep the default sidebar restrained
- avoid filling the title bar with controls
- avoid permanent multi-row toolbars unless a future workflow genuinely requires them
- advanced functionality should be discoverable without being permanently visible
- closing a temporary tool should return Moonmark to a clean reader state
- the empty/reader experience should remain understandable to a first-time user

A future Moonmark with dozens or hundreds of capabilities should still be able to look like a simple Markdown reader when configured that way.

---

# Platform roadmap

Moonmark is **Windows-first, not Windows-only**.

Current platform priority:

1. Windows desktop
2. Linux desktop
3. Android / phone

## Windows

Windows remains the primary implementation, validation, and release target in the near term.

Planned work includes:

- installer
- updater
- WinGet
- file associations
- shell integration
- polished taskbar/window behavior
- accessibility validation
- mixed-DPI validation
- touchpad/input validation
- packaging and signing improvements

## Linux

Linux is an intended supported desktop platform.

Planned work includes:

- compile and validate the shared/core architecture on Linux
- native desktop integration
- packaging
- filesystem/path behavior
- file watching
- clipboard/dialog integration
- window-manager behavior
- Wayland/X11 validation as appropriate
- accessibility testing
- distribution format decisions

## Android

Android is an intended future mobile platform, including eventual APK distribution.

Android should be treated as a mobile product surface, not as the desktop UI shrunk onto a phone.

Planned direction includes:

- touch-first navigation
- compact mobile document UI
- mobile-friendly outline/search/navigation
- Android document-provider integration
- support for `content://`-style document access where required
- mobile image viewing
- mobile reader customization
- preserve Moonmark's recognizable visual identity
- reuse portable parsing/semantic/document logic where practical
- decide Android editing separately rather than assuming desktop editing must be copied to mobile

macOS is not currently a committed roadmap target. It may be reconsidered later.

---

# Accessibility and input

Accessibility should improve alongside feature growth rather than being postponed until the application is finished.

Planned work includes:

- keyboard-complete navigation
- visible but Moonmark-consistent focus indicators
- screen-reader/UI automation validation
- semantic controls and labels
- scalable text/UI
- high-DPI behavior
- reduced-motion support
- touchpad behavior
- mouse and keyboard parity where practical
- touch-first behavior on Android
- sensible high-contrast behavior without breaking Moonmark's identity

---

# Optional mod-loader support

Moonmark core may expose a deliberate, versioned support surface for an **optional separately installed Mod Loader**.

The important architecture rule is:

> **Moonmark supports the loader, but Moonmark itself does not discover or load ordinary mods when the loader is absent.**

Without the Mod Loader installed, Moonmark must start and behave normally as vanilla Moonmark.

The intended model is:

`Moonmark core <-> optional Mod Loader <-> mods / loader-managed extensions`

Moonmark may provide stable events, commands, contribution points, or communication hooks that the Mod Loader can use. The loader is responsible for the actual mod ecosystem.

The Mod Loader should own concerns such as:

- discovering mods
- loading/unloading mods
- mod manifests
- mod compatibility
- API version compatibility
- dependency handling
- permissions/capabilities
- mod failures
- extension/mod lifecycle
- optional mod update mechanisms
- translating mod contributions into the supported Moonmark interface

Moonmark core should remain functional if:

- the loader is not installed
- the loader is disabled
- the loader is incompatible
- the loader crashes or disconnects
- an individual mod fails

Moonmark should not require users to install the loader to obtain normal first-party features.

Official Moonmark functionality remains core functionality. The mod ecosystem exists for users who want to add behavior beyond the curated product.

Forking remains another valid way for developers to create substantially different Moonmark-derived applications, subject to the project license.

The exact loader transport/runtime/API is intentionally not locked yet. It should be designed only when mod-loader implementation work begins.

---

# Features that should remain core

When Moonmark itself adopts a feature as part of the official product vision, it should normally be implemented directly in core rather than shipped as an official mod.

Examples of core-class functionality include:

- renderer improvements
- image handling
- document navigation
- search
- quick open
- command palette
- split view
- backlinks if adopted
- properties/tags if adopted
- Git history/diff if adopted
- annotations if adopted
- export tools
- integrated editing
- settings and customization
- platform support
- accessibility
- update/distribution infrastructure

Mods are for functionality beyond the official curated scope, experimentation, niche workflows, novelty behavior, organization-specific tooling, and community additions.

---

# Performance roadmap

Performance remains part of Moonmark's product identity, especially for image-heavy Markdown.

Continue measuring and improving:

- cold startup
- warm startup
- first document render
- repeated render/switch behavior
- retained multi-document switching
- large-text document construction
- large Markdown parse time
- presentation-model construction
- native layout/reflow
- zoom cost
- scroll/navigation frame pacing
- image decode throughput
- image cache memory
- image-heavy document completion time
- memory with one document
- memory with many retained documents
- memory with large images
- folder/search indexing if introduced
- editor performance when introduced

New features should not casually regress the simple read-only path.

---

# Security and trust boundaries

As Moonmark gains features, ordinary Markdown files should remain untrusted input.

Future work should preserve clear boundaries around:

- filesystem access
- canonical paths
- symlinks
- local resources
- remote resources
- external URLs
- export inputs
- Git/repository data
- annotation data
- editor writes
- mod-loader communication
- mod capabilities

Features that involve remote content, arbitrary code, or external processes require explicit design rather than being silently added as conveniences.

---

# Longer-term product direction

Moonmark's eventual scope may include most of the useful lifecycle around Markdown:

- open
- read
- navigate
- search
- inspect
- organize
- compare
- annotate
- edit
- preview
- save
- export
- review history
- continue on another supported platform

The application may become an all-in-one Markdown tool, but it should remain recognizably Moonmark rather than turning into a generic IDE or visually dense knowledge-management dashboard.

The desired end state is:

> **A feature-rich, highly customizable Markdown application built around an excellent renderer, where ordinary users can keep the interface extremely simple and power users can expose the tools they want.**

---

# Open architectural decisions for later milestones

These should be decided when implementation is close enough to justify locking them:

- shell/file-association behavior when Moonmark is already running
- single-instance versus multi-instance policy
- exact integrated editor widget/engine and buffer architecture
- exact live-preview strategy
- exact Markdown compatibility-profile definitions and persistence behavior
- exact strategy for Moonmark-specific syntax that is not directly supported by Comrak
- exact math/diagram renderer strategy
- exact folder/search indexing strategy
- exact annotation persistence format
- exact Git integration implementation
- exact export engines/formats
- exact Linux packaging targets
- exact Android presentation technology
- whether Android editing is supported
- exact Mod Loader communication/runtime/API design
- whether any single-executable packaging is practical without compromising licensing or maintainability

Do not prematurely lock these decisions merely because they appear on the roadmap.

---

# Roadmap status model

Use this document for high-level product direction.

Use GitHub Issues for individual feature proposals, design discussions, implementation tracking, screenshots, and the historical record of features that are eventually shipped.

As features become concrete:

- move them into a defined milestone/version when appropriate
- create focused issues for implementation-sized work
- link completed issues to the release/commit that shipped them
- keep completed issues as useful feature-development history rather than deleting them

The roadmap should remain readable as the description of where Moonmark is going rather than becoming a giant release checklist.
