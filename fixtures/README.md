# Wolfmark fixtures

`compatibility/all-features.md` and `security/unsafe-html.md` are small committed inputs used to preserve Markdown and inert-content expectations.

`fixtures/generated` is reproducible output and is ignored by Git. Generate it with:

```powershell
cargo run --bin generate_stress_fixture
```

The Rust generator produces geometric PNG, JPEG, WebP, GIF, and SVG assets plus a Markdown document with exactly 250 valid image references, headings, paragraphs, nested lists, task-list syntax, tables, code blocks, Unicode, spaces, percent-encoded paths, intentionally missing references, and a 4096-pixel source. No private or copyrighted documents are used.

`wolfmark-visual-test.md` is the committed wide-layout visual fixture. Its geometric images live in `fixtures/images`. `task-list-visual.md` is a focused fixture for native achromatic task boxes, nested lists, and ordinary list markers.

`literal-space-destinations.md` verifies that literal, percent-encoded, and CommonMark angle-bracket forms all resolve the real `images/image with spaces.png` fixture without changing ordinary web or reference links.

`markdown-compatibility.md` covers Wolfmark's deliberate CommonMark, GFM, reference-link, footnote, and inert-HTML profile through the semantic and native presentation paths.
