# Windows setup and maintenance

Wolfmark dev.7 uses NSIS 3.12 as a thin, elevated Windows bootstrap. NSIS extracts the signed-or-local package payload and native setup host, then delegates all product decisions and file ownership to the framework-independent Rust maintenance core in `installer/maintenance`. No MSI, WiX, Burn, CLR, browser engine, Node.js, or separately installed runtime is involved.

## Artifacts

- `Wolfmark-Setup-win-x64.exe` — interactive and unattended install/maintenance controller.
- `Wolfmark-portable-win-x64.zip` — portable payload with `portable.flag`; it changes no machine registration.
- `SHA256SUMS.txt` — SHA-256 for those exact two artifacts.

The old unreleased `Wolfmark-win-x64.msi` is not part of the release contract. Setup recognizes the final dev.7 MSI ProductCode only to uninstall that development build through Windows Installer before the maintenance core assumes ownership.

## Build identity and state

Every package records the semantic version, full Git commit, the first 12 commit characters as BuildId, commit timestamp, x86-64 architecture, release channel, dirty flag, and source ancestry. Packaging refuses a dirty tree unless `-AllowDirty` is passed explicitly; such local artifacts use a `-dirty` BuildId and are not release candidates.

The schema-versioned payload manifest lists every installer-owned application file with its relative path, size, and SHA-256. Machine state, the transaction journal, the exact current/previous payload cache, and diagnostic logs live under:

```text
%ProgramData%\Wolfmark\Installer\
```

Status detection distinguishes absent, exact, damaged, older/newer, same-version ancestor relationships, diverged Git history, dirty candidates, partial/inconsistent state, and interrupted transactions. A pixel coordinate, package timestamp, or version string alone is never treated as product identity.

## Operations

- **Install** deploys the setup payload under 64-bit Program Files and registers only manifest-owned files.
- **Update** is selected for a newer semantic version or a descendant commit at the same development version.
- **Repair** hashes the installed manifest and restores only missing/corrupt files from the exact-build cache. It never downloads or upgrades.
- **Reinstall this setup build** fully redeploys the build embedded in the running setup. It does not claim to find the latest release.
- **Change options** updates Open With registration and the optional public Desktop shortcut without replacing application files.
- **Uninstall** removes manifest-owned files, shortcuts, Open With/capability/ProgID registration, and Installed Apps registration. User documents and per-user settings are not installer-owned and remain untouched. Diagnostic logs intentionally survive uninstall.

The transaction journal advances through prepared, staged, backed-up, applied, integrated, validated, and committed phases. Failed deployment attempts restore the previous manifest-owned payload and registration from backup. An unfinished journal is surfaced as interrupted/rescue state on the next launch.

## Windows integration

The maintenance core owns one centralized integration contract:

- Installed Apps entry `Wolfmark`;
- Start Menu shortcut;
- optional public Desktop shortcut;
- Registered Applications/capabilities;
- `.md`, `.markdown`, and `.txt` Open With ProgIDs and icons;
- quoted `"Wolfmark.exe" "%1"` open command.

Wolfmark never writes protected `UserChoice` keys and does not seize the user's default application.

## Command line

The setup executable supports these stable case-insensitive switches:

```text
Wolfmark-Setup-win-x64.exe /S
Wolfmark-Setup-win-x64.exe /MAINTENANCE
Wolfmark-Setup-win-x64.exe /UPDATE
Wolfmark-Setup-win-x64.exe /REPAIR
Wolfmark-Setup-win-x64.exe /REINSTALL
Wolfmark-Setup-win-x64.exe /UNINSTALL
Wolfmark-Setup-win-x64.exe /DIAGNOSE
Wolfmark-Setup-win-x64.exe /S /REPAIR
Wolfmark-Setup-win-x64.exe /S /UNINSTALL
Wolfmark-Setup-win-x64.exe /S /D=C:\Program Files\Wolfmark
```

NSIS requires `/D=<path>` to be the last argument. `/S` suppresses the Qt UI. Interactive operations use the approved native C++20/Qt Widgets setup presentation; unattended operations invoke the same Rust core without constructing a window. All operations require administrative rights because the install is per-machine.

NSIS captures the caller's silent flag in `.onInit`/`un.onInit` **before** making its own extraction window silent. A normal launch must not pass `--silent` to the native host. `/DIAGNOSE` always takes the read-only, unattended detection path. The generated uninstaller uses the same launch/logging contract.

### Private native host contract

`WolfmarkSetup.exe` is a private payload host, not the installer entry point. No-argument execution returns Win32 `ERROR_BAD_ARGUMENTS` (160), writes a bootstrap diagnostic, and displays instructions to run the packaged setup. It does not indicate a Qt crash. Direct developer invocation with the complete contract does not request elevation; use the NSIS entry point for real operations.

NSIS finishes extraction and writes the uninstaller before invoking the host. It sets the working directory to `$PLUGINSDIR\setup` and supplies these quoted, absolute paths:

```text
--maintenance=<runtime>\WolfmarkMaintenance.exe
--payload=<extracted payload>
--manifest=<extracted payload>\payload-manifest.json
--uninstaller=<generated or running uninstaller>
--install-dir=<selected directory>
--action=<empty, maintenance, update, repair, reinstall, uninstall or diagnose>
--setup-source=<running package>
--bootstrap-log=<per-launch log>
```

The host accepts both `--name=value` and `--name value`. Windows `CommandLineToArgvW` decodes UTF-16 arguments; the host retains its own UTF-8 argument storage and a null-terminated `argv` for QApplication. Unknown/duplicate options, missing required paths, and unsupported actions fail explicitly. No setup-state or operation environment variables are required. The core resolves machine state from inherited `ProgramData`; QApplication resolves `qt.conf`, plugins and branding beside the executable, independently of the current directory. No state file is required for a fresh install.

`--ui-smoke=<state>` and `--ui-screenshot=<path>` are presentation-only test modes; the existing `WOLFMARK_SETUP_SMOKE_STATE`/`WOLFMARK_SETUP_SCREENSHOT` variables provide their developer equivalents. Valid states are install, update (also upgrade), maintenance, repair, uninstall, progress, complete and error. `--startup-check=<PNG path>` instead validates the full startup contract, runs actual core detection, renders the detected native page, saves a screenshot, and exits without performing an operation. It has a 30-second timeout and cannot be combined with `--silent`.

## Logging and diagnostics

The core writes a unique operation log beneath `%ProgramData%\Wolfmark\Installer\Logs`. Logs contain operation identity, changed/no-change result, and the error returned by a failed operation; they do not contain Markdown document contents. `/DIAGNOSE` reports the parsed install state and integrity status. Logs survive uninstall for supportability.

Before the core logger exists, NSIS and the native host append timestamped UTF-16LE records to `%TEMP%\Wolfmark-Setup-bootstrap-<process-id>.log`. Records include package/runtime paths, the constructed known argument list, working directory, extraction/launch stage, QApplication/platform readiness, detected state, core process errors/stderr and original child exit codes. Arbitrary package command lines and unknown argument values are not logged. NSIS keeps its automatic extracted-runtime cleanup enabled. An interactive launch/extraction/host failure displays a basic fallback dialog with the component, exit code and diagnostic path; cancellation exits 1602/1223 without an error dialog. `/S` failures remain unattended and logged.

Host startup codes use existing Win32 values, not a new internal numbering scheme: 160 for malformed/incomplete arguments, 2/3 for missing startup files/directories, 29 for screenshot write failure, 1460 for startup-probe timeout, and 1603 for helper launch/crash, invalid detection data or unhandled host exceptions. Normal helper exits propagate unchanged, including the Rust argument parser's exit 2. Logs identify the originating stage so parser exit 2 can be distinguished from Win32 file-not-found 2. NSIS uses conventional installer failure 1603 when there is no child exit code because process creation/extraction failed; original core exit/status and stderr remain in the log.

The interactive startup regression had two coupled causes: `SilentInstall silent` forced every launch into the unattended host path, and that path supplied `--cancel-file` with an empty value. The Rust parser rejected the empty value with exit 2 before its failure logger; the host then mapped it to 1603. Cancellation paths are now supplied only when a real cancellation file exists. Runtime extraction was not missing: NSIS initializes its plugin directory before the extraction section; explicit initialization now makes the ordering visible in the bootstrap source.

## Release gate

`cargo package-app` gates packaging on real native-host argument parsing, Qt/plugin loading, core detection and rendering under a restricted PATH and unrelated working directory. `scripts/test_windows_setup.ps1` checks exact artifact names, absence of MSI output, hashes, payload-manifest bytes, private Qt setup runtime, portable extraction, and restricted-PATH smoke, then runs `scripts/test_setup_startup.ps1`. The startup suite tests the real Qt/core path and compiles the production NSIS source into test-local bootstraps with a recording Rust maintenance double. It checks extraction, quoting/Unicode/spaces, interactive and silent routing, the generated uninstaller, empty cancellation argument regression, original exit propagation and process-creation failure logging. Only those test copies lower execution level and add a read-only probe; production packages retain required elevation and normal cleanup. These tests do not establish physical UAC or real elevated lifecycle success.

The setup script's `-ExecuteLifecycle` mode requires an elevated clean Windows environment and performs two real install/damage/repair/uninstall cycles. It refuses to replace an existing Wolfmark installation without `-AllowExistingWolfmarkReplacement`.

The unsigned dev.7 setup may trigger Windows reputation warnings. Release readiness still requires physical elevated validation on a clean VM for UAC, interactive install/update/repair/reinstall/modify/uninstall, running-app handling, rollback, Explorer Open With, Installed Apps, DPI, accessibility, and WinGet-style silent invocation.

NSIS is distributed under the zlib/libpng license. Wolfmark pins 3.12 and verifies its archive SHA-256 before use; see `docs/licenses/NSIS-zlib-license.txt`.
