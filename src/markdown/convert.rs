use std::path::Path;
use std::time::Instant;
use std::{collections::HashMap, mem};

use crate::images::{
    AssetState, ImageDimensions, ImageRequest, probe_dimensions, resolve_local_image,
};
use crate::presentation::{
    PresentationCommand, PresentationDocument, PresentationMetrics, PresentationSettings, TocEntry,
};
use crate::settings::Settings;

use super::highlight::highlight_code;
use super::model::{Block, DocumentModel, Inline, ListItem, TableRow};

pub mod kind {
    pub const BEGIN_PARAGRAPH: u8 = 1;
    pub const BEGIN_HEADING: u8 = 2;
    pub const END_BLOCK: u8 = 3;
    pub const TEXT: u8 = 4;
    pub const SOFT_BREAK: u8 = 5;
    pub const HARD_BREAK: u8 = 6;
    pub const BEGIN_LIST: u8 = 7;
    pub const END_LIST: u8 = 8;
    pub const BEGIN_ITEM: u8 = 9;
    pub const END_ITEM: u8 = 10;
    pub const CODE_BLOCK: u8 = 11;
    pub const HORIZONTAL_RULE: u8 = 12;
    pub const BEGIN_QUOTE: u8 = 13;
    pub const END_QUOTE: u8 = 14;
    pub const BEGIN_TABLE: u8 = 15;
    pub const BEGIN_ROW: u8 = 16;
    pub const BEGIN_CELL: u8 = 17;
    pub const END_CELL: u8 = 18;
    pub const END_ROW: u8 = 19;
    pub const END_TABLE: u8 = 20;
    pub const IMAGE: u8 = 21;
    pub const RAW_HTML: u8 = 22;
}

pub mod style {
    pub const EMPHASIS: u16 = 1;
    pub const STRONG: u16 = 2;
    pub const STRIKE: u16 = 4;
    pub const CODE: u16 = 8;
    pub const LINK: u16 = 16;
    pub const ORDERED: u16 = 32;
    pub const CHECKED: u16 = 64;
    pub const UNCHECKED: u16 = 128;
    pub const HEADER: u16 = 256;
}

pub fn to_presentation(
    model: &DocumentModel,
    document_path: &Path,
    mut metrics: PresentationMetrics,
    settings: &Settings,
) -> (PresentationDocument, Vec<ImageRequest>) {
    let started = Instant::now();
    let mut output = Converter {
        document_path,
        commands: Vec::new(),
        images: Vec::new(),
        image_assets: HashMap::new(),
        image_dimensions: HashMap::new(),
        next_image: 1,
        toc: Vec::new(),
        heading_counts: HashMap::new(),
    };
    output.blocks(&model.blocks);
    let image_ids = output
        .commands
        .iter()
        .filter(|command| command.kind == kind::IMAGE)
        .filter_map(|command| u32::try_from(command.number).ok())
        .collect();
    let toc = mem::take(&mut output.toc);
    let presentation_us = u64::try_from(started.elapsed().as_micros()).unwrap_or(u64::MAX);
    metrics.presentation_us = presentation_us;
    let document = PresentationDocument {
        title: document_path
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or("Wolfmark")
            .to_owned(),
        source_type: "markdown".into(),
        literal_text: String::new(),
        error: String::new(),
        commands: output.commands,
        image_ids,
        toc,
        metrics,
        settings: PresentationSettings {
            body_font_points: f64::from(settings.body_font_points),
            document_padding: settings.document_padding,
            line_height_percent: settings.line_height_percent,
        },
    };
    (document, output.images)
}

struct Converter<'a> {
    document_path: &'a Path,
    commands: Vec<PresentationCommand>,
    images: Vec<ImageRequest>,
    image_assets: HashMap<String, u32>,
    image_dimensions: HashMap<String, Option<ImageDimensions>>,
    next_image: u32,
    toc: Vec<TocEntry>,
    heading_counts: HashMap<String, u32>,
}

impl Converter<'_> {
    fn blocks(&mut self, blocks: &[Block]) {
        for block in blocks {
            self.block(block);
        }
    }

    fn block(&mut self, block: &Block) {
        match block {
            Block::Paragraph(inlines) => {
                if let [Inline::Image { source, title, alt }] = inlines.as_slice() {
                    self.image(source, title, alt);
                    return;
                }
                self.push(kind::BEGIN_PARAGRAPH, 0, 0, "", "", "", 0);
                self.inlines(inlines, 0);
                self.push(kind::END_BLOCK, 0, 0, "", "", "", 0);
            }
            Block::Heading { level, content } => {
                let title = super::model::plain_text(content);
                let anchor = self.next_heading_anchor(&title);
                self.toc.push(TocEntry {
                    level: *level,
                    title,
                    anchor: anchor.clone(),
                });
                self.push(kind::BEGIN_HEADING, *level, 0, "", &anchor, "", 0);
                self.inlines(content, 0);
                self.push(kind::END_BLOCK, 0, 0, "", "", "", 0);
            }
            Block::Quote(children) => {
                self.push(kind::BEGIN_QUOTE, 0, 0, "", "", "", 0);
                self.blocks(children);
                self.push(kind::END_QUOTE, 0, 0, "", "", "", 0);
            }
            Block::List {
                ordered,
                start,
                items,
            } => {
                self.push(
                    kind::BEGIN_LIST,
                    0,
                    if *ordered { style::ORDERED } else { 0 },
                    "",
                    "",
                    "",
                    *start as i64,
                );
                for (index, item) in items.iter().enumerate() {
                    let marker = if *ordered {
                        format!("{}.", start.saturating_add(index as u64))
                    } else {
                        "•".to_owned()
                    };
                    self.list_item(item, &marker);
                }
                self.push(kind::END_LIST, 0, 0, "", "", "", 0);
            }
            Block::CodeBlock { language, source } => {
                self.push(kind::CODE_BLOCK, 0, 0, "", "", language, 0);
                if let Some(command) = self.commands.last_mut() {
                    command.spans = highlight_code(language, source);
                }
            }
            Block::Table {
                header,
                rows,
                alignments,
            } => {
                self.push(
                    kind::BEGIN_TABLE,
                    0,
                    0,
                    "",
                    "",
                    "",
                    header.cells.len() as i64,
                );
                self.table_row(header, true, alignments);
                for row in rows {
                    self.table_row(row, false, alignments);
                }
                self.push(kind::END_TABLE, 0, 0, "", "", "", 0);
            }
            Block::HorizontalRule => self.push(kind::HORIZONTAL_RULE, 0, 0, "", "", "", 0),
            Block::RawHtml(source) => self.push(kind::RAW_HTML, 0, 0, source, "", "", 0),
        }
    }

    fn list_item(&mut self, item: &ListItem, marker: &str) {
        let flags = match item.checked {
            Some(true) => style::CHECKED,
            Some(false) => style::UNCHECKED,
            None => 0,
        };
        self.push(kind::BEGIN_ITEM, 0, flags, marker, "", "", 0);
        self.blocks(&item.blocks);
        self.push(kind::END_ITEM, 0, 0, "", "", "", 0);
    }

    fn table_row(
        &mut self,
        row: &TableRow,
        header: bool,
        alignments: &[super::model::TableAlignment],
    ) {
        self.push(
            kind::BEGIN_ROW,
            0,
            if header { style::HEADER } else { 0 },
            "",
            "",
            "",
            0,
        );
        for (column, cell) in row.cells.iter().enumerate() {
            use super::model::TableAlignment;
            let alignment = match alignments.get(column).copied().unwrap_or_default() {
                TableAlignment::Left => 0,
                TableAlignment::Center => 1,
                TableAlignment::Right => 2,
            };
            self.push(kind::BEGIN_CELL, 0, 0, "", "", "", alignment);
            self.inlines(cell, if header { style::STRONG } else { 0 });
            self.push(kind::END_CELL, 0, 0, "", "", "", 0);
        }
        self.push(kind::END_ROW, 0, 0, "", "", "", 0);
    }

    fn inlines(&mut self, inlines: &[Inline], inherited: u16) {
        for inline in inlines {
            match inline {
                Inline::Text(text) => self.push(kind::TEXT, 0, inherited, text, "", "", 0),
                Inline::Emphasis(children) => self.inlines(children, inherited | style::EMPHASIS),
                Inline::Strong(children) => self.inlines(children, inherited | style::STRONG),
                Inline::Strikethrough(children) => {
                    self.inlines(children, inherited | style::STRIKE)
                }
                Inline::Code(text) => {
                    self.push(kind::TEXT, 0, inherited | style::CODE, text, "", "", 0)
                }
                Inline::Link {
                    destination,
                    content,
                } => {
                    for child in content {
                        self.inline_link(child, inherited | style::LINK, destination);
                    }
                }
                Inline::Image { source, title, alt } => self.image(source, title, alt),
                Inline::SoftBreak => self.push(kind::SOFT_BREAK, 0, 0, "", "", "", 0),
                Inline::HardBreak => self.push(kind::HARD_BREAK, 0, 0, "", "", "", 0),
                Inline::RawHtml(source) => {
                    self.push(kind::RAW_HTML, 0, inherited, source, "", "", 0)
                }
                Inline::TaskMark(_) => {}
            }
        }
    }

    fn inline_link(&mut self, inline: &Inline, flags: u16, destination: &str) {
        match inline {
            Inline::Text(text) | Inline::Code(text) => {
                self.push(kind::TEXT, 0, flags, text, destination, "", 0)
            }
            Inline::Emphasis(children) => {
                for child in children {
                    self.inline_link(child, flags | style::EMPHASIS, destination);
                }
            }
            Inline::Strong(children) => {
                for child in children {
                    self.inline_link(child, flags | style::STRONG, destination);
                }
            }
            Inline::Strikethrough(children) => {
                for child in children {
                    self.inline_link(child, flags | style::STRIKE, destination);
                }
            }
            _ => self.push(kind::TEXT, 0, flags, "link", destination, "", 0),
        }
    }

    fn image(&mut self, source: &str, title: &str, alt: &str) {
        let resolution = resolve_local_image(self.document_path, source);
        let state: u16 = match resolution.state {
            AssetState::Allowed => 0,
            AssetState::Missing => 1,
            AssetState::Blocked => 2,
            AssetState::Remote => 3,
            AssetState::Unsupported => 4,
        };
        let path = resolution
            .canonical_path
            .as_ref()
            .map_or_else(String::new, |path| path.to_string_lossy().into_owned());
        let dimensions = if state == 0 {
            *self
                .image_dimensions
                .entry(path.clone())
                .or_insert_with(|| probe_dimensions(Path::new(&path)).ok())
        } else {
            None
        };
        let id = if state == 0 {
            if let Some(id) = self.image_assets.get(&path) {
                *id
            } else {
                let id = self.take_image_id();
                self.image_assets.insert(path.clone(), id);
                self.images.push(ImageRequest {
                    id,
                    path,
                    max_width: 1800,
                    intrinsic_width: dimensions.map_or(0, |size| size.width),
                    intrinsic_height: dimensions.map_or(0, |size| size.height),
                });
                id
            }
        } else {
            self.take_image_id()
        };
        self.push(
            kind::IMAGE,
            0,
            state,
            alt,
            &resolution.display_path,
            title,
            i64::from(id),
        );
        if let Some(command) = self.commands.last_mut()
            && let Some(dimensions) = dimensions
        {
            command.image_width = dimensions.width;
            command.image_height = dimensions.height;
        }
    }

    fn take_image_id(&mut self) -> u32 {
        let id = self.next_image;
        self.next_image = self.next_image.saturating_add(1);
        id
    }

    fn next_heading_anchor(&mut self, title: &str) -> String {
        let base = heading_slug(title);
        let count = self.heading_counts.entry(base.clone()).or_default();
        let anchor = if *count == 0 {
            base
        } else {
            format!("{base}-{count}")
        };
        *count = count.saturating_add(1);
        anchor
    }

    #[allow(clippy::too_many_arguments)]
    fn push(
        &mut self,
        kind: u8,
        level: u8,
        flags: u16,
        text: &str,
        target: &str,
        extra: &str,
        number: i64,
    ) {
        self.commands.push(PresentationCommand {
            kind,
            level,
            flags,
            text: text.to_owned(),
            target: target.to_owned(),
            extra: extra.to_owned(),
            number,
            image_width: 0,
            image_height: 0,
            spans: Vec::new(),
        });
    }
}

fn heading_slug(title: &str) -> String {
    let mut slug = String::new();
    let mut pending_dash = false;
    for character in title.chars().flat_map(char::to_lowercase) {
        if character.is_alphanumeric() || character == '_' {
            if pending_dash && !slug.is_empty() {
                slug.push('-');
            }
            slug.push(character);
            pending_dash = false;
        } else if character.is_whitespace() || character == '-' {
            pending_dash = true;
        }
    }
    if slug.is_empty() {
        "section".into()
    } else {
        slug
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::markdown::parse;

    #[test]
    fn table_alignment_survives_the_framework_neutral_boundary() {
        let model = parse("| Left | Center | Right |\n|:---|:---:|---:|\n| one | two | three |\n");
        let (presentation, _) = to_presentation(
            &model,
            std::path::Path::new("table.md"),
            PresentationMetrics::default(),
            &Settings::default(),
        );
        let alignment: Vec<_> = presentation
            .commands
            .iter()
            .filter(|command| command.kind == kind::BEGIN_CELL)
            .map(|command| command.number)
            .collect();
        assert_eq!(alignment, [0, 1, 2, 0, 1, 2]);
    }

    #[test]
    fn repeated_image_references_share_one_decode_request() {
        let root =
            std::env::temp_dir().join(format!("wolfmark-duplicate-image-{}", std::process::id()));
        std::fs::create_dir_all(&root).expect("create test directory");
        let document_path = root.join("document.md");
        let image_path = root.join("shared.png");
        std::fs::write(&document_path, b"").expect("write document");
        std::fs::write(&image_path, b"fixture").expect("write image fixture");

        let model = parse("![first](shared.png)\n\n![second](shared.png)");
        let (presentation, requests) = to_presentation(
            &model,
            &document_path,
            PresentationMetrics {
                revision: 1,
                ..PresentationMetrics::default()
            },
            &Settings::default(),
        );
        let ids = presentation
            .commands
            .iter()
            .filter(|command| command.kind == kind::IMAGE)
            .map(|command| command.number)
            .collect::<Vec<_>>();

        assert_eq!(ids.len(), 2);
        assert_eq!(ids[0], ids[1]);
        assert_eq!(requests.len(), 1);
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn reserves_intrinsic_image_geometry_before_decode() {
        let root =
            std::env::temp_dir().join(format!("wolfmark-image-geometry-{}", std::process::id()));
        std::fs::create_dir_all(&root).expect("create test directory");
        let document_path = root.join("document.md");
        let image_path = root.join("image with spaces.png");
        std::fs::write(&document_path, b"").expect("write document");
        image::RgbaImage::new(1280, 720)
            .save(&image_path)
            .expect("write image fixture");

        let model = parse("Text\n\n![image](image with spaces.png)\n\nText below");
        let (presentation, requests) = to_presentation(
            &model,
            &document_path,
            PresentationMetrics::default(),
            &Settings::default(),
        );
        let image = presentation
            .commands
            .iter()
            .find(|command| command.kind == kind::IMAGE)
            .expect("image command");

        assert_eq!((image.image_width, image.image_height), (1280, 720));
        assert_eq!(requests.len(), 1);
        assert_eq!(
            (requests[0].intrinsic_width, requests[0].intrinsic_height),
            (1280, 720)
        );
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn literal_encoded_and_bracketed_space_paths_share_the_local_asset() {
        let root = std::env::temp_dir().join(format!(
            "wolfmark-literal-space-image-{}",
            std::process::id()
        ));
        let images = root.join("images");
        std::fs::create_dir_all(&images).expect("create image directory");
        let document_path = root.join("document.md");
        let image_path = images.join("image with spaces.png");
        std::fs::write(&document_path, b"").expect("write document");
        std::fs::write(&image_path, b"fixture").expect("write image fixture");

        let model = parse(
            "![literal](images/image with spaces.png)\n\n\
             ![encoded](images/image%20with%20spaces.png)\n\n\
             ![bracketed](<images/image with spaces.png>)\n\n\
             ![titled](images/image with spaces.png \"Example\")\n\n\
             [Document](Some Folder/Document Name.md)",
        );
        let (presentation, requests) = to_presentation(
            &model,
            &document_path,
            PresentationMetrics::default(),
            &Settings::default(),
        );
        let image_commands = presentation
            .commands
            .iter()
            .filter(|command| command.kind == kind::IMAGE)
            .collect::<Vec<_>>();
        assert_eq!(image_commands.len(), 4);
        assert!(image_commands.iter().all(|command| command.flags == 0));
        assert!(
            image_commands
                .iter()
                .all(|command| command.number == image_commands[0].number)
        );
        assert_eq!(requests.len(), 1);
        assert_eq!(image_commands[3].extra, "Example");
        assert_eq!(
            std::path::Path::new(&requests[0].path),
            std::fs::canonicalize(&image_path).unwrap()
        );
        assert!(presentation.commands.iter().any(|command| {
            command.flags & style::LINK != 0 && command.target == "Some Folder/Document Name.md"
        }));

        let _ = std::fs::remove_dir_all(root);
    }
}
