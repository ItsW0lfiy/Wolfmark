# Windows shell integration — no file ownership

Wolfmark accepts Markdown/text file paths opened from inside the application,
dragged onto its window, or explicitly supplied as command-line arguments. An
open document must not lock its file or containing directory.

## Registration boundary

The installer must not register Wolfmark as an Open With or Default Apps handler
for .md, .markdown or .txt. It must not claim a Windows default file handler.

The Rust maintenance engine enforces this independently of an old setup UI
file-associations checkbox or a legacy command-line flag:

- The flag is clamped off during Install, Update, Reinstall, Repair, Modify, and rollback.
- The installer records FileAssociations=0 for backward compatibility.
- The normal App Paths executable lookup remains, without an associated document type.
- Existing Wolfmark-owned RegisteredApplications, capabilities, ProgIDs,
  OpenWithProgids and Applications/Wolfmark.exe handler registrations are cleaned up.
- Other applications' registrations and Windows UserChoice are not overwritten.
- Portable archives perform no registrations.

The historical Qt setup UI may still display a nonfunctional association
checkbox. It cannot override the enforced Rust boundary and must be removed
when a Servo-compatible installer UI replaces that older interface. Do not
add project-authored C++ to implement new installer features.

Tests use isolated HKCU registry subtrees rather than live Windows defaults.
Physical Windows upgrade and rollback verification remain outstanding.
