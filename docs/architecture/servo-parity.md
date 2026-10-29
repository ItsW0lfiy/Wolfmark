# Wolfmark Servo migration — implementation and acceptance ledger

**Target branch:** `codex/servo-migration-20261029`. **Production:** Qt remains default on `main`; migration is not yet accepted. A successful Cargo compile is only the first gate. Always inspect the actual implementation and current CI before marking features complete.

## UI and functional comparison

| Feature | Servo migration implementation | Outstanding Qt parity / verification |
| --- | --- | --- |
| Approved shell | Dark graphite/crimson title bar, compact toolbar, footer mountain art, sole sidebar document selector, no rejected dropdown or horizontal tabs | Screenshot comparison at 1648×928 and varied DPI, hover/focus details |
| Native custom window | Windows frameless by default; Rust winit minimize/maximize/close, movement/resizing; Win32 subclass for native Snap maximize hit and rounded DWM corners. `--native-frame` fallback. X11/Wayland still use native frame by default | Physical Windows Snap Layout hover, DPI, taskbar and system menu; Linux custom frame |
| Reader | Shared Comrak parser and syntax-highlighted code, full-width Reader, tables, images and outline, zoom | Qt typography details, rich document fixtures, layout/reflow anchors, copy-selection tests |
| Local images | Shared Rust path policy and decode pipeline, SVG rasterization, bounded worker pool/cache, intercepted in-memory resources; no external image requests | Animated GIF/WebP playback and more image lifecycle tests |
| Open documents | Native picker, remembered directory, drop, retained sessions, individual zoom/mode/scroll, close, history, file conflict checks | OS association startup matrix, external file watch and exact scroll anchor reflow |
| Source | Shared Rust editor buffer, BOM/line endings, dirty/save/save-as/conflict/undo/redo, syntax mirror, line gutter, line/column state | Clipboard, IME and complex selection/indent semantics in GUI |
| Live Preview | Shared edit buffer plus Comrak contextual decorations, inline syntax mirror, editable textarea, gutter and mode switch | **Still substantial:** full Qt cursor-sensitive marker minimization, rich paragraph heading sizing, grapheme/IME editing and layout without caret drift |
| Outline | Reader anchors; editable Source/Live headings route to source line/caret | Smooth caret positioning and source editor viewport tests |
| Search | Rust search state plus Servo in-document selection and navigation | Full find-highlighting/per-document search state and actual GUI keyboard tests |
| Scrolling | Qt-like Rust damped wheel, precision trackpad bypass, middle autoscroll and fixed indicator, reduced-motion bypass, fullscreen document | Physical responsiveness, rapid reversal, image reflow bounds, idle CPU |
| Settings | Qt-compatible JSON preferences with portable and recovery support; Rust-owned Save/Cancel, Updates/Editor/About, word wrap, last-used folder, focus trap; overlays without reloading Reader | Verify config paths on both platforms, focus/accessible state/snapshot |
| Updates | Bound-only GitHub Releases check on background thread, fixed canonical HTTPS endpoint, no redirects, bounded metadata, manual check, startup preference | Qt ETag/6h cache, verified release asset downloads and installer handoff |
| Security | Resource delegate blocks remote document resources/background traffic, document Markdown escaped and external navigation denied | Full network traffic audit, navigation and protocol exploit tests |
| Distribution | Nested Cargo workspace preserves Qt default and opt-in `cargo servo-test` | Windows packaging/installer, Linux packages and deployment, Android embedding |

## Current verification policy

- CI: `.github/workflows/servo-experiment.yml` typechecks Servo on Windows, checks that the default Cargo workspace remains Qt, runs standalone motion-controller tests, and caches the dependency graph.
- CI does **not** prove a rendered GUI, Qt parity, safe drag/resize behavior or release-ready installation.
- User acceptance testing begins only **after** the UI integration is sufficiently complete. Development should continue with automation and source-level comparison without repeatedly requesting manual tests.
- Windows, Debian Linux, and Android support must be distinguished. Android is **not yet implemented**.
- No project-authored C++, WebView2, Electron, JavaScript application framework, localhost server, or remote frontend is permitted.
- `main` remains untouched. Never merge or delete Qt before explicit acceptance.

## Next uncompleted gates

1. Finish Qt rich/contextual Live Preview parity and reliable editor behavior across Source/Live.
2. Fix all Windows compile/test failures and add actual graphical smoke tests/screenshot comparisons.
3. Verify native Windows Snap/Layout/window resize/restore semantics and Linux frame/input behavior.
4. Finish GitHub update/download verification and portable/installer packaging.
5. Validate visual parity, performance, resource constraints and offline security before merging.
