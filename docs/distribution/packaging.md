# Windows packaging

Cargo is the normal Windows packaging entry point:

```powershell
cargo setup
cargo package-app
```

`cargo setup` verifies MSVC/Windows SDK, prepares Qt, and restores the explicitly approved WiX Toolset 7.0.0 CLI plus locked native bootstrapper packages to ignored project-local `out/` directories. `cargo package-app` delegates to `scripts/package_windows.ps1`, builds Release, stages one app-local Wolfmark payload, smoke-tests it with only normal Windows system paths available, and creates:

- `Wolfmark-Setup-win-x64.exe`
- `Wolfmark-win-x64.msi`
- `Wolfmark-portable-win-x64.zip`
- `SHA256SUMS.txt`

The exact output directory is `out/release/<version>/`. WiX symbol/intermediate files remain below `out/package/wix`; they are not release artifacts.

The shared application payload contains `Wolfmark.exe`, dynamically linked Qt6 Core/Gui/Widgets/Network, `platforms/qwindows.dll`, app-local MSVC runtime DLLs, icons/branding, `qt.conf`, Wolfmark's license/third-party notice, and Qt license texts. The portable archive adds `portable.flag`; installed and portable editions do not use unrelated application builds.

The normal setup EXE embeds a genuine per-machine x64 MSI. Its native C++20/Qt Widgets bootstrapper presents install, update, maintenance, modify, repair, uninstall, progress, completion, and error flows while Burn/MSI owns detection, elevation, transactions, rollback, and cached uninstall behavior. Direct interactive MSI launch uses the same presentation through a native Windows Installer Embedded UI; it does not fall back to a generic WiX wizard. The MSI installs under 64-bit Program Files, appears in Installed Apps, creates the Start Menu shortcut, optionally creates the Desktop shortcut, and conditionally registers `.md`, `.markdown`, and `.txt` Open With handlers without changing protected Windows `UserChoice` defaults.

The Qt SDK, Rust/Cargo, MSVC/Windows SDK, PowerShell 7, .NET SDK, NuGet, and WiX are build-time requirements only. The .NET SDK exists solely to restore the WiX CLI/native API packages; Wolfmark's setup UI and shipped application are native. End users do not need Rust, Qt, WiX, .NET, Node.js, a browser runtime, or developer tools installed. Both installed and portable packages use app-local Qt and CRT files plus ordinary Windows system DLLs.

## Quiet deployment and lifecycle checks

Burn and MSI provide conventional unattended entry points:

```powershell
Wolfmark-Setup-win-x64.exe /quiet /norestart
Wolfmark-Setup-win-x64.exe /uninstall /quiet /norestart

msiexec.exe /i Wolfmark-win-x64.msi /qn /norestart
msiexec.exe /x Wolfmark-win-x64.msi /qn /norestart
```

The custom bootstrapper does not implement a background updater or silent application-owned install policy; these are ordinary administrator-invoked setup switches for deployment/testing. The unsigned development artifacts may trigger Windows reputation warnings.

Run the non-invasive package audit with:

```powershell
pwsh -File scripts/test_windows_installer.ps1
```

To exercise the actual packaged interactive startup/cancel paths without installing:

```powershell
pwsh -File scripts/test_windows_installer.ps1 -InteractiveLaunchSmoke
```

It verifies all artifact hashes, decompiles/audits the MSI authoring, and launches the portable smoke with a restricted path. The optional elevated lifecycle path is:

```powershell
pwsh -File scripts/test_windows_installer.ps1 -ExecuteLifecycle
```

It builds an older compatible MSI and performs isolated old-MSI install, Setup-EXE upgrade, same-version setup, bundle repair/uninstall, direct-MSI install/repair/modify/uninstall, Installed Apps uniqueness, installed launch, and user-document preservation checks. The script refuses to replace an existing Wolfmark installation unless the operator explicitly supplies `-AllowExistingWolfmarkReplacement`. This safeguard matters on developer machines; clean-VM validation remains the preferred release gate.

Installer state screenshots can be regenerated without installing anything:

```powershell
pwsh -File scripts/capture_wix_installer_ui.ps1
```

The screenshots land under ignored `out/visual/wix-setup/` and cover install, upgrade, maintenance, repair, uninstall, progress, completion, and error states. They supplement rather than replace the actual packaged launch smoke or physical keyboard/accessibility, DPI, UAC, Explorer, upgrade, and rollback checks.

A true single executable would require a separate static Qt build and deliberate Qt licensing work; dev.7 intentionally keeps dynamic Qt. Exact sizes and hashes are emitted by every package run rather than copied into this document. Re-check WiX 7 OSMF terms and the release-specific Qt/license audit before publication.
