# Next steps

Wolfmark's active implementation milestone is `0.1.0-dev.7`: First Windows Release & Shell Integration. WiX Toolset 7.0.0 is approved and the local release workflow now produces a native Burn setup executable, genuine MSI, portable ZIP, and checksums from one staged payload. Dev.7 also contains the first bounded update-delivery foundation: typed settings, conditional GitHub release checks, exact asset selection, SHA-256 verification, and explicit installed/portable handoff. Before publication, complete the remaining elevated/clean-VM Windows acceptance checks, review the exact local artifacts, re-check WiX 7 OSMF terms, then obtain separate authorization for push, tag, GitHub prerelease creation, and asset upload.

After dev.7 is locally complete and explicitly authorized for publication, `0.1.0-dev.8` remains the updater-hardening milestone. It should validate the published-release lifecycle, channel behavior, recovery, and update UX against real signed-or-unsigned release artifacts without adding a background service or silently installing software.

See [ROADMAP.md](ROADMAP.md) for the development-channel, updater, WinGet, Linux, Android, editor, settings, and packaging direction.
