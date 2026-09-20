# Code block quality

The fenced block below checks language labeling, indentation, blank lines, and
continuous graphite containment.

```rust
fn main() {
    let message = "Wolfmark";

    if !message.is_empty() {
        println!("{message}");
    }
}
```

The paragraph after the fence must not inherit code formatting or excessive
spacing.

```text
A deliberately long code line remains intact where practical: 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789 0123456789
```
