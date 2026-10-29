# Wolfmark / Servo embedding experiment

This is a **technical UI parity prototype**, not an approved replacement for
the production Qt frontend. It follows Wolfmark's preferred visual structure
on a separately built Servo backend. It is an isolated Cargo workspace
with an entirely separate target directory, while the existing Qt frontend is
still the default for bare Cargo commands.

## Commands (from the Wolfmark repository root)

| Qt (unchanged) | Isolated Servo experiment |
| --- | --- |
| `cargo run` | `cargo servo-test` |
| `cargo run --release` | `cargo servo-test --release` |
| `cargo build` | `cargo build-servo-test` |
| `cargo build --release` | `cargo build-servo-test --release` |
| `cargo check` | `cargo check-servo-test` |

**Cargo does not support** `cargo run --servo-test` or
`cargo build --servo-test` as custom options. The equivalents above use Cargo's
supported alias mechanism. Normal build targets, packaging and `cargo all`
are deliberately untouched. Do **not** add the Servo crate to Wolfmark's main
dependencies or default workspace members during this evaluation.

The test compiles against pinned published `servo = 0.7.0` crates and runs a
Rust/winit window. First-time compilation can be resource-intensive, requires
Servo's native Windows build prerequisites, and downloads crates *at build time*.
See Servo's official [build prerequisites](https://book.servo.org/building/index.html).
The experiment does not require Qt, WebView2, Node.js or a local HTTP server.

## Wolfmark UI parity prototype (October 27)

The Servo command now launches an **initial, read-only Wolfmark shell**, not
the old standalone rendering test. This work is intentionally isolated from
`main` and the normal Qt frontend.

It follows the preferred single-sidebar design: one **Open document** button,
no Open dropdown, no duplicate horizontal document-tab strip; the original
bottom-left Wolfmark artwork is retained. The main toolbar contains Reader,
Live Preview (currently disabled), and Source. The system/winit title bar
remains temporarily native/default: a frameless custom window must not be
introduced until move/resize/snap/caption semantics are implemented and tested
for Windows and Linux.

### Run it on Windows

```powershell
cd C:\Projects\Wolfmark-Servo
git fetch origin
git switch --detach origin/experiment/servo-embedding-poc-20261026
cargo servo-test
# Or open a known local file without a picker:
cargo servo-test -- --document "C:\path\to\document.md"
# The original small engine smoke page is still available:
cargo servo-test -- --smoke-page
```

Use **Open document**, or drag a `.md`, `.markdown` or `.txt` file onto
the application window. Multiple opened documents remain in the left sidebar;
selecting a document restores its Rust-owned read-only buffer. The bundled
architecture example remains available from the first sidebar entry. Reload
re-reads the selected file without discarding other opened documents. Reader and Source switch
read-only presentation modes. Plus/minus changes document text zoom. The
sidebar shows document headings and anchors from Wolfmark's existing Rust
Comrak model. Code blocks use Wolfmark's existing two-face/Syntect classifier.
Local PNG/JPEG/GIF/WebP images are embedded by Rust as `data:` resources,
not accessed over the network or via `file:` requests.

**Prototype limits (not approved production behavior):** source files are
limited to 2 MiB and each local image to 2 MiB; images must be relative and
resolve beneath the document directory (canonical path). HTML is displayed
inertly. External links are inactive. Live Preview editing, text search,
settings, persisted cross-restart sessions, polished clipboard/IME,
image-cache parity, accessibility, custom Windows caption/snap and native-Qt-equivalent smooth scrolling are not yet complete. The native picker uses the OS desktop dialog.
Neither strict offline network isolation nor Android portability is proven.
Avoid comparing memory/performance with production Qt until a release build
and equivalent document/input paths are available.

### Scrolling investigation

The early Servo test forwarded `winit::MouseScrollDelta::LineDelta` values
multiplied by 76 while also declaring `WheelMode::DeltaLine`, which can cause
the recipient to multiply by a line-height factor again. The current probe
normalizes wheel notches to a 48-pixel estimate and sends
`WheelMode::DeltaPixel`; precise trackpad pixel deltas pass through unchanged.
This needs a fresh physical comparison on the same hardware: it is **not yet
proof of smooth-scrolling parity** with the existing Qt frontend.

### Document-navigation parity (October 27 continuation)

The single left sidebar now supports multiple retained in-process documents,
active selection and **Close** controls. Back/Forward navigate through the
Rust-owned document history; closed entries are invalidated and remaining
indexes adjusted. The extra horizontal document-tab strip stays rejected.

Rust/Winit shortcuts in the experiment: **Ctrl+O** Open, **Ctrl+W** Close
current document, **Ctrl+R** Reload, **Ctrl+Tab** and **Ctrl+Shift+Tab**
cycle through the built-in document and opened documents, and **Alt+Left /
Alt+Right** navigate Back/Forward. Actual Windows keyboard behavior still
needs end-to-end user testing; CI only typechecks targets.

The OS window caption shows the selected document, but the title bar is
still the normal native frame. Custom minimize/maximize/snap/resize semantics
must be verified before adopting a frameless Wolfmark title bar.

For local document images, the prototype now uses Wolfmark's authoritative
`src/images/paths.rs` policy instead of its earlier divergent relative-only
resolver, including percent-encoded spaces and explicitly allowed absolute
or `file:` paths. SVG rasterization and full image pipeline/cache parity
remain outstanding; remote image requests are blocked. Brand artwork base64
encoding is cached between document switches.

The experiment imports Wolfmark's existing `src/markdown/model.rs`,
`parser.rs`, `highlight.rs` and `src/presentation.rs` directly from Rust,
without linking the main Qt build script or authoring any C++ bridge.
This is a temporary source-level reuse boundary; a later production migration
would extract the shared Rust core into its own Cargo crate instead of keeping
path imports.

The branch CI typechecks both the executable and Rust unit-test targets.
It does **not** run the graphical UI. Run the full regression tests locally
with `cargo test --manifest-path experiments/servo/Cargo.toml --target-dir
out/servo-cargo/target` when build resources permit.

## October 29 Qt-referenced UI parity pass

The real Qt `main` build was compiled and launched on Debian 13 using Qt 6.8.2
and the approved 1648×928 UI screenshot mode. This is the comparison baseline
for the following changes on the **Servo experimental branch only**:

- The reader no longer has the artificial `max-width:1150px` and centered
  column. Its canvas fills the available viewport, with zoom-aware adaptive
  margins inspired by Qt's `effectiveSideMargin()` function.
- Literal `.txt` files use non-wrapping `pre` text and horizontal scrolling,
  as in Qt's `QTextEdit::NoWrap`.
- The previous footer image was a **Wolfmark wordmark**, not the mountain
  scenery in the approved design reference. A bundled decorative mountain
  SVG now sits behind the separate paw/version identity. The SVG uses no
  scripting, external resource references, or network fetches.
- Discrete mouse-wheel notches feed a Rust critically damped trajectory,
  based on Qt's `SmoothScrollController`; repeated input extends the target
  and reversals preserve the current velocity. Precision touchpad/pixel
  deltas remain direct. `WOLFMARK_REDUCED_MOTION=1` bypasses the animation.
  Winit frames are paced by `ControlFlow::WaitUntil`, without a busy spin.
  Smoothness and CPU usage require physical graphical testing before approval.
- A new **opt-in** frameless UI probe runs with
  `cargo servo-test -- --frameless-test`. This removes OS decorations and
  enables Rust/winit-backed minimize, maximize/restore, close, drag and
  resize-edge actions behind the approved top-bar style. The regular
  `cargo servo-test` still uses native OS window decorations.
  **Do not adopt frameless mode yet:** Windows DWM Snap Layout hover, real
  rounded corners, multiple monitors and per-DPI interaction are unverified.
  Winit's `drag_resize_window` is not supported on X11, so Linux parity
  still needs a native-window strategy; Qt remains the functional baseline.

### Test commands on Windows

```powershell
cd C:\Projects\Wolfmark-Servo
git fetch origin
git switch --detach origin/experiment/servo-embedding-poc-20261026

# Normal native-frame UI, new mountain footer/full-width reader/smooth wheel
cargo servo-test

# Separate, explicitly risky native window frame trial
cargo servo-test -- --frameless-test

# With a chosen local Markdown document
cargo servo-test -- --document "C:\path\to\document.md"

# Original isolated graphics diagnostic
cargo servo-test -- --smoke-page
```

**CI compiles and typechecks the tests; it cannot validate a physical
Windows title bar, present animations, measure scrolling latency or
prove the Servo SVG is actually displayed.** Those are human acceptance
gates on the same laptop that runs the Qt reference.

## Patched Servo paint component (upstream fix)

The first executable launch failed in `glow 0.17` with `GL_VERSION`
because Servo 0.7.0 constructs `glow` before making the WGL OpenGL context
current. We backport Servo's already-merged
[commit 16e4b1591e1f](https://github.com/servo/servo/commit/16e4b1591e1fa4a47016643e34d4f259185c5f2f)
using an isolated `[patch.crates-io]` override of `servo-paint-api`.

The vendored component contains the original v0.7.0 source (MPL-2.0)
with **only that small rendering-context fix**. See
`vendor/servo-paint-api/README.md`. It does not affect Qt,
the normal Cargo workspace or users' system graphics drivers.

Expect Servo dependencies to rebuild **once** after changing this patch,
as a key upstream library changed. Do not `cargo clean` and do not upgrade
hundreds of unrelated crates.

**The fix still requires real Windows rendering validation**. Passing
`cargo check-servo-test` or Windows headless CI is not equivalent
to seeing the test page render.

## Windows OpenGL startup troubleshooting

On a hybrid-GPU Windows laptop, the first successfully **linked and launched**
Servo 0.7 test crashed during `WindowRenderingContext::new()` at
`glow 0.17` while querying `GL_VERSION`. The logs also reported missing
Surfman GPU-selection exports. We now invoke Surfman's upstream
`declare_surfman!()` macro at the executable root to declare these exports,
and print initialization diagnostics.

**This does not establish a runtime fix.** The OpenGL version panic can still
occur if the WGL context is not current when Servo initializes its
`glow::Context`. In the upstream Servo 0.7 implementation, `SurfmanRenderingContext::new`
constructs the `glow` wrapper before explicitly making the newly created
context current; the Surfman Windows context constructor temporarily binds
it and then restores the previous context. If the GPU export fix is insufficient,
we should maintain a small upstream rendering-context patch, not blindly switch
drivers or redesign Wolfmark.

A successful `cargo check-servo-test` validates compilation only. CI on
headless Windows cannot prove a graphics window runs on a hybrid-GPU laptop.

## Windows SpiderMonkey build notes

The initial Oct 26 Windows CI and laptop checks failed inside
`mozjs_sys 153.3.0-1` before reaching the Wolfmark test crate. Upstream's
Windows build script assumed that the Cargo output tree includes a directory
literally named `target`, but this experiment initially used
`out/servo-cargo`. On reaching the filesystem root, upstream
`cargo_target_dir()` panicked at `build.rs:725`.

The separate target directory is now `out/servo-cargo/target`. The Servo
crate also enables `js_jit` (one of upstream Servo 0.7's default features),
allowing `mozjs_sys` to try its prebuilt SpiderMonkey archive instead of
forcing source compilation merely because JIT was disabled.

A build-time archive download is permitted; the *installed application* must
remain fully offline. If the archive is unavailable, SpiderMonkey may fall
back to compiling from source. That requires MozTools 4.0, Clang/LLVM,
MSVC C++ build tools/SDK and Python, as detailed in
[Servo's mozjs Windows prerequisites](https://github.com/servo/mozjs#building).
These are developer/build-machine dependencies, not proposed requirements
for Wolfmark end users.

**Existing Windows build cache:** if you previously ran the experiment with
`out/servo-cargo`, the corrected target path starts a separate cache. The
old cache may safely be deleted later after build validation; don't delete
any Qt cache under `out/cargo`. Normal Cargo still builds Qt.

`available: vX` lines in Cargo output are version notices, not compilation
errors. Do not mass-update Servo's pinned transitive dependencies just to
silence these notices.

## What this first probe actually tests

- Servo initialization in an independent Windows-hosted Rust binary.
- An inline HTML/CSS document compiled **inside the Rust source**, loaded via
  `data:` rather than from localhost, a network address or a loose file.
- Text/layout, sample table, preformatted text, internal anchor, long scrolling,
  resize, mouse wheel and basic mouse selection (experimental).
- Mouse movement, clicks, wheel scrolling, window resizing and DPI events are
  forwarded through winit.
- Basic keyboard events and modifiers now forward to Servo for testing input,
  text selection and keyboard scrolling. The page includes a local input and
  textarea plus a tiny inline PNG, to test input and image layout without
  loose files or external resources.
- **Keyboard, clipboard and editing are NOT validated end to end.** Keyboard
  mapping is incomplete, IME/dead-key composition is not implemented and text
  typed into the test textarea is not persisted as a Wolfmark document.

The test does **not** implement Markdown conversion, the approved Wolfmark
interface, document editing, image pipeline, saving, accessibility or Android.
It does not prove performance parity or acceptable package size.

## Offline-only safety status

- The embedded test document contains a restrictive CSP and no remote assets.
- Main-frame navigation to anything other than a `data:` URL is denied.
- Webview-associated HTTP(S) requests are intercepted and refused.
- Webview permission prompts are denied.
- **This is not yet a complete verified network sandbox.** Engine background
  requests, service-worker-associated loads, DNS, origin behavior and all
  unassociated requests need controlled network monitoring and fail-closed
  policies before Servo could be considered for Wolfmark production.

The future official GitHub updater is the only possible Wolfmark network
exception, handled by the dedicated Rust update subsystem—not the webview.

## Embedded page load regression (October 26)

The first Windows runtime after the upstream OpenGL fix reached
`Servo OpenGL window context is current.`, but the offline
`WebViewDelegate::load_web_resource` callback also received the
application's **data:text/html** startup URL. The old handler
intercepted *every* callback with HTTP 403, preventing the page itself
from rendering and printing the enormous percent-encoded HTML URL.

The probe now leaves `data:` resources unintercepted, allowing Servo
to process the bundled HTML and PNG normally. All other resource
loads observed by this callback continue to receive a forbidden
response; logs now print the scheme rather than a full URL.
Regression unit tests cover embedded data versus remote and file URLs.

This fixes the known logical load blocker but **still needs a real
graphical runtime test** on Windows. The callback is not a complete
network sandbox: unaffiliated background loads and other engine
paths need auditing before production integration.

## Quick Windows test checklist

1. From the experiment worktree, run `cargo check-servo-test`. It may
   download many Servo crates and need Clang and other native prerequisites.
2. If compilation succeeds, run `cargo servo-test`. A normal resizable
   desktop window should render a locally embedded page.
3. Scroll with wheel and PageDown, drag across two paragraphs to inspect
   selection, Tab into the input and textarea and type. Try Ctrl+A/C/V, but
   record any clipboard failures rather than assuming support.
4. Click the external-navigation test link. The page must **not navigate**
   and the console should report the blocked URL.
5. Disconnect the network and repeat. Separately monitor traffic with an
   OS-level network tool; lack of page navigation alone does not prove
   total offline isolation.
6. Record CPU, peak/idle memory, startup, resize and scroll behavior.
   Compare with `cargo run --release` for the unchanged Qt application.

The optional branch-only GitHub Actions workflow performs a Windows
`cargo check-servo-test` attempt and uploads the resolved experiment
`Cargo.lock` as an artifact. A successful compile check is not a rendered
window test. The uploaded Rust/Cargo Linux and original Wolfmark Cargo
registry archives do not contain Servo's dependency set, so they alone
cannot validate this test offline.

## Prerequisites before claiming Servo passes

1. Verify `cargo check-servo-test` and `cargo servo-test` on Windows with the
   required Servo build toolchain and record any compiler/SDK errors.
2. Check memory/CPU/startup, scrolling, selection and process behavior.
3. Test with networking disconnected and with OS-level network monitoring.
4. Compare against equivalent `cargo run --release` Qt baselines on the same PC.
5. Extend the test to Wolfmark's Comrak presentations, heavy image fixtures,
   keyboard/clipboard, accessibility and editor behaviors.
6. Evaluate Linux and Android platform integration separately.

Do not merge an implementation based on this probe alone. Preserving Wolfmark's
locked appearance, features, file integrity, lightweight distribution, and
Windows/Linux/Android roadmap is required.

## Licensing

This isolated crate adapts Servo's v0.7.0
`components/servo/examples/winit_minimal.rs` (Mozilla Public License 2.0).
The adapted file retains that license, independently of Wolfmark's
GPL-3.0-only core. A dependency/source-license audit is mandatory before
any production integration.
