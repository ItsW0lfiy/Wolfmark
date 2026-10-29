//! Rust-owned source/history. DOM text is only an edit proposal; saves use original bytes.
use std::{io::Write, path::Path};

pub struct Buffer {
    pub source: String,
    saved: String,
    undo: Vec<String>,
    redo: Vec<String>,
}
impl Buffer {
    pub fn new(source: String) -> Result<Self, String> {
        validate(&source)?;
        Ok(Self {
            saved: source.clone(),
            source,
            undo: Vec::new(),
            redo: Vec::new(),
        })
    }
    pub fn dirty(&self) -> bool {
        self.source != self.saved
    }
    pub fn view(&self) -> String {
        normalize(&self.source).0
    }
    pub fn apply(&mut self, view: &str) -> Result<(), String> {
        validate(view)?;
        let view = view.replace("\r\n", "\n").replace('\r', "\n");
        let (old, boundaries) = normalize(&self.source);
        if old == view {
            return Ok(());
        }
        let mut prefix = old
            .bytes()
            .zip(view.bytes())
            .take_while(|(a, b)| a == b)
            .count();
        while !old.is_char_boundary(prefix) || !view.is_char_boundary(prefix) {
            prefix -= 1;
        }
        let mut suffix = old[prefix..]
            .bytes()
            .rev()
            .zip(view[prefix..].bytes().rev())
            .take_while(|(a, b)| a == b)
            .count();
        while !old.is_char_boundary(old.len() - suffix)
            || !view.is_char_boundary(view.len() - suffix)
        {
            suffix -= 1;
        }
        let start = boundaries[prefix];
        let end = boundaries[old.len() - suffix];
        let ending = preferred_ending(&self.source, start);
        let inserted = view[prefix..view.len() - suffix].replace('\n', ending);
        let mut next = self.source[..start].to_owned();
        next.push_str(&inserted);
        next.push_str(&self.source[end..]);
        self.undo.push(std::mem::replace(&mut self.source, next));
        // Bounded full-source history for the initial editor integration.
        while self.undo.iter().map(String::len).sum::<usize>() > 64 * 1024 * 1024
            || self.undo.len() > 256
        {
            self.undo.remove(0);
        }
        self.redo.clear();
        Ok(())
    }
    pub fn undo(&mut self) {
        if let Some(source) = self.undo.pop() {
            self.redo.push(std::mem::replace(&mut self.source, source));
        }
    }
    pub fn redo(&mut self) {
        if let Some(source) = self.redo.pop() {
            self.undo.push(std::mem::replace(&mut self.source, source));
        }
    }
    pub fn save(&mut self, path: &Path) -> Result<(), String> {
        let expected = self.saved.as_bytes();
        save_atomic(path, self.source.as_bytes(), Some(expected))?;
        self.saved.clone_from(&self.source);
        Ok(())
    }
    pub fn save_as(&mut self, path: &Path, expected: Option<&[u8]>) -> Result<(), String> {
        save_atomic(path, self.source.as_bytes(), expected)?;
        self.saved.clone_from(&self.source);
        Ok(())
    }
}
fn validate(source: &str) -> Result<(), String> {
    if source.contains(['\0', '\u{2028}', '\u{2029}']) {
        return Err("Document contains unsupported NUL or Unicode paragraph separators".into());
    }
    Ok(())
}
fn normalize(source: &str) -> (String, Vec<usize>) {
    let mut result = String::new();
    let mut position = if source.starts_with('\u{feff}') { 3 } else { 0 };
    let mut boundaries = vec![position];
    while position < source.len() {
        let ch = source[position..].chars().next().unwrap();
        let (display, consumed) = if ch == '\r' {
            (
                '\n',
                if source[position..].starts_with("\r\n") {
                    2
                } else {
                    1
                },
            )
        } else {
            (ch, ch.len_utf8())
        };
        result.push(display);
        for _ in 1..display.len_utf8() {
            boundaries.push(position);
        }
        position += consumed;
        boundaries.push(position);
    }
    (result, boundaries)
}
fn preferred_ending(source: &str, at: usize) -> &'static str {
    let index = source[..at]
        .rfind(['\r', '\n'])
        .or_else(|| source.find(['\r', '\n']));
    match index {
        Some(i)
            if source.as_bytes()[i] == b'\r' && source.as_bytes().get(i + 1) == Some(&b'\n') =>
        {
            "\r\n"
        }
        Some(i) if source.as_bytes()[i] == b'\r' => "\r",
        Some(i) if i > 0 && source.as_bytes()[i - 1] == b'\r' => "\r\n",
        _ => "\n",
    }
}
fn save_atomic(path: &Path, bytes: &[u8], expected: Option<&[u8]>) -> Result<(), String> {
    let unchanged = || match (std::fs::read(path), expected) {
        (Ok(current), Some(expected)) if current == expected => Ok(()),
        (Err(error), None) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
        _ => Err(
            "File changed or became unavailable on disk; use Save As to preserve your edits"
                .to_string(),
        ),
    };
    unchanged()?;
    let parent = path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    let mut temporary = tempfile::NamedTempFile::new_in(parent).map_err(|e| e.to_string())?;
    if let Ok(metadata) = std::fs::metadata(path) {
        temporary
            .as_file()
            .set_permissions(metadata.permissions())
            .map_err(|e| e.to_string())?;
    }
    temporary.write_all(bytes).map_err(|e| e.to_string())?;
    temporary.as_file().sync_all().map_err(|e| e.to_string())?;
    unchanged()?;
    temporary.persist(path).map_err(|e| e.to_string())?;
    Ok(())
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn byte_preservation_and_undo_survive_dom_normalization() {
        let original = "\u{feff}# Wolf\r\n\r\n🐺 café  \nlast\r";
        let mut buffer = Buffer::new(original.into()).unwrap();
        let view = buffer.view();
        buffer.apply(&view).unwrap();
        assert_eq!(buffer.source, original);
        buffer.apply(&view.replace("café", "forest")).unwrap();
        assert_eq!(buffer.source, original.replace("café", "forest"));
        buffer.undo();
        assert_eq!(buffer.source, original);
        assert!(!buffer.dirty());
        buffer.redo();
        assert!(buffer.dirty());
    }
    #[test]
    fn new_lines_inherit_crlf_and_unicode_edits_are_safe() {
        let mut buffer = Buffer::new("one\r\n🐺\r\nend".into()).unwrap();
        buffer.apply("one\n🐺 new\nline\nend").unwrap();
        assert_eq!(buffer.source, "one\r\n🐺 new\r\nline\r\nend");
        assert!(buffer.apply("bad\0text").is_err());
    }
    #[test]
    fn save_conflicts_keep_dirty_source_and_do_not_overwrite_disk() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("test.md");
        std::fs::write(&path, "initial").unwrap();
        let mut buffer = Buffer::new("initial".into()).unwrap();
        buffer.apply("mine").unwrap();
        std::fs::write(&path, "external").unwrap();
        assert!(buffer.save(&path).is_err());
        assert!(buffer.dirty());
        assert_eq!(std::fs::read_to_string(&path).unwrap(), "external");
        let recovery = dir.path().join("recovery.md");
        buffer.save_as(&recovery, None).unwrap();
        assert_eq!(std::fs::read_to_string(&recovery).unwrap(), "mine");
        assert!(!buffer.dirty());
    }
}
