# Settings and update delivery

Wolfmark dev.7 has one typed settings owner in the native application shell. Schema version 1 stores `updates.checkOnStartup`, `updates.includePrereleases`, and `files.lastOpenDirectory`. Installed builds use Qt's per-user application-config location. A package containing `portable.flag` instead uses `data/settings.json` beside `Wolfmark.exe`. Writes use `QSaveFile`; malformed JSON is preserved as a timestamped `settings.invalid-*.json` file before defaults are restored. The former Qt `lastOpenDirectory` value migrates once and is removed only after the JSON save succeeds.

Update checks use the public GitHub Releases API at `https://api.github.com/repos/ItsW0lfiy/Moonmark/releases?per_page=30`. No GitHub account, token, telemetry, local server, updater service, or browser runtime is involved. Startup checks run asynchronously after the window is usable, can be disabled, remain quiet on failure/current status, and are suppressed for six hours after a successful cached check. Manual checks bypass that interval. Wolfmark stores only the release response, ETag, and last-success timestamp in the platform cache location.

Rust owns release interpretation and checksum verification. Drafts and malformed versions are ignored; prereleases follow the setting; SemVer decides whether a newer version exists. A usable release must provide the exact platform assets:

- `Wolfmark-Setup-win-x64.exe`
- `Wolfmark-win-x64.msi` (separate administrative artifact; the in-app handoff continues to select the setup EXE)
- `Wolfmark-portable-win-x64.zip`
- `SHA256SUMS.txt`

The native frontend downloads only HTTPS GitHub asset URLs after explicit user action. It writes through `QSaveFile`, verifies the selected artifact against the exact manifest filename through the Rust core, and never executes an unverified file. Installed mode offers to launch the verified setup and quits only after process creation succeeds. Portable mode saves the verified ZIP and opens its containing folder; it does not overwrite or extract over the running application.

This is a bounded delivery foundation, not a background updater. It does not silently download, install, elevate, schedule tasks, modify release channels, or patch binaries in place. Dev.8 remains responsible for hardening the behavior against real published releases and broader release-channel/recovery cases.
