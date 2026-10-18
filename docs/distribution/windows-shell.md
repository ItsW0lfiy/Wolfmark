# Windows shell integration

Wolfmark dev.7 accepts ordinary Windows command-line document paths. One process may receive and retain multiple `.md`, `.markdown`, and `.txt` documents. Paths are decoded as UTF-8 across the Rust/C++ boundary, resolved relative to the process working directory when necessary, canonicalized through the existing document identity path, and deduplicated using the existing Windows case-insensitive identity comparison.

Examples:

```text
Wolfmark.exe "C:\Docs\README.md"
Wolfmark.exe "C:\Docs\My Document.md"
Wolfmark.exe "C:\Docs\日本語 document.md"
Wolfmark.exe "A.md" "B.markdown" "notes.txt"
```

Arguments beginning with `-` are treated as switches, not file names. Missing and unsupported file arguments are not opened; interactive startup reports them together in a warning. Empty startup remains valid. Drag/drop and the Open dialog use the same supported document set.

## Installer registration

The Rust maintenance core registers the installed executable, never a build-tree path. The open command preserves quoting:

```text
"[INSTALLFOLDER]Wolfmark.exe" "%1"
```

Registration should make Wolfmark a capable Open With application for:

- `.md`
- `.markdown`
- `.txt`

The implemented conventional registration model is:

- an application registration under `RegisteredApplications`;
- Wolfmark capabilities with `FileAssociations` entries;
- separate Wolfmark-owned Markdown/text ProgIDs and quoted `shell\open\command`;
- `Applications\Wolfmark.exe\SupportedTypes`, friendly name and application icon for Open With discovery;
- an installed-executable `App Paths\Wolfmark.exe` entry, without changing the system PATH;
- uninstall flags that remove only Wolfmark-owned registration;
- an Explorer association-change notification after install/uninstall.

The native setup UI offers Open With registration and the user may opt out or later change it through maintenance. It registers availability but does not overwrite Windows `UserChoice` or silently claim the current default. On modern Windows, the user remains authoritative through Open With / Default Apps. Portable archives perform no registration.

Setup's completion and maintenance pages offer **Choose Windows defaults** when file registration is enabled. This opens the system-owned `ms-settings:defaultapps?registeredAppMachine=Wolfmark` page; it does not set a default. Failure to open Settings is reported, with instructions to open it manually. Silent setup does not open Settings.

After successful install, repair, option changes, rollback restoration or uninstall, the maintenance core sends `SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, ...)` so Explorer refreshes its association information. The pre-fix core wrote registration without this notification. Windows owns the next-file-open chooser and any **New** label: registration makes Wolfmark eligible, but setup does not manufacture a label, reset per-user chooser history, or force a prompt. Reinstalling an already-known ProgID is not a fresh application registration for every user.

Registry regression tests use uniquely named, isolated HKCU test subtrees, not live file associations. They verify the installed path/quoting, all three file types, Default Apps capabilities and opt-out cleanup while retaining another application's registration, extension default and simulated UserChoice/hash. The restricted Codex sandbox denies these test writes; run the tests from an ordinary Windows developer shell or with approved unsandboxed execution. No administrator rights are needed for those isolated tests.

References: [Windows app defaults platform](https://learn.microsoft.com/en-us/windows/apps/develop/windows-integration/default-apps-platform), [Default Apps Settings URI](https://learn.microsoft.com/en-us/windows/apps/develop/launch/launch-default-apps-settings), [application registration](https://learn.microsoft.com/en-us/windows/win32/shell/app-registration), [Shell association-change notification](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shchangenotify).

Dev.7 deliberately retains a multiple-process policy: an Explorer invocation may create a new Wolfmark process. No single-instance mutex, named pipe, or shell-routing IPC is part of this milestone.
