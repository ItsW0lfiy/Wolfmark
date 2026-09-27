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

## Logging and diagnostics

The core writes a unique operation log beneath `%ProgramData%\Wolfmark\Installer\Logs`. Logs contain operation identity, changed/no-change result, and the error returned by a failed operation; they do not contain Markdown document contents. `/DIAGNOSE` reports the parsed install state and integrity status. Logs survive uninstall for supportability.

## Release gate

`scripts/test_windows_setup.ps1` always checks exact artifact names, absence of MSI output, hashes, payload-manifest bytes, private Qt setup runtime, portable extraction, and restricted-PATH smoke. Its `-ExecuteLifecycle` mode requires an elevated clean Windows environment and performs two real install/damage/repair/uninstall cycles. It refuses to replace an existing Wolfmark installation without `-AllowExistingWolfmarkReplacement`.

The unsigned dev.7 setup may trigger Windows reputation warnings. Release readiness still requires physical elevated validation on a clean VM for UAC, interactive install/update/repair/reinstall/modify/uninstall, running-app handling, rollback, Explorer Open With, Installed Apps, DPI, accessibility, and WinGet-style silent invocation.

NSIS is distributed under the zlib/libpng license. Wolfmark pins 3.12 and verifies its archive SHA-256 before use; see `docs/licenses/NSIS-zlib-license.txt`.
