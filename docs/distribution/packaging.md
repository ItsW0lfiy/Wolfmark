# Windows packaging

`scripts/package_windows.ps1` is the release-grade Windows packaging entry point:

```powershell
pwsh -File scripts/package_windows.ps1
```

It reads the version from Cargo metadata, builds Release, stages an ignored `out/package/staging/<version>/Moonmark` folder, smoke-tests that app-local payload with Qt discovery variables cleared and `PATH` limited to Windows system directories, then writes the release artifacts to `out/release/<version>/`.

The current engine-independent artifacts are:

- `Moonmark-Setup-win-x64.exe`
- `Moonmark-portable-win-x64.zip`
- `SHA256SUMS.txt`

Inno Setup 7.1.0 is the approved dev.7 installer engine. Prepare the verified project-local compiler when no compatible compiler is installed:

```powershell
pwsh -File scripts/bootstrap_inno.ps1
```

The bootstrap downloads the official signed Inno installer into ignored `out/cache/inno`, verifies its pinned SHA-256 and Authenticode signer, and extracts the compiler under `out/toolchains/inno`. It does not install Inno globally. Packaging discovers that compiler automatically; `-InnoCompiler` or `MOONMARK_INNO_ISCC` may select another compiler explicitly. Use `-SkipInstaller` only for a deliberate portable-only build.

The portable folder contains:

- `Moonmark.exe`
- dynamically linked Qt6Core, Qt6Gui, Qt6Widgets, and Qt6Network
- the Windows QPA plugin at `platforms/qwindows.dll`
- app-local MSVC runtime DLLs when the Visual Studio redistributable directory is available
- approved symbol artwork (the application, shell, and EXE icon are embedded and do not depend on this file)
- `qt.conf`
- Qt LGPL/GPL texts and Moonmark's third-party notice

This ZIP is the current practical portable architecture. It needs no .NET, JVM, Node.js, browser engine, or separately installed Qt. It uses normal Windows system libraries; including app-local MSVC CRT DLLs avoids asking users to install the Visual C++ Redistributable separately.

The Qt SDK, Rust toolchain, Cargo, MSVC compiler, Windows SDK, `vswhere.exe`, and Inno compiler are build-time requirements only. `vswhere` locates installed C++ toolchains and the newest app-local x64 VC runtime instead of relying on a hardcoded Visual Studio edition path. Missing Qt, platform-plugin, CRT, or installer compiler files fail packaging instead of silently producing a client-dependent artifact. Normal users receive the executable, exact Qt DLLs/plugins, app-local CRT, assets, notices, and license texts. The portable edition runs directly from its extracted directory; the installer deploys the same staged payload rather than bootstrap a development SDK or language runtime.

The setup defaults to `{autopf}\Moonmark` (normally 64-bit Program Files), creates an Installed Apps/uninstall entry and Start Menu shortcut, offers an optional Desktop shortcut, and offers Moonmark's Open With registration for `.md`, `.markdown`, and `.txt`. It does not write the protected Windows `UserChoice` default. Markdown and text handlers use their own Moonmark document icons. A stable AppId keeps upgrades in one Installed Apps entry. Uninstall removes Moonmark-owned files, shortcuts, and registration but not user documents or settings.

The installed build stores typed settings under the platform application-config location. The portable ZIP contains `portable.flag`; that marker selects `data/settings.json` beside the executable without changing the application binary. See [update delivery and settings](updates.md) for the schema and network/update contract.

For unattended deployment and later WinGet validation:

```powershell
Moonmark-Setup-win-x64.exe /SP- /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
"%ProgramFiles%\Moonmark\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
```

Use `/TASKS="fileassoc,desktopicon"` to select both optional tasks explicitly. The installer normally requests administrative elevation because its default target is Program Files. The unsigned development installer may trigger Windows reputation warnings.

The contained lifecycle test uses Inno's command-line privilege override to install under project-local `out/tests/installer`, while exercising the same payload, AppId, shortcuts, and HKCU-equivalent shell registration:

```powershell
pwsh -File scripts/test_windows_installer.ps1
```

It refuses to overwrite an existing current-user Moonmark installation or shortcut, generates and cleans its own spaced/Unicode test documents, verifies an older local build upgrades to the current version without duplicate Installed Apps entries, launches the installed app, then silently uninstalls and checks cleanup. It does not replace final physical testing of the elevated Program Files path or Windows Explorer UI.

A true single executable requires a separate static Qt build and a deliberate Qt licensing decision. It was not built. Under LGPLv3, static distribution adds relinking/application-object and installation-information obligations and may affect whether the application remains merely a work using the library. Dynamic Qt is the safer current packaging choice; legal review and Moonmark's own license decision remain required before public distribution.

The final local dev.7 measurement is 6,705,664 bytes (6.40 MiB) for `Moonmark.exe`, 37,378,626 bytes (35.65 MiB) for the complete portable folder, 16,660,564 bytes (15.89 MiB) for the ZIP, and 13,884,733 bytes (13.24 MiB) for the Inno setup. Exact SHA-256 values are written with each rebuild under `out/release/0.1.0-dev.7/`; do not copy stale hashes into documentation. The remaining folder bytes are Qt, the Windows platform plugin, app-local MSVC CRT, branding, and legal/support files. The packaged smoke passed with only normal Windows system paths visible. This is not yet a clean-VM or minimum-Windows-version certification.
