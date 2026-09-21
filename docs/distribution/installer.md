# Windows installer — dev.7

Wolfmark dev.7 uses **WiX Toolset 7.0.0** to produce a genuine per-machine x64 MSI and a WiX Burn setup bundle. WiX was explicitly approved for this milestone. Both interactive entry points use the shared native C++20/Qt Widgets setup presentation; they contain no C#, CLR, browser engine, or managed client runtime.

The Burn UI follows the locked Wolfmark identity: an approved paw brand rail, charcoal/graphite surfaces, silver typography, restrained crimson primary/focus/progress states, and the same compact one-screen operational flow for install, update, maintenance, repair, uninstall, completion, and failure. The reference mockup informs the visual language, not a fictional wizard or feature set.

The generated artifacts are:

- `Wolfmark-Setup-win-x64.exe` — the normal user-facing Burn bundle;
- `Wolfmark-win-x64.msi` — the genuine Windows Installer package for administration and deployment;
- `Wolfmark-portable-win-x64.zip` — the same application payload with portable mode enabled;
- `SHA256SUMS.txt` — hashes of all three distributables.

The MSI owns Program Files deployment, Installed Apps metadata, Start Menu and optional Desktop shortcuts, Open With registration, repair, modify, major upgrades, and uninstall. Installer identities are tracked in `packaging/windows/identity.json` and follow these rules:

- MSI UpgradeCode `{24F5627C-0519-4BC3-9C73-4DEBD580BA11}` is permanent for the Wolfmark product family.
- MSI ProductCode is stable for every rebuild of one published display version. It changes only when a later Wolfmark version is authored as a major upgrade. The `0.1.0-dev.7` ProductCode is `{FF340EB2-370A-474F-A6B8-4E78AFAE8952}`.
- MSI PackageCode remains WiX-generated and changes whenever a new MSI package is built. Two nonidentical MSI files therefore do not share a PackageCode.
- Burn UpgradeCode `{84457DCA-5666-4684-92DF-9E7677C285FD}` is permanent for the Wolfmark setup family.
- Burn bundle registration Code identifies one built bundle and may change. The explicit provider key `ItsW0lfiy.Wolfmark.Windows.x64` persists across compatible setup upgrades.

Burn maintenance is offered only when `WixBundleInstalled` confirms that the running bundle is itself registered. A related bundle or MSI is an update candidate, not proof that the current bundle owns maintenance. Direct MSI maintenance similarly checks the exact ProductCode first, then enumerates the UpgradeCode family to distinguish older, same-version, and newer related products. This prevents an unrelated setup from reporting a successful no-op uninstall.

The unreleased Moonmark development installer used different identities and is intentionally not part of Wolfmark's upgrade family. Earlier unreleased Wolfmark dev.7 builds may also have random per-build ProductCodes. Current installers detect those products through the stable UpgradeCode and migrate them as related products. If a stale development install interferes with testing, enumerate its ProductCode first and remove it through Windows Installed Apps or `msiexec.exe /x {PRODUCT-CODE}`; do not delete Program Files or registry entries by hand. Existing local Moonmark development installs remain untouched.

The custom bootstrapper offers install, update, maintenance, modify, repair, uninstall confirmation, progress, completion, and explicit error states. It delegates all package state changes and rollback to Burn/MSI. Its graphite/silver Qt UI is accessible by keyboard and deliberately contains no blue Wolfmark-controlled states. The bootstrapper is out-of-process from the Burn engine, following WiX 7's supported native BA model.

Direct interactive MSI launch uses Windows Installer Embedded UI rather than a generic WixUI dialog set. `WolfmarkMsiEmbeddedUI.dll` is a system-only loader with the three standard Embedded UI exports. Windows Installer extracts it alongside `WolfmarkMsiUi.dll`, Qt Core/Gui/Widgets, `qwindows`, the app-local MSVC runtime, and the Wolfmark symbol. The loader resolves the Qt host from that private resource directory without changing global `PATH`; the host presents the same `SetupWindow` and maps its choices to `INSTALLFOLDER`, `WOLFMARK_FILE_ASSOC`, and `WOLFMARK_DESKTOP_SHORTCUT`. Real action/progress/error/completion records come from Windows Installer. `/qn` and policy-disabled Embedded UI remain non-interactive.

## Build-time tooling and terms

`cargo setup` restores the pinned WiX CLI to ignored project-local `out/toolchains/wix` and its native bootstrapper API packages to `out/cache/nuget`. The .NET SDK/NuGet are build-time prerequisites for restoring WiX packages; neither .NET nor NuGet is shipped to or required by Wolfmark users. `cargo package-app` records WiX 7 EULA acceptance explicitly with `-acceptEula wix7` and builds the BA with `/p:AcceptEula=wix7`; it does not create a global/user-profile acceptance marker.

WiX source is distributed under the Microsoft Reciprocal License. WiX 7 is also governed by the Open Source Maintenance Fee EULA. At the time of the dev.7 audit, the fee threshold described by WiX is USD 10,000 annual revenue attributable to projects using WiX. Wolfmark currently falls below that threshold; this is not a permanent assumption. The project must re-check current terms before every public release and whenever funding/revenue or WiX terms materially change. No commercial terms are accepted on a contributor's behalf by this documentation.

Authoritative references:

- https://github.com/wixtoolset/wix/releases/tag/v7.0.0
- https://docs.firegiant.com/wix/whatsnew/oopbas/
- https://docs.firegiant.com/wix/osmf/
- https://github.com/wixtoolset/wix/blob/develop/LICENSE.TXT

## Lifecycle and validation

`scripts/test_windows_installer.ps1` always performs a non-invasive artifact/checksum/MSI-table/Embedded-UI-resource/portable smoke audit and a quiet Burn layout that exercises the real headless detect/plan/apply path without creating a Qt window or installing Wolfmark. `-InteractiveLaunchSmoke` launches the actual packaged EXE and MSI, requires each custom top-level window to appear, cancels each normally, and checks process cleanup without installing anything. `-ExecuteLifecycle` additionally builds an older compatible MSI and exercises its upgrade through the current Setup EXE, same-version setup, bundle repair/uninstall, direct-MSI install/repair/modify/uninstall, Installed Apps uniqueness, launch, and user-document preservation under `out/tests`. The lifecycle requires an elevated PowerShell session and refuses to replace an existing Wolfmark installation unless the caller also supplies the explicit `-AllowExistingWolfmarkReplacement` switch.

The bundle remains unsigned for this development prerelease. Windows may display an Unknown publisher or reputation warning. Authenticode signing, WinGet publication, and dev.8 updater hardening remain separate work.
