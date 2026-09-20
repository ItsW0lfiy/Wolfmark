# Windows shell integration

Moonmark dev.7 accepts ordinary Windows command-line document paths. One process may receive and retain multiple `.md`, `.markdown`, and `.txt` documents. Paths are decoded as UTF-8 across the Rust/C++ boundary, resolved relative to the process working directory when necessary, canonicalized through the existing document identity path, and deduplicated using the existing Windows case-insensitive identity comparison.

Examples:

```text
Moonmark.exe "C:\Docs\README.md"
Moonmark.exe "C:\Docs\My Document.md"
Moonmark.exe "C:\Docs\日本語 document.md"
Moonmark.exe "A.md" "B.markdown" "notes.txt"
```

Arguments beginning with `-` are treated as switches, not file names. Missing and unsupported file arguments are not opened; interactive startup reports them together in a warning. Empty startup remains valid. Drag/drop and the Open dialog use the same supported document set.

## Installer registration

The WiX MSI registers the installed executable, never a build-tree path. The open command preserves quoting:

```text
"[INSTALLFOLDER]Moonmark.exe" "%1"
```

Registration should make Moonmark a capable Open With application for:

- `.md`
- `.markdown`
- `.txt`

The implemented conventional registration model is:

- an application registration under `RegisteredApplications`;
- Moonmark capabilities with `FileAssociations` entries;
- one Moonmark-owned document ProgID and quoted `shell\open\command`;
- `Applications\Moonmark.exe\SupportedTypes` for Open With discovery;
- uninstall flags that remove only Moonmark-owned registration;
- an Explorer association-change notification after install/uninstall.

The native setup UI offers Open With registration and the user may opt out or later change it through maintenance. It registers availability but does not overwrite Windows `UserChoice` or silently claim the current default. On modern Windows, the user remains authoritative through Open With / Default Apps. Portable archives perform no registration.

Dev.7 deliberately retains a multiple-process policy: an Explorer invocation may create a new Moonmark process. No single-instance mutex, named pipe, or shell-routing IPC is part of this milestone.
