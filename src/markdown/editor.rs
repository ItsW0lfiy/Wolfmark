//! Source-only Comrak decorations. Offsets are UTF-16 units for native Qt cursors;
//! no rendered text is ever serialized back into the source buffer.

use comrak::nodes::{AstNode, NodeValue};
use comrak::{Arena, parse_document};
use serde::Serialize;

#[derive(Debug, Serialize)]
pub struct Decoration {
    pub start: usize,
    pub end: usize,
    pub kind: &'static str,
    pub level: u8,
    pub markers: Vec<[usize; 2]>,
}

pub fn decorations(source: &str) -> Vec<Decoration> {
    let arena = Arena::new();
    // Use original input: the Reader's local-path compatibility normalization
    // adds characters and must never be used for source offset mapping.
    let root = parse_document(&arena, source, &super::parser::options());
    let mut offsets = vec![0; source.len() + 1];
    let mut units = 0;
    for (byte, character) in source.char_indices() {
        for offset in &mut offsets[byte..byte + character.len_utf8()] {
            *offset = units;
        }
        units += character.len_utf16();
        offsets[byte + character.len_utf8()] = units;
    }
    let lines: Vec<usize> = std::iter::once(0)
        .chain(source.match_indices('\n').map(|(offset, _)| offset + 1))
        .collect();
    let byte_offset = |line: usize, column: usize, inclusive: bool| {
        let start = *lines.get(line.checked_sub(1)?)?;
        let limit = lines.get(line).copied().unwrap_or(source.len());
        Some((start + column.saturating_sub(usize::from(!inclusive))).min(limit))
    };
    let mut result = Vec::new();
    for node in root.descendants() {
        let data = node.data();
        let pos = data.sourcepos;
        let (Some(start), Some(end)) = (
            byte_offset(pos.start.line, pos.start.column, false),
            byte_offset(pos.end.line, pos.end.column, true),
        ) else {
            continue;
        };
        if start >= end || !source.is_char_boundary(start) || !source.is_char_boundary(end) {
            continue;
        }
        let text = &source[start..end];
        let mut markers = Vec::new();
        let mut level = 0;
        let kind = match &data.value {
            NodeValue::Heading(heading) => {
                level = heading.level;
                if text.starts_with('#') {
                    let length = text.bytes().take_while(|byte| *byte == b'#').count();
                    if length <= 6
                        && text
                            .as_bytes()
                            .get(length)
                            .is_some_and(u8::is_ascii_whitespace)
                    {
                        markers.push([start, start + length + 1]);
                    }
                }
                "heading"
            }
            NodeValue::Strong => {
                paired_markers(text, start, end, 2, &mut markers);
                "strong"
            }
            NodeValue::Emph => {
                paired_markers(text, start, end, 1, &mut markers);
                "emphasis"
            }
            NodeValue::Strikethrough => {
                paired_markers(text, start, end, 2, &mut markers);
                "strike"
            }
            NodeValue::Code(_) => {
                let count = text.bytes().take_while(|byte| *byte == b'`').count();
                if count > 0 && text.ends_with(&"`".repeat(count)) && text.len() >= count * 2 {
                    markers.extend([[start, start + count], [end - count, end]]);
                }
                "code"
            }
            NodeValue::Link(_) => {
                // Only hide unambiguous inline-link syntax; references,
                // autolinks, images, and malformed/unsupported forms stay raw.
                if text.starts_with('[')
                    && text.ends_with(')')
                    && let Some(close) = link_label_end(node, &lines)
                    && close < end
                    && source.get(close..close + 2) == Some("](")
                {
                    markers.extend([[start, start + 1], [close, end]]);
                }
                "link"
            }
            NodeValue::CodeBlock(_) => "fence",
            NodeValue::BlockQuote => "quote",
            NodeValue::Item(_) | NodeValue::TaskItem(_) => "list",
            NodeValue::Table(_) => "table",
            NodeValue::HtmlBlock(_) | NodeValue::HtmlInline(_) | NodeValue::FrontMatter(_) => "raw",
            _ => continue,
        };
        result.push(Decoration {
            start: offsets[start],
            end: offsets[end],
            kind,
            level,
            markers: markers
                .into_iter()
                .map(|[a, b]| [offsets[a], offsets[b]])
                .collect(),
        });
    }
    result
}

fn paired_markers(
    text: &str,
    start: usize,
    end: usize,
    count: usize,
    markers: &mut Vec<[usize; 2]>,
) {
    if text.len() >= count * 2 {
        let prefix = &text.as_bytes()[..count];
        if prefix.iter().all(|byte| matches!(byte, b'*' | b'_' | b'~'))
            && text.as_bytes().ends_with(prefix)
        {
            markers.extend([[start, start + count], [end - count, end]]);
        }
    }
}

fn link_label_end<'a>(node: &'a AstNode<'a>, lines: &[usize]) -> Option<usize> {
    let last = node.last_child()?.data().sourcepos.end;
    Some(*lines.get(last.line.checked_sub(1)?)? + last.column)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn offsets_use_utf16_and_markers_never_replace_source() {
        let source = "# 月🐺\n\n**bold _nested_** and [月](../月.md) `code`\n";
        let spans = decorations(source);
        assert!(
            spans
                .iter()
                .any(|span| span.kind == "heading" && span.end == 5)
        );
        assert!(
            spans
                .iter()
                .any(|span| span.kind == "strong" && span.markers.len() == 2)
        );
        assert!(spans.iter().any(|span| span.kind == "emphasis"));
        assert!(
            spans
                .iter()
                .any(|span| span.kind == "link" && span.markers.len() == 2)
        );
        for span in spans {
            assert!(span.start < span.end && span.end <= source.encode_utf16().count());
            for [start, end] in span.markers {
                assert!(start < end && end <= span.end);
            }
        }
    }

    #[test]
    fn complex_constructs_remain_source_visible() {
        let source = "---\nx: yes\n---\n\n- [x] task\n\n```rust\nlet x = 1;\n```\n\n| A | B |\n|---|---|\n\n<div>inert</div>\n";
        let spans = decorations(source);
        for kind in ["raw", "list", "fence", "table"] {
            assert!(
                spans
                    .iter()
                    .any(|span| span.kind == kind && span.markers.is_empty()),
                "{kind}"
            );
        }
    }
}
