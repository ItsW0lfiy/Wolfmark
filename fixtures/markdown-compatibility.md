# Compatibility fixture

## Nested heading

**bold**, *italic*, ***combined***, ~~strikethrough~~, and `inline code`.

**Strong with *nested emphasis*** and *emphasis with **nested strength*** remain structured.

This is a soft
line break. This is a hard break.\
The next line follows it.

Escaped punctuation stays literal: \*not emphasis\* and \[not a link\]. Entities decode normally: &copy; &amp;.

Inline HTML stays inert: <span title="example">not a native HTML element</span>.

```rust additional-info
fn example() {}
```

> blockquote
>
> > nested blockquote

- unordered
  - nested item

1. ordered
2. list

- [ ] unfinished task
- [x] finished task

| Column A | Column B |
| -------- | -------- |
| one      | two      |

---

<https://example.com/autolink>

[Inline link](https://example.com/ "Inline title")

[Reference link][reference]

[Collapsed reference][]

[Shortcut reference]

[reference]: https://example.com/reference "Reference title"
[Collapsed reference]: https://example.com/collapsed
[Shortcut reference]: https://example.com/shortcut

A footnote reference remains visible.[^compatibility]

[^compatibility]: Wolfmark currently presents footnotes as basic text.

<div data-kind="inert">
Block HTML remains inert source text.
</div>

## Repeated heading

First duplicate anchor target.

## Repeated heading

Second duplicate anchor target.
