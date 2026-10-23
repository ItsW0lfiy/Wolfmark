# Crimson Graphite — implemented design rules and acceptance

This working specification describes the October 2026 redesign of Wolfmark's
native Qt desktop UI. It is a design implementation document, not a new product
architecture or permission to fabricate controls absent from the application.
The user-approved concept is the visual target. The canonical product icon and
file icons in `assets/design-reference/wolfmark/` remain authoritative.

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

- Graphite titlebar: 48 logical pixels, dynamic breadcrumb, interactive Find,
  Settings, and real native caption controls.
- Sidebar: 240 logical pixels; one real Open action, Reload, open documents,
  outline navigation, and a restrained footer. Only real files are listed.
- Tabs: multi-document strip with real close and new-document actions; no
  decorative blank tabs. Single-document sessions remain tabless by design.
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
The plus control opens the real file chooser. There are no imaginary vaults,
folders, cloud searches, accounts, bookmarks, or paid features.

## Verification matrix

1. `cargo check -p wolfmark --locked --offline` / `cargo test -p wolfmark`
2. `cargo build -p wolfmark`, real Qt offscreen Reader/Live/Source screenshots
3. Shell-mode interaction smoke and multi-document title/tab checks
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
