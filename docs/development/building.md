# Building

## Quick start

After installing Rust/MSVC, PowerShell 7, and the Visual Studio C++ x64 workload with a Windows SDK:

```powershell
cargo setup
cargo app
```

Create the Windows setup executable, portable ZIP, and checksums with:

```powershell
cargo package-app
```

Cargo aliases provide the normal Moonmark workflow:

| Command | Purpose |
| --- | --- |
| `cargo app` | Build Moonmark in Release mode and launch it. Arguments may follow `--`, as with `cargo app -- README.md`. |
| `cargo build-app` | Build the Release application without launching. |
| `cargo setup` | Verify MSVC and prepare the project-local Qt and Inno Setup tools only when missing. |
| `cargo package-app` | Build the final installer, portable ZIP, and `SHA256SUMS.txt`. |
| `cargo clean-app` | Remove normal generated output under `out/`. |
| `cargo deep-clean` | Also remove project-local downloaded/bootstrap toolchains and caches. |

Cargo's built-in `cargo package` command remains unchanged; Moonmark uses `cargo package-app` because Cargo aliases cannot override built-in commands.

## Toolchain

Tested on Windows x64 with:

- Rust 1.94+ MSVC toolchain
- a C++20-capable MSVC compiler
- Qt 6.11.2 Core, Gui, Widgets, and Network development files
- Windows SDK resource compiler for the executable icon
- PowerShell 7 for the optional bootstrap/package scripts

No .NET SDK/runtime, C#, Avalonia, Node.js, browser engine, CMake, or qmake invocation is part of the normal build. The discovered Qt SDK's qmake executable is queried only as an optional SDK-location fallback.

## Cargo workflow

```powershell
cargo fmt --all --check
cargo check
cargo run
cargo run -- fixtures\moonmark-visual-test.md
cargo test
cargo clippy --all-targets --all-features -- -D warnings
cargo build --release
```

`build.rs` discovers Qt, compiles the C++20 adapter with warnings enabled, links Qt dynamically, embeds the Windows icon, and stages Qt DLLs/plugins/assets beside Cargo's executable. Build failure on either language fails Cargo.

Moonmark development milestones use prerelease versions such as `0.1.0-dev.4` through `0.1.0-dev.7`. During milestone work, each coherent source, UI, test, or documentation change receives its own descriptive commit before unrelated work begins. Commits are not squashed merely to shorten history, and pushing still requires separate explicit user approval.

## Advanced / internal tooling

The Cargo aliases delegate to small PowerShell scripts; normal development does not require invoking them directly. They remain available for diagnosing or customizing an individual stage:

```powershell
pwsh -File scripts/bootstrap_qt.ps1
pwsh -File scripts/bootstrap_inno.ps1
pwsh -File scripts/package_windows.ps1
pwsh -File scripts/clean.ps1
pwsh -File scripts/clean.ps1 -Deep
```

The Qt bootstrap downloads the tested Qt 6.11.2 MSVC2022 x64 archive to ignored project-local storage and verifies Qt's published SHA-1. The Inno bootstrap verifies its pinned SHA-256 and Authenticode signer. Neither tool is installed globally.

See [Windows packaging](../distribution/packaging.md) for prerequisites, staging, app-local runtime policy, artifact paths, installer lifecycle validation, unattended switches, and checksums.

## Fixtures and benchmarks

```powershell
cargo run --bin generate_stress_fixture
cargo run --release --bin renderer_benchmark -- fixtures\generated\image-stress.md
cargo run --release --bin renderer_benchmark -- fixtures\generated\large-text.md
```

Generated fixtures are ignored. Native integration smoke tests are part of `cargo test` on Windows.

For a deterministic rendered client-area snapshot during UI review:

```powershell
cargo run -- fixtures\moonmark-visual-test.md --smoke-snapshot
```

Ignored snapshots are written below `out/visual/dev7/`. Run `pwsh -File tools/visual_snapshots.ps1` after a Debug build and fixture generation to capture inline code/wrapping/selection, image-gap regression, empty/prose/headings/tables/code/images/plain text/narrow/wide, sidebar, menu, and 80/100/125/150% zoom cases. Optional process variables `MOONMARK_SNAPSHOT_NAME`, `MOONMARK_SNAPSHOT_WIDTH`, `MOONMARK_SNAPSHOT_HEIGHT`, and `MOONMARK_SNAPSHOT_SCROLL` select the filename, client size, and vertical offset. `MOONMARK_SNAPSHOT_ZOOM` sets actual document zoom; `MOONMARK_SNAPSHOT_NO_SIDEBAR=1` collapses navigation. `MOONMARK_SNAPSHOT_MENU=1` captures the real menu, and `MOONMARK_SNAPSHOT_SELECTION=1` selects the native document before capture. Smoke sessions use reduced motion and put QSettings in project-local `out/tests/native-settings`, not the user's application settings. They supplement rather than replace physical taskbar, Alt+Tab, DPI, touchpad, and multi-monitor checks.

Focused dev.6 checks:

```powershell
cargo run -- --smoke-plaintext fixtures\text\literal.txt
cargo run -- --smoke-multidoc fixtures\code-block-quality.md fixtures\text\literal.txt
cargo run -- --smoke-motion fixtures\compatibility\all-features.md
cargo run -- --smoke-navigation fixtures\compatibility\all-features.md
```

The native test suite also creates temporary Markdown/plain-text files to verify that a background document's file watcher reloads only its own retained session.

After a Release build, `pwsh -File tools/render_performance.ps1` runs native geometry, zoom, image-stress, and rapid-scroll profiles for large text, image-heavy, and mixed-feature documents. Set process variable `MOONMARK_PROFILE=1` for format/layout, worker queue/decode, and Qt delivery phase timings. `interactive_us` measures the zoom action; `painted_us` includes synchronous viewport repaint; `elapsed_us` additionally includes exhaustive format/text verification. Scroll profiles report actual viewport paint intervals and hot-path phase costs, not only controller positions. Compare like-for-like Release runs without concurrent builds. The image `completion_ms` excludes the subsequent zoom verification.

## Linux

The C++ adapter is mostly cross-platform and the Win32 sections are guarded. On Linux, `build.rs` discovers Qt6Widgets and Qt6Network with their transitive Qt modules through `pkg-config`; install compatible Qt 6 Widgets/Network development packages and `pkg-config`. This path is architecturally wired but has not been compiled on a physical Linux host. A Linux milestone must validate compilation/linking, package the platform plugin and system dependencies, and verify AT-SPI/accessibility behavior.
