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

The installer option **Add Wolfmark to Windows Open with and Default Apps**
lists the installed executable as a candidate for `.md`, `.markdown`, and `.txt`.
Users choose defaults through Windows. Setup leaves extension defaults, Windows
UserChoice and hashes alone. Portable builds do not register themselves.

Fresh setup leaves the option off. Update and reinstall show the saved choice,
and repair restores entries when enabled. If a previous 0.2.0 development build
erased your preference, run the corrected setup, choose **Change installation
options**, and enable the option. The update page can also enable it explicitly.

Disabling or uninstalling removes entries identified as belonging to the
installed Wolfmark. Conflicting registrations are preserved. Opening a document
releases its read handles and leaves the source available to other applications.

See Microsoft's [Default Programs registration](https://learn.microsoft.com/en-us/windows/win32/shell/default-programs)
and [Open with registration](https://learn.microsoft.com/en-us/windows/win32/shell/how-to-include-an-application-on-the-open-with-dialog-box).
Automated tests use isolated registry subtrees. Explorer launch and Default Apps
visibility need acceptance on a normal Windows desktop.

Wolfmark retains its multiple-process policy. No single-instance mutex, named
pipe, or shell-routing IPC is introduced by this milestone.
