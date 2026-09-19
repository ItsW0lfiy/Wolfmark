# Dependency license audit

Audit date: 2026-09-19. Target: Moonmark 0.1.0-dev.7, Windows x64. This is an engineering record, not legal advice.

## Result

No declared dependency license conflict was found with distributing Moonmark under GPL-3.0-only.

`cargo metadata --format-version 1` reports the Rust dependency graph under permissive terms: MIT, Apache-2.0, BSD-2-Clause, BSD-3-Clause, 0BSD, ISC-style, Zlib, Unlicense, Unicode-3.0, and Unicode-DFS-2016 combinations. Apache-2.0 is compatible with GPLv3; the BSD/MIT/ISC/Zlib families are permissive and GPL-compatible; the Free Software Foundation describes the Unicode data/software license as GPL-compatible. A generated release notice must still reproduce applicable copyright and license notices.

Moonmark uses Qt 6.11.2 Core, Gui, Widgets, Network, and the `qwindows` platform plugin. Qt documents those essential components as available under LGPLv3/GPLv3. Moonmark dynamically links them and ships the LGPLv3 and GPLv3 texts. It does not use Qt WebEngine, Qt Quick, QML, or a GPL-only add-on module.

Dev.7 adds `semver` plus the RustCrypto `sha2` stack (`digest`, `block-buffer`, `crypto-common`, `generic-array`, `typenum`, and `cpufeatures`). Their locked manifests declare MIT and/or Apache-2.0 terms. They provide release-version ordering and local SHA-256 verification; they do not add a language runtime, service, or browser component.

The app-local Microsoft Visual C++ runtime is redistributed under Microsoft's Visual Studio redistribution terms. Windows system DLLs are supplied by the operating system and are not packaged as Moonmark dependencies.

## Sources and release gate

- GNU license compatibility: https://www.gnu.org/licenses/license-compatibility.html
- GNU license list, including Unicode: https://www.gnu.org/philosophy/license-list.html
- Qt 6 licensing: https://doc.qt.io/qt-6/licensing.html
- Qt open-source obligations: https://www.qt.io/development/open-source-lgpl-obligations
- SPDX identifiers: https://spdx.org/licenses/

Before each public release, regenerate the dependency inventory from the locked graph, inspect changes, include all required third-party notices, and verify the exact Qt deployment against its SBOM. A crate's manifest declaration is evidence for review, not a substitute for reading the applicable license text.
