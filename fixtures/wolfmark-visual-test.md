# Wolfmark visual rendering fixture

This fixture checks the wide native document layout, comfortable body typography, **strong text**, *emphasis*, ***combined emphasis***, ~~strikethrough~~, [an achromatic link](https://example.invalid), and `inline code`.

## Second-level heading

Hard break follows this line.  
The next line starts deliberately. A soft
line break remains compatible with ordinary Markdown flow.

### Third-level heading

#### Fourth-level heading

##### Fifth-level heading

###### Sixth-level heading

> A quiet blockquote uses indentation and neutral contrast.
>
> It remains part of the document rather than becoming a large decorative card.

---

## Lists and tasks

- First unordered item
  - Nested item
    - Deeply nested item without excessive indentation
- Final unordered item

1. First ordered item
2. Second ordered item
   1. Nested ordered item

- [x] Rust semantic model parsed
- [ ] Deferred native image decode

## Table

| Feature | Native owner | Status |
|:--|:--|--:|
| Markdown model | Rust | Ready |
| Text layout | Wolfmark native document view | Native Qt / QTextDocument |
| Browser engine | None | 0 |

## Code

```rust
pub fn document_width(viewport: i32) -> i32 {
    viewport.saturating_sub(96)
}
```

    Indented code remains code.
    Whitespace is preserved.

## Images

![Large generated lunar test image](images/large-lunar-test.png)

![Path containing spaces](images/image-with-spaces.png)

![Missing image](images/missing-image.png)

End of chapter paragraph immediately before a thematic break.

---

## Heading immediately after a thematic break

![Image immediately after a heading](images/image-with-spaces.png)

Unicode: Wolfmark — maan — 月 — λ. Escaped characters: \*literal asterisks\* and \[literal brackets\].

Long line: This deliberately long line exercises the wide desktop layout and confirms that Wolfmark uses the available window rather than squeezing rendered Markdown into a narrow centered article card that leaves most of a large monitor unused.
