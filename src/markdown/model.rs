#[derive(Clone, Debug, Default, PartialEq)]
pub struct DocumentModel {
    pub blocks: Vec<Block>,
    pub source_lines: Vec<usize>,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Block {
    Paragraph(Vec<Inline>),
    Heading {
        level: u8,
        content: Vec<Inline>,
    },
    Quote(Vec<Block>),
    List {
        ordered: bool,
        start: u64,
        items: Vec<ListItem>,
    },
    CodeBlock {
        language: String,
        source: String,
    },
    Table {
        header: TableRow,
        rows: Vec<TableRow>,
        alignments: Vec<TableAlignment>,
    },
    HorizontalRule,
    RawHtml(String),
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct ListItem {
    pub checked: Option<bool>,
    pub blocks: Vec<Block>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct TableRow {
    pub cells: Vec<Vec<Inline>>,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub enum TableAlignment {
    #[default]
    Left,
    Center,
    Right,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Inline {
    Text(String),
    Emphasis(Vec<Inline>),
    Strong(Vec<Inline>),
    Strikethrough(Vec<Inline>),
    Code(String),
    Link {
        destination: String,
        content: Vec<Inline>,
    },
    Image {
        source: String,
        title: String,
        alt: String,
    },
    SoftBreak,
    HardBreak,
    RawHtml(String),
    TaskMark(bool),
}

pub fn plain_text(inlines: &[Inline]) -> String {
    let mut output = String::new();
    for inline in inlines {
        match inline {
            Inline::Text(text) | Inline::Code(text) | Inline::RawHtml(text) => {
                output.push_str(text)
            }
            Inline::Emphasis(children)
            | Inline::Strong(children)
            | Inline::Strikethrough(children)
            | Inline::Link {
                content: children, ..
            } => output.push_str(&plain_text(children)),
            Inline::Image { alt, .. } => output.push_str(alt),
            Inline::SoftBreak | Inline::HardBreak => output.push(' '),
            Inline::TaskMark(checked) => output.push_str(if *checked { "[x] " } else { "[ ] " }),
        }
    }
    output
}
