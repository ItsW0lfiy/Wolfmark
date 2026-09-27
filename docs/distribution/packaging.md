# Windows packaging

From a normal checkout:

```powershell
cargo setup
cargo package-app
```

`cargo setup` verifies MSVC/Windows SDK, prepares project-local Qt, and downloads the pinned NSIS 3.12 portable compiler only when missing. The archive is verified against its locked SHA-256. `cargo package-app` builds the Rust application and maintenance core, stages one app-local payload, generates its canonical file manifest, builds the shared native Qt setup host, compiles the NSIS bootstrap, runs the packaged application smoke, creates the portable ZIP, and hashes the final bytes.

Output is under `out/release/<version>/`:

```text
Wolfmark-Setup-win-x64.exe
Wolfmark-portable-win-x64.zip
SHA256SUMS.txt
```

No MSI is produced. NSIS/setup intermediates remain under `out/package/nsis`; the canonical staged payload remains under `out/package/staging`.

Official packaging requires a clean Git worktree. For local migration/debug builds only:

```powershell
cargo package-app -- -AllowDirty
```

That output records `<12-char-commit>-dirty` and must not be published as an official artifact.

The Qt SDK, Rust/Cargo, MSVC/Windows SDK, PowerShell 7, and NSIS are build-time requirements only. End users do not need them. Neither the installed nor portable application contains .NET/CLR, JVM, Node.js, Electron, WebView2, Chromium, or Qt WebEngine.

Non-invasive artifact validation:

```powershell
pwsh -File scripts/test_windows_setup.ps1
```

Elevated lifecycle validation on a clean VM:

```powershell
pwsh -File scripts/test_windows_setup.ps1 -ExecuteLifecycle
```

Capture the shared installer UI states without installing:

```powershell
pwsh -File scripts/capture_nsis_installer_ui.ps1
```

Exact sizes and hashes are emitted by every package run; do not copy stale values into documentation.
