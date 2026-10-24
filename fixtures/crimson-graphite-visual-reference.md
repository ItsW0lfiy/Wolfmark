# Wolfmark Architecture

An overview of the internal architecture, core components and design principles
behind Wolfmark, a native Markdown reader and editor built with Rust and Qt 6.

---

## 1. Introduction

Wolfmark is a modern, native Markdown reader and editor designed for a fast, reliable and visually consistent
experience across Windows and Linux. It combines a custom rendering pipeline with a clean and focused user interface,
built using `Rust` for application logic and `C++/Qt 6` for the native frontend.

> **Core principle**
>
> Keep the interface fast, stable and out of the way, so the content remains the focus.

## 2. Architecture Overview

Wolfmark follows a **hybrid architecture** where Rust handles the application state, document model and core logic,
while C++/Qt provides the native user interface, rendering integration and platform functionality.

```rust
pub struct Document {
    pub path: PathBuf,
    pub content: String,
    pub metadata: DocumentMetadata,
}
```

| Component | Language | Responsibility |
| --- | --- | --- |
| Frontend (Qt) | C++ / Qt 6 | Native UI, rendering, input handling |
| Backend | Rust | Application logic, document model, configuration |
| Renderer | Rust + C++ | Markdown parsing and custom rendering |
| Platform integration | Rust + Qt | File system, dialogs, updates, system integration |

## 3. Core Components

The application preserves a single source of truth for document content and settings.
