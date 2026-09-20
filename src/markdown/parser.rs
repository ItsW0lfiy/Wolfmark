use std::borrow::Cow;

use comrak::nodes::{AstNode, NodeValue};
use comrak::{Arena, Options, parse_document};

use super::model::{Block, DocumentModel, Inline, ListItem, TableRow, plain_text};

pub fn parse(source: &str) -> DocumentModel {
    let arena = Arena::new();
    let mut options = Options::default();
    options.extension.strikethrough = true;
    options.extension.table = true;
    options.extension.autolink = true;
    options.extension.tasklist = true;
    options.extension.footnotes = true;
    options.extension.front_matter_delimiter = Some("---".into());
    let compatible_source = normalize_literal_space_destinations(source);
    let root = parse_document(&arena, &compatible_source, &options);
    DocumentModel {
        blocks: block_children(root),
    }
}

/// CommonMark requires angle brackets around destinations containing spaces.
/// Wolfmark accepts the unbracketed form for local paths by normalizing only
/// the parser input; the semantic destination and original document stay intact.
fn normalize_literal_space_destinations(source: &str) -> Cow<'_, str> {
    let mut output = String::with_capacity(source.len());
    let mut changed = false;
    let mut fence: Option<(u8, usize)> = None;
    let mut inline_ticks = 0;

    for line in source.split_inclusive('\n') {
        if inline_ticks == 0
            && let Some((marker, length, can_close)) = fence_marker(line)
        {
            output.push_str(line);
            if fence.is_some_and(|active| active.0 == marker && length >= active.1 && can_close) {
                fence = None;
            } else if fence.is_none() {
                fence = Some((marker, length));
            }
            continue;
        }
        if fence.is_some() {
            output.push_str(line);
            continue;
        }
        changed |= normalize_line(line, &mut inline_ticks, &mut output);
    }

    if changed {
        Cow::Owned(output)
    } else {
        Cow::Borrowed(source)
    }
}

fn fence_marker(line: &str) -> Option<(u8, usize, bool)> {
    let bytes = line.as_bytes();
    let indent = bytes.iter().take_while(|byte| **byte == b' ').count();
    if indent > 3 {
        return None;
    }
    let marker = *bytes.get(indent)?;
    if !matches!(marker, b'`' | b'~') {
        return None;
    }
    let length = bytes[indent..]
        .iter()
        .take_while(|byte| **byte == marker)
        .count();
    let can_close = bytes[indent + length..]
        .iter()
        .all(|byte| byte.is_ascii_whitespace());
    (length >= 3).then_some((marker, length, can_close))
}

fn normalize_line(line: &str, inline_ticks: &mut usize, output: &mut String) -> bool {
    let bytes = line.as_bytes();
    let mut index = 0;
    let mut copied = 0;
    let mut changed = false;

    while index < bytes.len() {
        if bytes[index] == b'`' {
            let ticks = bytes[index..]
                .iter()
                .take_while(|byte| **byte == b'`')
                .count();
            if *inline_ticks == 0 {
                *inline_ticks = ticks;
            } else if *inline_ticks == ticks {
                *inline_ticks = 0;
            }
            index += ticks;
            continue;
        }
        if *inline_ticks != 0
            || bytes[index] != b']'
            || bytes.get(index + 1) != Some(&b'(')
            || is_escaped(bytes, index)
            || !has_link_label_start(bytes, index)
        {
            index += 1;
            continue;
        }
        let destination_start = index + 2;
        let Some(destination_end) = find_destination_end(bytes, destination_start) else {
            index += 1;
            continue;
        };
        let inner = &line[destination_start..destination_end];
        let Some(normalized) = normalize_destination(inner) else {
            index = destination_end + 1;
            continue;
        };
        output.push_str(&line[copied..destination_start]);
        output.push_str(&normalized);
        copied = destination_end;
        index = destination_end + 1;
        changed = true;
    }
    output.push_str(&line[copied..]);
    changed
}

fn has_link_label_start(bytes: &[u8], closing_bracket: usize) -> bool {
    let mut nested = 0;
    for index in (0..closing_bracket).rev() {
        if is_escaped(bytes, index) {
            continue;
        }
        match bytes[index] {
            b']' => nested += 1,
            b'[' if nested == 0 => return true,
            b'[' => nested -= 1,
            _ => {}
        }
    }
    false
}

fn is_escaped(bytes: &[u8], index: usize) -> bool {
    bytes[..index]
        .iter()
        .rev()
        .take_while(|byte| **byte == b'\\')
        .count()
        % 2
        == 1
}

fn find_destination_end(bytes: &[u8], start: usize) -> Option<usize> {
    let mut depth = 1;
    let mut escaped = false;
    for (offset, byte) in bytes[start..].iter().copied().enumerate() {
        let index = start + offset;
        if escaped {
            escaped = false;
            continue;
        }
        if byte == b'\\' {
            escaped = true;
            continue;
        }
        if matches!(byte, b'\n' | b'\r') {
            return None;
        }
        match byte {
            b'(' => depth += 1,
            b')' => {
                depth -= 1;
                if depth == 0 {
                    return Some(index);
                }
            }
            _ => {}
        }
    }
    None
}

fn normalize_destination(inner: &str) -> Option<String> {
    let trimmed = inner.trim();
    if trimmed.starts_with('<') || trimmed.contains(['\n', '\r', '<', '>']) {
        return None;
    }
    let (destination, title) = split_quoted_title(trimmed);
    if !destination.chars().any(char::is_whitespace) || !looks_like_local_path(destination) {
        return None;
    }
    let mut normalized = format!("<{destination}>");
    if let Some(title) = title {
        normalized.push(' ');
        normalized.push_str(title);
    }
    Some(normalized)
}

fn split_quoted_title(value: &str) -> (&str, Option<&str>) {
    for (index, character) in value.char_indices() {
        if !character.is_whitespace() {
            continue;
        }
        let title = value[index..].trim_start();
        let Some(quote) = title.chars().next() else {
            continue;
        };
        if !matches!(quote, '\"' | '\'') || !has_closing_quote(title, quote) {
            continue;
        }
        let destination = value[..index].trim_end();
        if !destination.is_empty() {
            return (destination, Some(title));
        }
    }
    (value, None)
}

fn has_closing_quote(value: &str, quote: char) -> bool {
    if !value.ends_with(quote) || value.len() < quote.len_utf8() * 2 {
        return false;
    }
    let mut escaped = false;
    let mut closing = None;
    for (index, character) in value.char_indices().skip(1) {
        if escaped {
            escaped = false;
        } else if character == '\\' {
            escaped = true;
        } else if character == quote {
            closing = Some(index);
            break;
        }
    }
    closing == Some(value.len() - quote.len_utf8())
}

fn looks_like_local_path(destination: &str) -> bool {
    if destination.starts_with('#') || destination.starts_with("//") {
        return false;
    }
    let bytes = destination.as_bytes();
    if bytes.len() >= 3
        && bytes[0].is_ascii_alphabetic()
        && bytes[1] == b':'
        && matches!(bytes[2], b'/' | b'\\')
    {
        return true;
    }
    let Some(colon) = destination.find(':') else {
        return true;
    };
    let scheme = &destination[..colon];
    if scheme.eq_ignore_ascii_case("file") {
        return true;
    }
    !is_uri_scheme(scheme)
}

fn is_uri_scheme(value: &str) -> bool {
    let mut characters = value.chars();
    characters
        .next()
        .is_some_and(|first| first.is_ascii_alphabetic())
        && characters.all(|character| {
            character.is_ascii_alphanumeric() || matches!(character, '+' | '-' | '.')
        })
}

fn block_children<'a>(node: &'a AstNode<'a>) -> Vec<Block> {
    node.children().filter_map(parse_block).collect()
}

fn parse_block<'a>(node: &'a AstNode<'a>) -> Option<Block> {
    let value = node.data.borrow().value.clone();
    match value {
        NodeValue::Paragraph => Some(Block::Paragraph(inline_children(node))),
        NodeValue::Heading(heading) => Some(Block::Heading {
            level: heading.level,
            content: inline_children(node),
        }),
        NodeValue::BlockQuote => Some(Block::Quote(block_children(node))),
        NodeValue::List(list) => {
            let items = node
                .children()
                .filter_map(|item| match &item.data.borrow().value {
                    NodeValue::Item(_) => {
                        let blocks = block_children(item);
                        let checked = first_task_state(&blocks);
                        Some(ListItem { checked, blocks })
                    }
                    NodeValue::TaskItem(task) => Some(ListItem {
                        checked: Some(task.symbol.is_some()),
                        blocks: block_children(item),
                    }),
                    _ => None,
                })
                .collect();
            Some(Block::List {
                ordered: list.list_type == comrak::nodes::ListType::Ordered,
                start: list.start as u64,
                items,
            })
        }
        NodeValue::CodeBlock(code) => Some(Block::CodeBlock {
            language: code
                .info
                .split_whitespace()
                .next()
                .unwrap_or_default()
                .to_owned(),
            source: code.literal,
        }),
        NodeValue::ThematicBreak => Some(Block::HorizontalRule),
        NodeValue::Table(_) => Some(parse_table(node)),
        NodeValue::HtmlBlock(html) => Some(Block::RawHtml(html.literal)),
        NodeValue::FrontMatter(source) => Some(Block::RawHtml(source)),
        NodeValue::FootnoteDefinition(definition) => {
            let mut content = vec![Inline::Strong(vec![Inline::Text(format!(
                "{}: ",
                definition.name
            ))])];
            for block in block_children(node) {
                if let Block::Paragraph(inlines) = block {
                    content.extend(inlines);
                }
            }
            Some(Block::Paragraph(content))
        }
        _ => None,
    }
}

fn parse_table<'a>(node: &'a AstNode<'a>) -> Block {
    use super::model::TableAlignment;
    let alignments = match &node.data.borrow().value {
        NodeValue::Table(table) => table
            .alignments
            .iter()
            .map(|alignment| match alignment {
                comrak::nodes::TableAlignment::Center => TableAlignment::Center,
                comrak::nodes::TableAlignment::Right => TableAlignment::Right,
                _ => TableAlignment::Left,
            })
            .collect(),
        _ => Vec::new(),
    };
    let mut rows = node.children().map(parse_table_row);
    Block::Table {
        header: rows.next().unwrap_or_default(),
        rows: rows.collect(),
        alignments,
    }
}

fn parse_table_row<'a>(node: &'a AstNode<'a>) -> TableRow {
    TableRow {
        cells: node.children().map(inline_children).collect(),
    }
}

fn inline_children<'a>(node: &'a AstNode<'a>) -> Vec<Inline> {
    node.children().filter_map(parse_inline).collect()
}

fn parse_inline<'a>(node: &'a AstNode<'a>) -> Option<Inline> {
    let value = node.data.borrow().value.clone();
    match value {
        NodeValue::Text(text) => Some(Inline::Text(text.into_owned())),
        NodeValue::Emph => Some(Inline::Emphasis(inline_children(node))),
        NodeValue::Strong => Some(Inline::Strong(inline_children(node))),
        NodeValue::Strikethrough => Some(Inline::Strikethrough(inline_children(node))),
        NodeValue::Code(code) => Some(Inline::Code(code.literal)),
        NodeValue::Link(link) => Some(Inline::Link {
            destination: link.url,
            content: inline_children(node),
        }),
        NodeValue::Image(link) => {
            let children = inline_children(node);
            Some(Inline::Image {
                source: link.url,
                title: link.title,
                alt: plain_text(&children),
            })
        }
        NodeValue::SoftBreak => Some(Inline::SoftBreak),
        NodeValue::LineBreak => Some(Inline::HardBreak),
        NodeValue::HtmlInline(source) => Some(Inline::RawHtml(source)),
        NodeValue::TaskItem(marker) => Some(Inline::TaskMark(marker.symbol.is_some())),
        NodeValue::FootnoteReference(reference) => {
            Some(Inline::Text(format!("[{}]", reference.name)))
        }
        _ => {
            let children = inline_children(node);
            (!children.is_empty()).then_some(Inline::Text(plain_text(&children)))
        }
    }
}

fn first_task_state(blocks: &[Block]) -> Option<bool> {
    let Block::Paragraph(inlines) = blocks.first()? else {
        return None;
    };
    match inlines.first() {
        Some(Inline::TaskMark(value)) => Some(*value),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_gfm_structure_without_html_rendering() {
        let document =
            parse("# Heading\n\n- [x] task\n  - nested\n\n| A | B |\n|---|---|\n| 1 | 2 |\n");
        assert!(matches!(
            document.blocks[0],
            Block::Heading { level: 1, .. }
        ));
        assert!(
            document
                .blocks
                .iter()
                .any(|block| matches!(block, Block::List { .. }))
        );
        let Block::List { items, .. } = &document.blocks[1] else {
            panic!("expected task list");
        };
        assert_eq!(items[0].checked, Some(true));
        assert!(!items[0].blocks.is_empty());
        assert!(
            document
                .blocks
                .iter()
                .any(|block| matches!(block, Block::Table { .. }))
        );
    }

    #[test]
    fn preserves_links_images_and_code() {
        let document = parse(
            "[Wolfmark](https://example.invalid) ![alt](images/a.png) `code`\n\n```rust\nlet x = 1;\n```",
        );
        assert_eq!(document.blocks.len(), 2);
        assert!(matches!(document.blocks[1], Block::CodeBlock { .. }));
    }

    #[test]
    fn preserves_commonmark_breaks_escaping_entities_and_nested_blocks() {
        let document = parse(
            "soft\nline  \nhard\n\n\
             \\*literal punctuation\\* &copy; &amp;\n\n\
             > outer\n>\n> > inner\n\n\
             - parent\n  - child",
        );
        let Block::Paragraph(breaks) = &document.blocks[0] else {
            panic!("expected break paragraph");
        };
        assert!(
            breaks
                .iter()
                .any(|inline| matches!(inline, Inline::SoftBreak))
        );
        assert!(
            breaks
                .iter()
                .any(|inline| matches!(inline, Inline::HardBreak))
        );

        let Block::Paragraph(escaped) = &document.blocks[1] else {
            panic!("expected escaped punctuation paragraph");
        };
        assert_eq!(plain_text(escaped), "*literal punctuation* © &");
        assert!(escaped.iter().all(|inline| !matches!(
            inline,
            Inline::Emphasis(_) | Inline::Strong(_) | Inline::Link { .. }
        )));

        let quote = document
            .blocks
            .iter()
            .find_map(|block| match block {
                Block::Quote(children) => Some(children),
                _ => None,
            })
            .expect("outer quote");
        assert!(quote.iter().any(|block| matches!(block, Block::Quote(_))));

        let list = document
            .blocks
            .iter()
            .find_map(|block| match block {
                Block::List { items, .. } => Some(items),
                _ => None,
            })
            .expect("outer list");
        assert!(
            list[0]
                .blocks
                .iter()
                .any(|block| matches!(block, Block::List { .. }))
        );
    }

    #[test]
    fn preserves_code_info_and_keeps_inline_and_block_html_inert() {
        let document = parse(
            "Before <span title=\"example\">inline</span>.\n\n\
             ```rust additional-info\nfn example() {}\n```\n\n\
             <div data-kind=\"inert\">\nblock HTML\n</div>",
        );
        let Block::Paragraph(inlines) = &document.blocks[0] else {
            panic!("expected inline HTML paragraph");
        };
        assert!(inlines.iter().any(
            |inline| matches!(inline, Inline::RawHtml(source) if source.starts_with("<span"))
        ));
        assert!(
            inlines
                .iter()
                .any(|inline| matches!(inline, Inline::RawHtml(source) if source == "</span>"))
        );
        assert!(document.blocks.iter().any(|block| matches!(
            block,
            Block::CodeBlock { language, source }
                if language == "rust" && source == "fn example() {}\n"
        )));
        assert!(document.blocks.iter().any(|block| matches!(
            block,
            Block::RawHtml(source)
                if source.contains("data-kind=\"inert\"") && source.contains("block HTML")
        )));
    }

    #[test]
    fn accepts_literal_spaces_in_local_image_destinations() {
        let document = parse(
            "![literal](images/image with spaces.png)\n\n\
             ![ordinary](images/test.webp \"Example\")\n\n\
             ![encoded](images/image%20with%20spaces.png)\n\n\
             ![bracketed](<images/image with spaces.png>)\n\n\
             ![titled](images/image with spaces.png \"Example\")",
        );
        let images = document
            .blocks
            .iter()
            .filter_map(|block| match block {
                Block::Paragraph(inlines) => inlines.first(),
                _ => None,
            })
            .filter_map(|inline| match inline {
                Inline::Image { source, title, .. } => Some((source.as_str(), title.as_str())),
                _ => None,
            })
            .collect::<Vec<_>>();
        assert_eq!(
            images,
            [
                ("images/image with spaces.png", ""),
                ("images/test.webp", "Example"),
                ("images/image%20with%20spaces.png", ""),
                ("images/image with spaces.png", ""),
                ("images/image with spaces.png", "Example"),
            ]
        );
    }

    #[test]
    fn accepts_literal_spaces_in_local_link_destinations() {
        let document = parse(
            "[Document](Some Folder/Document Name.md) \
             [Titled](  Some Folder/Titled Document.md  \"Document title\"  ) \
             [Parent](../../Other Folder/Some Document.md)",
        );
        let Block::Paragraph(inlines) = &document.blocks[0] else {
            panic!("expected paragraph");
        };
        let destinations = inlines
            .iter()
            .filter_map(|inline| match inline {
                Inline::Link { destination, .. } => Some(destination.as_str()),
                _ => None,
            })
            .collect::<Vec<_>>();
        assert_eq!(
            destinations,
            [
                "Some Folder/Document Name.md",
                "Some Folder/Titled Document.md",
                "../../Other Folder/Some Document.md"
            ]
        );
    }

    #[test]
    fn preserves_complete_windows_and_file_uri_destinations_with_spaces() {
        let document = parse(
            "![drive](C:/Other Folder/image.png) \
             ![uri](file:///C:/Other Folder/image.png) \
             ![separator](images\\Other Folder\\image.png)",
        );
        let Block::Paragraph(inlines) = &document.blocks[0] else {
            panic!("expected paragraph");
        };
        let destinations = inlines
            .iter()
            .filter_map(|inline| match inline {
                Inline::Image { source, .. } => Some(source.as_str()),
                _ => None,
            })
            .collect::<Vec<_>>();
        assert_eq!(
            destinations,
            [
                "C:/Other Folder/image.png",
                "file:///C:/Other Folder/image.png",
                "images\\Other Folder\\image.png"
            ]
        );
    }

    #[test]
    fn leaves_remote_reference_and_code_syntax_unchanged() {
        let document = parse(
            "[Website](https://example.com) [Malformed](https://example.com/a b) \
             [Reference][example]\n\n\
             `[Code](Some Folder/File.md)`\n\n\
             ```text\n![Code image](images/image with spaces.png)\n```\n\n\
             [example]: https://example.com/reference",
        );
        let links = document
            .blocks
            .iter()
            .filter_map(|block| match block {
                Block::Paragraph(inlines) => Some(inlines),
                _ => None,
            })
            .flatten()
            .filter_map(|inline| match inline {
                Inline::Link { destination, .. } => Some(destination.as_str()),
                _ => None,
            })
            .collect::<Vec<_>>();
        assert_eq!(
            links,
            [
                "https://example.com",
                "https://example.com/a",
                "https://example.com/reference"
            ]
        );
        let code = document.blocks.iter().find_map(|block| match block {
            Block::CodeBlock { source, .. } => Some(source.as_str()),
            _ => None,
        });
        assert_eq!(code, Some("![Code image](images/image with spaces.png)\n"));
    }

    #[test]
    fn leaves_escaped_syntax_and_nonclosing_fence_content_unchanged() {
        let source = "\\![escaped](Some Folder/Image.png)\n\n\
                      ```text\n\
                      ```not a closing fence\n\
                      ![Code image](images/image with spaces.png)\n\
                      ```";
        let document = parse(source);
        assert!(document.blocks.iter().all(|block| !matches!(
            block,
            Block::Paragraph(inlines)
                if inlines.iter().any(|inline| matches!(inline, Inline::Image { .. }))
        )));
        let code = document.blocks.iter().find_map(|block| match block {
            Block::CodeBlock { source, .. } => Some(source.as_str()),
            _ => None,
        });
        assert_eq!(
            code,
            Some("```not a closing fence\n![Code image](images/image with spaces.png)\n")
        );
        assert!(matches!(
            normalize_literal_space_destinations("suffix](Some Folder/File.md)"),
            Cow::Borrowed(_)
        ));
    }
}
