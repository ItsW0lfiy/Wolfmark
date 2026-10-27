// SPDX-License-Identifier: GPL-3.0-only
//! Experimental Servo presentation of the real Wolfmark Rust document model.
//! No secondary Markdown parser, web server, WebView2 or application JavaScript.
use std::collections::HashMap;
use std::fmt::Write as _;
use std::io::Cursor;
use std::path::{Path, PathBuf};
use std::sync::OnceLock;

use base64::{Engine as _, engine::general_purpose::STANDARD};
use url::Url;

use crate::backend::Backend;
use crate::markdown::convert::{kind, style};
use crate::presentation::{PresentationCommand, PresentationDocument};

const MAX_DOCUMENT_BYTES: u64 = 12 * 1024 * 1024;
const MAX_IMAGE_BYTES: u64 = 24 * 1024 * 1024;
const WELCOME: &str = "# Welcome to Wolfmark\n\nThis **Servo UI prototype** renders through the real Wolfmark Rust parser and presentation pipeline.\n\n- Open a local Markdown file with the left button.\n- Try the document outline, Reader and Source, or zoom controls.\n- Qt remains the normal Wolfmark application.\n\n\`\`\`rust\nfn main() {\n    println!(\"Wolfmark / Servo\");\n}\n\`\`\`\n";

#[derive(Clone, Copy, PartialEq, Eq)]
enum ViewMode { Reader, Source }

struct OpenDoc {
    path: PathBuf,
    source: String,
    presentation: PresentationDocument,
    image_data: HashMap<u32, String>,
}

pub struct Prototype {
    backend: Backend,
    documents: Vec<OpenDoc>,
    current: usize,
    mode: ViewMode,
    zoom: u16,
    message: String,
}

impl Prototype {
    pub fn new(first_path: Option<PathBuf>) -> Self {
        let mut ui = Self {
            backend: Backend::default(),
            documents: Vec::new(),
            current: 0,
            mode: ViewMode::Reader,
            zoom: 100,
            message: String::new(),
        };
        let initial = first_path.or_else(|| {
            let readme = PathBuf::from("README.md");
            readme.exists().then_some(readme)
        });
        if let Some(path) = initial {
            if let Err(error) = ui.open_document(path) {
                ui.message = error;
            }
        }
        if ui.documents.is_empty() {
            let path = PathBuf::from("Welcome.md");
            let source = WELCOME.to_string();
            let presentation = ui.backend.render_source(&path, &source);
            ui.documents.push(OpenDoc {
                path, source, presentation, image_data: HashMap::new()
            });
        }
        ui
    }

    pub fn command(&mut self, url: &Url) {
        if url.scheme() != "wolfmark" || url.host_str() != Some("action") {
            return;
        }
        let components: Vec<_> = url.path_segments().map(|s| s.collect()).unwrap_or_default();
        match components.first().copied().unwrap_or("") {
            "open" => {
                // Cross-platform native desktop file picker. No network, no shell command.
                if let Some(path) = rfd::FileDialog::new()
                    .add_filter("Markdown and text", &["md", "markdown", "txt"])
                    .pick_file()
                {
                    if let Err(error) = self.open_document(path) {
                        self.message = error;
                    } else {
                        self.message.clear();
                        self.mode = ViewMode::Reader;
                    }
                }
            }
            "switch" => {
                if let Some(Ok(index)) = components.get(1).map(|part| part.parse::<usize>()) {
                    if index < self.documents.len() { self.current = index; }
                }
            }
            "mode" => match components.get(1).copied().unwrap_or("") {
                "reader" => self.mode = ViewMode::Reader,
                "source" => self.mode = ViewMode::Source,
                _ => {},
            },
            "zoom" => match components.get(1).copied().unwrap_or("") {
                "in" => self.zoom = self.zoom.saturating_add(10).min(200),
                "out" => self.zoom = self.zoom.saturating_sub(10).max(60),
                "reset" => self.zoom = 100,
                _ => {},
            },
            "refresh" => {
                if let Some(path) = self.documents.get(self.current).map(|doc| doc.path.clone()) {
                    if path.is_absolute() {
                        if let Err(error) = self.refresh_document(&path) {
                            self.message = error;
                        }
                    }
                }
            }
            _ => {},
        }
    }

    fn open_document(&mut self, path: PathBuf) -> Result<(), String> {
        let canonical = std::fs::canonicalize(&path).map_err(|error| format!("Could not open file: {error}"))?;
        let valid = canonical.extension().and_then(|ext| ext.to_str())
            .is_some_and(|ext| matches!(ext.to_ascii_lowercase().as_str(), "md" | "markdown" | "txt"));
        if !valid { return Err("Only .md, .markdown, and .txt files are supported".into()); }
        if let Some(index) = self.documents.iter().position(|doc| doc.path == canonical) {
            self.current = index;
            return Ok(());
        }
        let metadata = std::fs::metadata(&canonical).map_err(|error| error.to_string())?;
        if metadata.len() > MAX_DOCUMENT_BYTES { return Err("Document exceeds the prototype's 12 MiB safety limit".into()); }
        let source = std::fs::read_to_string(&canonical).map_err(|error| format!("Could not read UTF-8 document: {error}"))?;
        let presentation = self.backend.render_loaded_source(&canonical, &source);
        let image_data = images_for(&self.backend);
        self.documents.push(OpenDoc { path: canonical, source, presentation, image_data });
        self.current = self.documents.len() - 1;
        Ok(())
    }

    fn refresh_document(&mut self, path: &Path) -> Result<(), String> {
        let metadata = std::fs::metadata(path).map_err(|error| error.to_string())?;
        if metadata.len() > MAX_DOCUMENT_BYTES { return Err("Document exceeds the prototype's 12 MiB safety limit".into()); }
        let source = std::fs::read_to_string(path).map_err(|error| error.to_string())?;
        let presentation = self.backend.render_loaded_source(path, &source);
        let image_data = images_for(&self.backend);
        if let Some(doc) = self.documents.get_mut(self.current) {
            doc.source = source;
            doc.presentation = presentation;
            doc.image_data = image_data;
        }
        self.message.clear();
        Ok(())
    }

    pub fn url(&self) -> Url {
        let html = self.html();
        let encoded = percent_encoding::utf8_percent_encode(&html, percent_encoding::NON_ALPHANUMERIC);
        Url::parse(&format!("data:text/html;charset=utf-8,{encoded}"))
            .expect("generated local prototype HTML is a valid data URL")
    }

    fn html(&self) -> String {
        let doc = &self.documents[self.current];
        let title = doc.presentation.title.as_str();
        let mut sidebar_docs = String::new();
        for (index, entry) in self.documents.iter().enumerate() {
            let active = if index == self.current { " active" } else { "" };
            let _ = write!(sidebar_docs,
                "<a class=\"document-link{active}\" href=\"wolfmark://action/switch/{index}\"><span class=\"doc-icon\">▤</span><span class=\"truncate\">{}</span></a>",
                escape(&entry.presentation.title)
            );
        }
        let mut outline = String::new();
        for heading in &doc.presentation.toc {
            let _ = write!(outline,
                "<a class=\"outline-link level-{}\" href=\"#{}\">{}</a>",
                heading.level, escape(&heading.anchor), escape(&heading.title)
            );
        }
        if outline.is_empty() { outline.push_str("<div class=\"muted hint\">No headings in this document</div>"); }

        let content = match self.mode {
            ViewMode::Reader => render_document(doc),
            ViewMode::Source => format!("<pre class=\"source-text\"><code>{}</code></pre>", escape(&doc.source)),
        };
        let reader_active = if self.mode == ViewMode::Reader { " active" } else { "" };
        let source_active = if self.mode == ViewMode::Source { " active" } else { "" };
        let message = if self.message.is_empty() { String::new() } else {
            format!("<div class=\"notice\" role=\"alert\">{}</div>", escape(&self.message))
        };
        let source_type = if doc.presentation.source_type == "plainText" { "Text" } else { "Markdown" };
        let mut output = include_str!("wolfmark_shell.html").to_string();
        let data = [
            ("{{CSS}}", include_str!("wolfmark.css").to_string()),
            ("{{TITLE}}", escape(title)),
            ("{{DOCS}}", sidebar_docs),
            ("{{OUTLINE}}", outline),
            ("{{CONTENT}}", content),
            ("{{MESSAGE}}", message),
            ("{{READER_ACTIVE}}", reader_active.into()),
            ("{{SOURCE_ACTIVE}}", source_active.into()),
            ("{{ZOOM}}", self.zoom.to_string()),
            ("{{SCALE}}", format!("{:.2}", f32::from(self.zoom) / 100.0)),
            ("{{SOURCE_TYPE}}", source_type.into()),
            ("{{LOGO}}", logo_asset().to_string()),
            ("{{BANNER}}", banner_asset().to_string()),
        ];
        for (name, value) in data { output = output.replace(name, &value); }
        output
    }
}

fn images_for(backend: &Backend) -> HashMap<u32, String> {
    let mut images = HashMap::new();
    for request in backend.image_requests() {
        let path = Path::new(&request.path);
        let safe = std::fs::metadata(path).is_ok_and(|metadata| metadata.len() <= MAX_IMAGE_BYTES);
        if !safe { continue; }
        let Ok(raw) = std::fs::read(path) else { continue };
        let Ok(image) = image::load_from_memory(&raw) else { continue };
        let scaled = image.thumbnail(1800, 1800);
        let mut png = Cursor::new(Vec::new());
        if scaled.write_to(&mut png, image::ImageFormat::Png).is_ok() {
            images.insert(request.id, format!("data:image/png;base64,{}", STANDARD.encode(png.into_inner())));
        }
    }
    images
}

fn local_asset(bytes: &[u8], width: u32, height: u32) -> String {
    // Built-in artwork only; scaled once for a compact local data: resource.
    let Ok(image) = image::load_from_memory(bytes) else { return String::new() };
    let image = image.thumbnail(width, height);
    let mut png = Cursor::new(Vec::new());
    if image.write_to(&mut png, image::ImageFormat::Png).is_err() { return String::new(); }
    format!("data:image/png;base64,{}", STANDARD.encode(png.into_inner()))
}

fn logo_asset() -> &'static str {
    static LOGO: OnceLock<String> = OnceLock::new();
    LOGO.get_or_init(|| local_asset(include_bytes!("../../../assets/branding/wolfmark-symbol.png"), 92, 92))
}

fn banner_asset() -> &'static str {
    static BANNER: OnceLock<String> = OnceLock::new();
    BANNER.get_or_init(|| local_asset(include_bytes!("../../../assets/branding/wolfmark-banner.png"), 520, 240))
}

fn render_document(doc: &OpenDoc) -> String {
    if doc.presentation.source_type == "plainText" {
        return format!("<pre class=\"literal-text\">{}</pre>", escape(&doc.presentation.literal_text));
    }
    if !doc.presentation.error.is_empty() {
        return format!("<div class=\"notice\">{}</div>", escape(&doc.presentation.error));
    }
    let mut html = String::new();
    let mut closing: Vec<&'static str> = Vec::new();
    let mut header = false;
    for command in &doc.presentation.commands {
        match command.kind {
            kind::BEGIN_PARAGRAPH => { html.push_str("<p>"); closing.push("</p>"); }
            kind::BEGIN_HEADING => {
                let level = command.level.clamp(1,6);
                let _ = write!(html, "<h{level} id=\"{}\">", escape(&command.target));
                closing.push(match level {1 => "</h1>",2 => "</h2>",3 => "</h3>",4 => "</h4>",5 => "</h5>",_ => "</h6>"});
            }
            kind::BEGIN_QUOTE => { html.push_str("<blockquote>"); closing.push("</blockquote>"); }
            kind::BEGIN_LIST => {
                if command.flags & style::ORDERED != 0 {
                    let _ = write!(html, "<ol start=\"{}\">", command.number.max(1));
                    closing.push("</ol>");
                } else { html.push_str("<ul>"); closing.push("</ul>"); }
            }
            kind::BEGIN_ITEM => {
                html.push_str("<li>");
                if command.flags & style::CHECKED != 0 { html.push_str("<span class=\"task-check\">☑ </span>"); }
                if command.flags & style::UNCHECKED != 0 { html.push_str("<span class=\"task-check\">☐ </span>"); }
                closing.push("</li>");
            }
            kind::BEGIN_TABLE => { html.push_str("<div class=\"table-scroll\"><table>"); closing.push("</table></div>"); }
            kind::BEGIN_ROW => {
                header = command.flags & style::HEADER != 0;
                html.push_str("<tr>"); closing.push("</tr>");
            }
            kind::BEGIN_CELL => {
                let align = match command.number {1=>"center",2=>"right",_=>"left"};
                if header {
                    let _ = write!(html, "<th class=\"align-{align}\">"); closing.push("</th>");
                } else {
                    let _ = write!(html, "<td class=\"align-{align}\">"); closing.push("</td>");
                }
            }
            kind::END_BLOCK | kind::END_LIST | kind::END_ITEM | kind::END_TABLE | kind::END_CELL | kind::END_ROW => {
                if let Some(tag) = closing.pop() { html.push_str(tag); }
            }
            kind::TEXT => {
                let text = escape(&command.text);
                let mut prefix = String::new();
                let mut suffix = String::new();
                if command.flags & style::STRONG != 0 { prefix.push_str("<strong>"); suffix.insert_str(0,"</strong>"); }
                if command.flags & style::EMPHASIS != 0 { prefix.push_str("<em>"); suffix.insert_str(0,"</em>"); }
                if command.flags & style::STRIKE != 0 { prefix.push_str("<s>"); suffix.insert_str(0,"</s>"); }
                if command.flags & style::CODE != 0 { prefix.push_str("<code>"); suffix.insert_str(0,"</code>"); }
                if command.flags & style::LINK != 0 {
                    if command.target.starts_with('#') {
                        let _ = write!(html, "<a href=\"{}\">{prefix}{text}{suffix}</a>", escape(&command.target));
                    } else { let _ = write!(html, "<span class=\"link-disabled\" title=\"External links disabled in this offline test\">{prefix}{text}{suffix}</span>"); }
                } else { html.push_str(&prefix); html.push_str(&text); html.push_str(&suffix); }
            }
            kind::SOFT_BREAK => html.push(' '),
            kind::HARD_BREAK => html.push_str("<br>"),
            kind::HORIZONTAL_RULE => html.push_str("<hr>"),
            kind::CODE_BLOCK => {
                html.push_str("<div class=\"code-panel\"><div class=\"code-header\">");
                html.push_str(&escape(if command.extra.is_empty() { "text" } else { &command.extra }));
                html.push_str("</div><pre><code>");
                for span in &command.spans {
                    let _ = write!(html, "<span style=\"color:rgb({},{},{}){}{}\">{}</span>",
                        span.red, span.green, span.blue,
                        if span.flags & 1 != 0 { ";font-weight:700" } else { "" },
                        if span.flags & 2 != 0 { ";font-style:italic" } else { "" },
                        escape(&span.text));
                }
                html.push_str("</code></pre></div>");
            }
            kind::IMAGE => {
                let alt = escape(&command.text);
                if let Ok(id) = u32::try_from(command.number) {
                    if let Some(url) = doc.image_data.get(&id) {
                        let _ = write!(html, "<figure><img src=\"{url}\" alt=\"{alt}\"><figcaption>{alt}</figcaption></figure>");
                        continue;
                    }
                }
                let _ = write!(html, "<div class=\"broken-image\">Image unavailable: {alt}</div>");
            }
            kind::RAW_HTML => {
                // Raw HTML from an untrusted Markdown file must stay inert.
                if !command.text.trim().is_empty() {
                    let _ = write!(html, "<pre class=\"raw-html\">{}</pre>", escape(&command.text));
                }
            }
            _ => {}
        }
    }
    while let Some(tag) = closing.pop() { html.push_str(tag); }
    html
}

fn escape(input: &str) -> String {
    let mut text = String::with_capacity(input.len());
    for c in input.chars() {
        match c {
            '&' => text.push_str("&amp;"), '<' => text.push_str("&lt;"),
            '>' => text.push_str("&gt;"), '"' => text.push_str("&quot;"),
            '\'' => text.push_str("&#39;"), _ => text.push(c),
        }
    }
    text
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn real_wolfmark_model_produces_highlighted_html() {
        let mut backend = Backend::default();
        let doc = backend.render_source(Path::new("test.md"), "# Hello\n\n\`\`\`rust\nfn main() {}\n\`\`\`");
        let opened = OpenDoc { path: PathBuf::from("test.md"), source: String::new(), presentation: doc, image_data: HashMap::new() };
        let html = render_document(&opened);
        assert!(html.contains("<h1 id=\"hello\">"));
        assert!(html.contains("code-panel"));
        assert!(html.contains("color:rgb("));
    }
    #[test]
    fn untrusted_raw_html_is_escaped() {
        let mut backend = Backend::default();
        let doc = backend.render_source(Path::new("test.md"), "<script>alert(1)</script>");
        let opened = OpenDoc { path: PathBuf::from("test.md"), source: String::new(), presentation: doc, image_data: HashMap::new() };
        let html = render_document(&opened);
        assert!(!html.contains("<script>"));
    }
}
