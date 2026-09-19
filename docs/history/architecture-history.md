# Architecture history

Moonmark began with a web-desktop experiment and later used an early Qt/QTextDocument presentation implementation. Neither implementation was restored wholesale.

The subsequent Rust + C#/Avalonia experiment established a valuable framework-neutral Rust semantic and presentation model and validated substantial renderer, image, file, performance, and window behavior. Rust launched the CLR in-process through `hostfxr`, and Avalonia provided the reference desktop presentation.

The Avalonia experiment was discontinued after its runtime/distribution tradeoff was not accepted for Moonmark. This was a project-specific architectural and packaging decision, not a claim that the experiment technically failed or that managed runtimes cannot be approved elsewhere. The measured Windows x64 results were approximately:

- 6.07 MiB of Moonmark-owned binaries
- 32.72 MiB for a framework-dependent package that still required installed .NET
- 109.21 MiB for a self-contained non-AOT package
- 76.46 MiB of bundled .NET runtime in that self-contained package

An Iced investigation later stopped at whole-document rich-text selection and accessibility limitations. A Slint proof was investigation-only and did not become production architecture.

The user subsequently approved Rust and C++20 as first-class Moonmark languages and Qt 6 Widgets/QTextDocument as the Windows/Linux desktop frontend. The current implementation preserves the newer Rust parsing, semantic/presentation models, image pipeline, diagnostics, tests, fixtures, benchmarks, and behavioral expectations, while building a new Qt adapter around them. The obsolete C#/Avalonia/.NET source, CLR hosting, and build path were removed only after the native renderer and stress smoke matrix passed.

Cargo remains the entry point and the application remains one native process. CXX-Qt was evaluated as unnecessary for the current plain C ABI and was not restored.

Milestone 0.1.0-dev.4 keeps that architecture. It translates the approved lunar concept into file-first navigation, a restrained native header, clearer Markdown tables, and native percentage zoom. Rust/C++ ownership remains a technical-fit decision rather than a fixed language ratio.
