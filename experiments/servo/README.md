# Wolfmark / Servo embedding experiment

This is a **technical probe**, not a new Wolfmark frontend. It deliberately does
not copy or replace the locked Wolfmark design. It is an isolated Cargo workspace
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
