# Moonmark

![Moonmark approved logo](assets/branding/moonmark-logo.png)

Moonmark is a Windows-first, viewer-first Markdown application. It is one native process built with Rust, C++20, Qt 6 Widgets, and QTextDocument. Linux is the secondary desktop target; Android remains later work. Moonmark contains no browser engine, web frontend, local server, CLR, JVM, or Node.js runtime.

Current development milestone: `0.1.0-dev.7` — First Windows Release & Shell Integration.

## Architecture

```text
Markdown source
  -> Rust / Comrak semantic model
  -> Rust framework-neutral presentation commands
  -> versioned C ABI
  -> C++ / Qt Widgets adapter
  -> native QTextDocument
```

Rust owns file loading, Comrak parsing, semantic/presentation models, syntax classification, image policy/decoding/cache, diagnostics, and framework-neutral window state. C++ owns the Qt widget shell, native QTextDocument construction, selection/clipboard interaction, document layout, and Windows presentation integration. The boundary transfers UTF-8 JSON, IDs, owned byte buffers, counters, and function pointers; it does not use a second process or network IPC.

The New Moon presentation uses a collapsible file-first sidebar with Open/Reload, compact open-document entries, and the active document's native heading outline. A quiet breadcrumb header with Windows captions frames a broad document canvas. Tables have graphite headers and restrained horizontal/vertical separators; inline code has subtle graphite backgrounds; fences remain one coherent native frame. True percentage zoom scales native document layout while retaining selection and decoded images. Syntax colors remain restrained and non-blue.

Moonmark is file-oriented rather than vault-oriented: it reads an ordinary Markdown file and releases the read handle. Relative images resolve from that document; valid parent, absolute, and `file:///` paths are allowed after canonicalization. Remote images remain disabled.

Dev.6 retains one complete Rust backend and native `QTextDocument` per open file. Switching therefore preserves scroll, zoom, selection, outline, watcher, and decoded image state without rereading or reparsing. Canonically identical paths activate the existing session. Literal `.txt` files bypass Comrak and Markdown presentation entirely.

Mouse-wheel notches feed one continuous elapsed-time trajectory that accelerates under repeated input and reverses without restarting an easing animation. Precision touchpad/pixel input, scrollbar dragging, keyboard paging/Home/End, outline activation, and document anchors remain direct. Image prefetch and delivery are coalesced away from rapid paint cadence. Code blocks and tables use quieter graphite framing while retaining native selection and accessibility.

## Build and run

On Windows, install Rust 1.94+ with the MSVC target and a C++20 MSVC toolchain. Then either set `MOONMARK_QT_DIR`/`QTDIR` to a Qt 6 Widgets SDK or bootstrap the tested project-local Qt 6.11.2 SDK:

```powershell
pwsh -File scripts/bootstrap_qt.ps1
cargo run
cargo run -- fixtures\moonmark-visual-test.md
cargo run -- fixtures\moonmark-visual-test.md fixtures\text\literal.txt
cargo check
cargo test
cargo build --release
```

Cargo compiles the C++ adapter and stages the required dynamic Qt libraries. It does not invoke CMake, `dotnet`, NuGet, Node.js, or a browser toolchain.

Generate repeatable stress inputs and run the headless Rust benchmark with:

```powershell
cargo run --bin generate_stress_fixture
cargo run --bin generate_text_fixture
cargo run --release --bin renderer_benchmark -- fixtures\generated\image-stress.md
```

Assemble the measured Windows setup executable, portable ZIP, and checksums with:

```powershell
pwsh -File scripts/bootstrap_inno.ps1
pwsh -File scripts/package_windows.ps1
```

See the [documentation index](docs/README.md), [architecture](docs/architecture/overview.md), [renderer](docs/architecture/renderer.md), [images](docs/architecture/images.md), [window behavior](docs/architecture/windowing.md), [building](docs/development/building.md), [packaging](docs/distribution/packaging.md), [updates](docs/distribution/updates.md), and [Qt licensing](docs/distribution/qt-licensing.md).

Moonmark source is licensed under [GPL-3.0-only](LICENSE). Official branding is covered separately by [BRANDING.md](BRANDING.md). Review the [dependency license audit](docs/licenses/dependency-audit.md), [third-party notices](THIRD_PARTY_NOTICES.txt), and [roadmap](ROADMAP.md) before distribution work.
