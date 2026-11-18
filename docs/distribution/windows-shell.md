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

## Installer boundary in 0.2.0

Wolfmark does not register document handlers, supported extensions, ProgIDs,
Open With capabilities, or default-file associations. Legacy command-line and
state fields can still be read for compatibility, but cannot enable registration.
The installed application keeps its ordinary App Paths and Installed Apps entries
and shortcuts. Portable archives perform no registration.

Install, update, repair, modification, rollback restoration, and removal clean
legacy handlers only when their command or icon matches the known Wolfmark
installation directory. Entries using the same names but pointing elsewhere are
preserved. Extension defaults, other applications' Open With entries, and Windows
UserChoice/hash values are never rewritten. No Windows Default Apps action is
offered by the setup UI.

Registry regressions use disposable uniquely named HKCU subtrees. They verify
that new application registration creates no handlers, that legacy cleanup keeps
other handlers/defaults intact, and that foreign entries reusing Wolfmark names
are preserved. No test writes to the live Classes or UserChoice trees.

Wolfmark retains its multiple-process policy. No single-instance mutex, named
pipe, or shell-routing IPC is introduced by this milestone.
