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

## What this first probe actually tests

- Servo initialization in an independent Windows-hosted Rust binary.
- An inline HTML/CSS document compiled **inside the Rust source**, loaded via
  `data:` rather than from localhost, a network address or a loose file.
- Text/layout, sample table, preformatted text, internal anchor, long scrolling,
  resize, mouse wheel and basic mouse selection (experimental).
- Mouse events are passed through winit. **Keyboard/clipboard and real editing
  are not implemented** in this smoke build.

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

## Prerequisites before claiming Servo passes

1. Run `cargo check-servo-test` and `cargo servo-test` on Windows with the
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
