---
fixture: compatibility
title: Wolfmark compatibility
---

# Wolfmark compatibility fixture

Plain paragraph with a  
hard break and a soft
line break.

**Bold**, *italic*, ~~struck~~, `inline code`, an <https://example.test> autolink, and a [standard link](https://example.test).

[Reference-style link][reference]. Escaped \*asterisks\* and an HTML entity: &copy;.

[reference]: https://example.test/reference "Reference"

1. Ordered
   - Nested unordered
     - [x] Complete task
     - [ ] Open task
2. Second

> A blockquote
>
> with two paragraphs.

---

```rust
fn main() {
    println!("Wolfmark");
}
```

| Feature | Supported |
| :--- | ---: |
| Tables | Yes |

> [!NOTE]
> GitHub-style alert content.

A footnote reference.[^detail]

[^detail]: Footnote content.

![Relative image](../generated/assets/small-01.png)
