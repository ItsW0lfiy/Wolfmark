# Crimson Graphite — approved reference and acceptance (implementation rejected)

> **Design acceptance: REJECTED (October 24, 2026).** The current source is a
> functional intermediate implementation, not an approved completion of the
> Crimson Graphite redesign. Do not describe it as visually finished because
> Linux/Windows CI passes. The approved concept screenshot is the comparison
> target, and the user specifically rejected missing smoothness, insufficient
> rounded corner treatment, and incomplete visual fidelity.

This working specification describes the October 2026 redesign of Wolfmark's
native Qt desktop UI. It is a design implementation document, not a new product
architecture or permission to fabricate controls absent from the application.
The user-approved concept is the visual target. The canonical product icon and
file icons in `assets/design-reference/wolfmark/` remain authoritative.

## Required visual match before acceptance

- Compare actual Qt screenshots at the **same canvas size and DPI** as the
  approved reference, using an equivalent Markdown document. Do not confuse
  different fixture content with differences in UI rendering.
- The global header must match the reference's integrated branding,
  breadcrumbs, search, settings and caption-control alignment, not merely have
  the same dark background.
- Sidebar: correct hierarchical outline connectors, document file icons,
  selection treatment, spacing, rounded hover/selected rows and footer.
- One document navigation surface: open-document selection and close controls live
  in the sidebar only. No second horizontal file tab strip or duplicate plus tab.
- Toolbar: crisp joined geometry, real icons, visually continuous segmented
  Reader/Live Preview/Source modes, smooth radius joins and no clipping.
- Reader: editorial typography, clear hierarchy, soft graphite layers, rounded
  inline-code pills, full-width rounded code frames with an integrated
  language header and a **functional Copy button on the right**, accurately
  rounded tables, and refined callout/block quote surfaces.
- Settings and setup: consistent custom chrome, rounded containers and
  controls, typography, paddings and hover/focus states matching the main UI.
- Smoothness is not an aesthetic adjective: validate input-to-paint latency,
  transition continuity, resize responsiveness, fast-wheel stability, focus
  response and reduced-motion behavior in actual interaction.
- No dead buttons, fake paths, or features invented solely to match the
  concept image. Preserve Rust ownership and existing document contracts.
- Completion needs human review **after** side-by-side screenshot inspection,
  not just a green build, CI job, or code review.

## Architecture and ownership

- Rust remains the owner of document parsing, model state, semantic formatting,
  file identity, settings serialization, and update/maintenance decisions.
- Qt/C++ remains responsible for native text widgets, selection, drawing,
  keyboard focus, styling, window frame, and platform-specific presentation.
- The design must not rewrite files, change source bytes, lose undo history,
  or reconstruct the semantic document during mode/zoom/hover changes.
- Do not introduce Chromium, Electron, JavaScript UI frameworks, C#, or
  new application runtimes for layout or animation.

## Layout contract

- Graphite titlebar: 48 logical pixels, dynamic breadcrumb, a genuine Reload
  action (F5), interactive Find, a native vector Settings icon with readable
  text, and real native caption controls.
- Sidebar: 240 logical pixels; one full-width Open document button (Ctrl+O),
  open documents with selection/close controls, outline navigation, and a
  restrained footer. Only real files are listed. No Open dropdown.
- No horizontal document tabs, including when multiple files are open; do not
  reintroduce them from the older approved-concept illustration.
- Disabled controls must never advertise a clickable hand cursor. Native
  hit testing must distinguish interactive controls from passive surfaces.
- Command row: 48 logical pixels, real previous/next-open-document navigation,
  equal treatment of Reader / Live Preview / Source, actual zoom and actions.
- Document canvas: full available width, readable editorial hierarchy,
  neutral graphite surfaces, disciplined crimson active/focus indicators.
- Windows narrow layout: search entry can disappear if space is insufficient;
  Ctrl+F must remain accessible. The sidebar may collapse as before.
- Settings: use exactly the same graphite/crimson tokens as the main shell,
  with an editorial heading hierarchy, actual option rows, and honest Save/Cancel
  semantics. Nothing is persisted on Cancel.

## Rendering contract

- Inline code remains selectable source text with native character background
  and a subtle rounded accent edge, not a replacement element.
- Fenced Reader code remains a continuous native `QTextFrame`; border styling
  is a clipped viewport decoration. Copy remains a real action.
- Complex fences and unsupported structures in Live Preview remain editable
  Markdown. Avoid jagged per-token opaque rectangles that destroy cohesion.
- Native scrollbars remain bounded and must not reset the document position
  during free-spin or large wheel bursts.
- Reduced-motion input prevents decorative animation, never accessibility.

## Functional controls

Every visible control must either execute an existing function or be disabled
with an accurate accessible description. Previous/next arrows cycle among real
open sessions (Alt+Left and Alt+Right); they are not a fabricated edit history.
The Open document button opens the real file chooser. Reload is a separate
top-bar action. There are no imaginary vaults, folders, cloud searches,
accounts, bookmarks, or paid features.

## Verification matrix

1. `cargo check -p wolfmark --locked --offline` / `cargo test -p wolfmark`
2. `cargo build -p wolfmark`, real Qt offscreen Reader/Live/Source screenshots
3. Shell-mode interaction smoke, no-duplicate-tabs checks, native-icon and disabled-cursor checks
4. Code-frame render, selection/copy behavior, code editing history
5. Fast-wheel/scroll stabilization, anchor preservation, resize, zoom
6. Settings modal navigation, Save/Cancel persistence, keyboard focus
7. DPI 100/125/150/200%, keyboard and screen reader focus handling
8. Windows frame dragging, snapping, maximize, system menu, multi-monitor
9. Windows installer setup UI compilation, elevation, maintenance lifecycle
10. Final human visual review against approved concept, not just Qt screenshots

Offscreen Linux validation is *not* a substitute for native Windows acceptance.
Never mark all ten categories complete solely because compilation succeeds.

## Known validation boundaries

The Linux Qt offscreen environment cannot prove Windows native caption behavior,
installer UAC lifecycle, or real assistive-technology navigation. The native
editor-core smoke test has historically timed out on Linux offscreen; do not
misreport that as a regression fixed by visual changes. A separate Windows
runner now exercises native installer compilation and screenshot states, but
Windows desktop testing is still required to approve distribution.
