# Windows installer — dev.7

Wolfmark dev.7 uses **WiX Toolset 7.0.0** to produce a genuine per-machine x64 MSI and a WiX Burn setup bundle. WiX was explicitly approved for this milestone. The setup user interface is a small native C++20/Qt Widgets bootstrapper application; it contains no C#, CLR, browser engine, or managed client runtime.

The generated artifacts are:

- `Wolfmark-Setup-win-x64.exe` — the normal user-facing Burn bundle;
- `Wolfmark-win-x64.msi` — the genuine Windows Installer package for administration and deployment;
- `Wolfmark-portable-win-x64.zip` — the same application payload with portable mode enabled;
- `SHA256SUMS.txt` — hashes of all three distributables.

The MSI owns Program Files deployment, Installed Apps metadata, Start Menu and optional Desktop shortcuts, Open With registration, repair, modify, major upgrades, and uninstall. The permanent Wolfmark MSI UpgradeCode is `{24F5627C-0519-4BC3-9C73-4DEBD580BA11}`. The Wolfmark Burn bundle uses permanent UpgradeCode `{84457DCA-5666-4684-92DF-9E7677C285FD}`. Product/package codes are generated per build as required by major-upgrade servicing.

The unreleased Moonmark development installer used different identities and is intentionally not part of Wolfmark's upgrade family. Existing local Moonmark development installs are left untouched. If one interferes with manual testing, remove it through Windows Installed Apps (or its original setup executable) before testing Wolfmark; do not delete its Program Files or registry entries by hand.

The custom bootstrapper offers install, update, maintenance, modify, repair, uninstall confirmation, progress, completion, and explicit error states. It delegates all package state changes and rollback to Burn/MSI. Its graphite/silver Qt UI is accessible by keyboard and deliberately contains no blue Wolfmark-controlled states. The bootstrapper is out-of-process from the Burn engine, following WiX 7's supported native BA model.

## Build-time tooling and terms

`cargo setup` restores the pinned WiX CLI to ignored project-local `out/toolchains/wix` and its native bootstrapper API packages to `out/cache/nuget`. The .NET SDK/NuGet are build-time prerequisites for restoring WiX packages; neither .NET nor NuGet is shipped to or required by Wolfmark users. `cargo package-app` records WiX 7 EULA acceptance explicitly with `-acceptEula wix7` and builds the BA with `/p:AcceptEula=wix7`; it does not create a global/user-profile acceptance marker.

WiX source is distributed under the Microsoft Reciprocal License. WiX 7 is also governed by the Open Source Maintenance Fee EULA. At the time of the dev.7 audit, the fee threshold described by WiX is USD 10,000 annual revenue attributable to projects using WiX. Wolfmark currently falls below that threshold; this is not a permanent assumption. The project must re-check current terms before every public release and whenever funding/revenue or WiX terms materially change. No commercial terms are accepted on a contributor's behalf by this documentation.

Authoritative references:

- https://github.com/wixtoolset/wix/releases/tag/v7.0.0
- https://docs.firegiant.com/wix/whatsnew/oopbas/
- https://docs.firegiant.com/wix/osmf/
- https://github.com/wixtoolset/wix/blob/develop/LICENSE.TXT

## Lifecycle and validation

`scripts/test_windows_installer.ps1` always performs a non-invasive artifact/checksum/MSI-table/portable smoke audit. `-ExecuteLifecycle` additionally builds an older compatible MSI and exercises its upgrade through the current Setup EXE, same-version setup, bundle repair/uninstall, direct-MSI install/repair/modify/uninstall, Installed Apps uniqueness, launch, and user-document preservation under `out/tests`. The lifecycle requires an elevated PowerShell session and refuses to replace an existing Wolfmark installation unless the caller also supplies the explicit `-AllowExistingWolfmarkReplacement` switch.

The bundle remains unsigned for this development prerelease. Windows may display an Unknown publisher or reputation warning. Authenticode signing, WinGet publication, and dev.8 updater hardening remain separate work.
