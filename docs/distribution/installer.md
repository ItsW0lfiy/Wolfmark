# Windows installer decision — dev.7

Inno Setup was explicitly approved for Moonmark dev.7 after the following comparison. Moonmark uses Inno Setup 7.1.0 to compile a conventional offline setup executable from the same staged payload used by the portable ZIP. Inno is a build-time tool only; users do not install an Inno runtime.

## Shortlist

| Engine | Output and unattended use | Upgrade, uninstall, shell integration | Build/tooling and license | Moonmark fit |
| --- | --- | --- | --- | --- |
| **Inno Setup** | Traditional single `.exe`; documented `/SILENT` and `/VERYSILENT` modes with `/SUPPRESSMSGBOXES` and `/NORESTART` | Built-in uninstall, shortcuts, Program Files installs, registry entries, version-aware replacement, and optional installer tasks | Native compiler plus an `.iss` installer DSL. Its license permits use for any purpose, including commercial applications; the project separately requests commercial users purchase a license. No client runtime is installed. | **Recommended.** Smallest maintainable route to Moonmark's conventional installer and future WinGet validation. |
| **NSIS** | Traditional `.exe`; `/S` silent mode | Fully capable through its script language, but upgrades, registry ownership, and uninstall details are more manual | Native compiler and `.nsi` DSL; primarily zlib/libpng licensed, with separately licensed compression modules | Viable runner-up, but Moonmark would own more low-level installation logic for no current benefit. |
| **WiX Toolset** | Native `.msi`/bundle output and strong enterprise unattended behavior | Excellent Windows Installer upgrade/uninstall semantics and component ownership | Current WiX is normally built as a .NET tool/MSBuild SDK and requires a .NET SDK. Current documentation also describes an Open Source Maintenance Fee for revenue-generating use. | Not recommended under Moonmark's current runtime/no-required-paid-component policy without another explicit decision. |
| **MSIX** | Native `.msix`; deployment is largely Windows-managed | Strong clean install/update/uninstall and declarative file associations | Windows tooling; every directly deployed package must be signed and the certificate trusted on the client | Poor fit for an unsigned first GitHub prerelease. It also introduces package identity/container and signing decisions beyond this milestone. |

## Decision

Moonmark uses **Inno Setup** for its first Windows installer. It produces the expected offline setup EXE, installs the validated portable payload under Program Files, registers Moonmark without changing protected Windows defaults, supports optional Desktop/file-association tasks, and has normal uninstall/upgrade and unattended behavior suitable for later WinGet validation.

The implementation is [Moonmark.iss](../../packaging/windows/Moonmark.iss), invoked by `scripts/package_windows.ps1`. `scripts/bootstrap_inno.ps1` can download the official signed 7.1.0 compiler into ignored project-local `out/cache/inno`; it checks the pinned SHA-256 and Authenticode signer before extracting the compiler under `out/toolchains/inno`. A compatible explicitly supplied compiler remains supported through `-InnoCompiler` or `MOONMARK_INNO_ISCC`.

The decision does not authorize code-signing claims, single-instance IPC, or an application-architecture change. Moonmark's setup executable remains unsigned for this development prerelease. Dev.7's separately approved update-delivery path downloads this complete installer only after user action and verifies it against the release checksum manifest.

Local lifecycle validation covers clean current-user installation of the same payload, an older local dev.7 build upgraded in place through the stable AppId, same-version reinstall, installed launch, shell registration, shortcuts, silent uninstall, registration cleanup, and preservation of user documents. A physical elevated Program Files install and the Explorer/Installed Apps visual surfaces remain manual Windows checks before publication.

## Sources checked

- [Inno Setup features](https://jrsoftware.org/isinfo.php), [license](https://github.com/jrsoftware/issrc/blob/main/license.txt), and [command-line parameters](https://jrsoftware.org/ishelp/topic_setupcmdline.htm)
- [NSIS license](https://nsis.sourceforge.io/License) and [silent install behavior](https://nsis.sourceforge.io/Docs/Chapter3.html#3.2.1)
- [WiX usage and .NET SDK requirement](https://docs.firegiant.com/wix/using-wix/) and [current licensing/maintenance-fee statement](https://docs.firegiant.com/wix/)
- [Microsoft's Windows packaging comparison](https://learn.microsoft.com/windows/apps/package-and-deploy/packaging/) and [MSIX signing requirement](https://learn.microsoft.com/windows/msix/package/signing-package-overview)

Licensing findings are an engineering audit, not legal advice.
