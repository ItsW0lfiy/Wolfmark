// SPDX-License-Identifier: GPL-3.0-only
//! First Wolfmark/Servo UI parity slice. Document semantics and syntax
//! highlighting are shared with Wolfmark's existing Rust modules.
//! This is a read-only prototype, not the production editor.

use std::fmt::Write;
use std::path::{Path, PathBuf};

use base64::Engine;
use url::Url;

use crate::{highlight, model, parser};

const CSS: &str = include_str!("ui.css");
const LOGO: &[u8] = include_bytes!("../../../assets/icons/wolfmark-128x128.png");
const FOOTER: &[u8] = include_bytes!("../../../assets/branding/wolfmark-banner.png");
const BUNDLED_SOURCE: &str = include_str!("../../../docs/architecture/overview.md");
const MAX_DOCUMENT_BYTES: u64 = 2 * 1024 * 1024;
const MAX_IMAGE_BYTES: u64 = 2 * 1024 * 1024;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    Reader,
    Source,
}

pub struct Session {
    pub path: Option<PathBuf>,
    pub source: String,
    pub mode: Mode,
    pub zoom: u16,
    pub notice: Option<String>,
}

impl Default for Session {
    fn default() -> Self {
        Self {
            path: None,
            source: BUNDLED_SOURCE.to_owned(),
            mode: Mode::Reader,
            zoom: 100,
            notice: Some("Bundled architecture document — drag and drop a .md/.txt file or use Open document to test your own files.".into()),
        }
    }
}

impl Session {
    pub fn open(&mut self, path: &Path) {
        if !supported_document(path) {
            self.notice = Some("Only .md, .markdown and .txt documents are supported in this prototype.".into());
            return;
        }
        match std::fs::metadata(path) {
            Ok(metadata) if metadata.len() > MAX_DOCUMENT_BYTES => {
                self.notice = Some("This initial UI prototype limits documents to 2 MiB; the production reader must remove this limitation without freezing.".into());
                return;
            }
            Err(error) => {
                self.notice = Some(format!("Unable to inspect document: {error}"));
                return;
            }
            _ => {}
        }
        match std::fs::read_to_string(path) {
            Ok(source) => {
                self.path = Some(std::fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf()));
                self.source = source;
                self.mode = Mode::Reader;
                self.notice = None;
            }
            Err(error) => self.notice = Some(format!("Unable to open document: {error}")),
        }
    }

    pub fn reload(&mut self) {
        if let Some(path) = self.path.clone() {
            self.open(&path);
        } else {
            self.notice = Some("The bundled demonstration is already current.".into());
        }
    }

    pub fn title(&self) -> String {
        self.path.as_ref()
            .and_then(|path| path.file_name())
            .map(|name| name.to_string_lossy().into_owned())
            .unwrap_or_else(|| "Architecture.md".to_owned())
    }

    pub fn change_zoom(&mut self, delta: i16) {
        self.zoom = ((self.zoom as i16 + delta).clamp(65, 170)) as u16;
    }

    pub fn page_url(&self) -> Url {
        // No localhost, no external CDN, no WebView2, no loose HTML resources.
        let html = self.render_html();
        let encoded = percent_encoding::utf8_percent_encode(&html, percent_encoding::NON_ALPHANUMERIC);
        Url::parse(&format!("data:text/html;charset=utf-8,{encoded}")).expect("valid bundled page")
    }

    pub fn render_html(&self) -> String {
        let title = self.title();
        let mut toc = Vec::new();
        let mut body = String::new();
        let plain = self.path.as_ref()
            .is_some_and(|path| path.extension().is_some_and(|ext| ext.eq_ignore_ascii_case("txt")));
        if self.mode == Mode::Source {
            body.push_str("<pre class=\"source-text\">");
            body.push_str(&escape(&self.source));
            body.push_str("</pre>");
        } else if plain {
            body.push_str("<pre class=\"plain-text\">");
            body.push_str(&escape(&self.source));
            body.push_str("</pre>");
        } else {
            let parsed = parser::parse(&self.source);
            render_blocks(&mut body, &parsed.blocks, self.path.as_deref(), &mut toc);
        }

        let logo = data_png(LOGO);
        let footer = data_png(FOOTER);
        let mut html = String::with_capacity(CSS.len() + body.len() + 4000);
        html.push_str("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">");
        html.push_str("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
        html.push_str("<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; img-src data:; style-src 'unsafe-inline'; script-src 'none'; connect-src 'none'; form-action 'none'; base-uri 'none'\">");
        html.push_str("<title>Wolfmark — Servo prototype</title><style>");
        html.push_str(CSS);
        html.push_str("</style></head><body><div class=\"app\"><header class=\"header\">");
        let _ = write!(html, "<span class=\"brand\"><img alt=\"\" src=\"{logo}\"><span>Wolfmark</span></span>");
        html.push_str("<span class=\"separator\"></span><span class=\"crumb\"><span>Documents</span><span class=\"chevron\">›</span>");
        let _ = write!(html, "<span class=\"filename\" title=\"{}\">{}</span></span>", escape_attr(&title), escape(&title));
        html.push_str("<span class=\"header-spacer\"></span>");
        html.push_str("<a class=\"toolbar-link\" href=\"wolfmark://refresh/\" title=\"Reload document from disk\"><span class=\"glyph\">↻</span></a>");
        html.push_str("<span class=\"top-search\" title=\"Search is not wired in this first prototype\">⌕ &nbsp; Search in document… <span class=\"shortcut\">Ctrl+F</span></span>");
        html.push_str("<span class=\"separator\"></span><span class=\"toolbar-link top-settings disabled\" title=\"Settings integration pending\"><span class=\"glyph\">⚙</span><span class=\"label\">Settings</span></span>");
        html.push_str("</header><aside class=\"sidebar\"><div class=\"sidebar-inner\">");
        html.push_str("<a class=\"open-doc\" href=\"wolfmark://open/\" title=\"Select a local Markdown or text document\"><span class=\"glyph\">▱</span>Open document</a>");
        html.push_str("<p class=\"section-title\">Open Documents</p>");
        let _ = write!(html, "<span class=\"document-link selected\" title=\"{}\"><span class=\"doc-file\">▤</span>{}</span>", escape_attr(&title), escape(&title));
        html.push_str("<p class=\"section-title\">Outline</p><nav class=\"outline-scroll\" aria-label=\"Document outline\">");
        if self.mode == Mode::Reader {
            if toc.is_empty() {
                html.push_str("<span class=\"outline-item\">No headings</span>");
            } else {
                for (level, heading, anchor) in &toc {
                    let _ = write!(html, "<a class=\"outline-item depth-{}\" href=\"#{}\" title=\"{}\">{}</a>", level, anchor, escape_attr(heading), escape(heading));
                }
            }
        } else {
            html.push_str("<span class=\"outline-item\">Switch to Reader for outline</span>");
        }
        html.push_str("</nav></div><footer class=\"sidebar-footer\">");
        let _ = write!(html, "<img class=\"footer-art\" alt=\"\" src=\"{footer}\"><div class=\"footer-brand\"><img src=\"{logo}\" alt=\"\"><div><strong>Wolfmark</strong><small>Servo UI experiment</small></div></div>");
        html.push_str("</footer></aside><main class=\"main\">");
        html.push_str("<div class=\"viewbar\"><span class=\"nav-pair\"><span class=\"icon-button\" title=\"History navigation pending\">←</span><span class=\"icon-button\" title=\"History navigation pending\">→</span></span>");
        html.push_str("<nav class=\"mode-picker\" aria-label=\"Document mode\">");
        let reader = if self.mode == Mode::Reader {" selected"} else {""};
        let source = if self.mode == Mode::Source {" selected"} else {""};
        let _ = write!(html, "<a class=\"mode{reader}\" href=\"wolfmark://mode/reader\">◫ &nbsp; Reader</a>");
        html.push_str("<span class=\"mode unavailable\" title=\"Live Preview editor integration pending\">▣ &nbsp; Live Preview</span>");
        let _ = write!(html, "<a class=\"mode{source}\" href=\"wolfmark://mode/source\">⌘ &nbsp; Source</a>");
        html.push_str("</nav><span class=\"viewbar-spacer\"></span><nav class=\"zoom-controls\" aria-label=\"Document zoom\">");
        let _ = write!(html, "<a href=\"wolfmark://zoom/out\" title=\"Zoom out\">−</a><span>{}%</span><a href=\"wolfmark://zoom/in\" title=\"Zoom in\">+</a></nav>", self.zoom);
        html.push_str("<span class=\"icon-button more\" title=\"Additional actions not yet implemented\">⋯</span></div>");
        html.push_str("<div class=\"document-viewport\"><div class=\"document-inner\">");
        if let Some(notice) = &self.notice {
            let _ = write!(html, "<div class=\"notice\" role=\"status\">{}</div>", escape(notice));
        }
        let size = self.zoom as f64 * 0.17;
        let _ = write!(html, "<article class=\"document-body\" style=\"--doc-size:{size:.1}px\">{body}</article>");
        html.push_str("<p class=\"status-line\">Servo UI parity prototype · Reader and Source are read-only · Powered by Wolfmark's Rust parser</p>");
        html.push_str("</div></div></main></div></body></html>");
        html
    }
}

pub fn supported_document(path: &Path) -> bool {
    path.extension().is_some_and(|ext| {
        ext.eq_ignore_ascii_case("md")
            || ext.eq_ignore_ascii_case("markdown")
            || ext.eq_ignore_ascii_case("txt")
    })
}

pub fn escape(value: &str) -> String {
    let mut s = String::with_capacity(value.len());
    for ch in value.chars() {
        match ch {
            '&' => s.push_str("&amp;"),
            '<' => s.push_str("&lt;"),
            '>' => s.push_str("&gt;"),
            '"' => s.push_str("&quot;"),
            '\'' => s.push_str("&#39;"),
            _ => s.push(ch),
        }
    }
    s
}

fn escape_attr(value: &str) -> String {
    escape(value)
}

fn data_png(bytes: &[u8]) -> String {
    let encoded = base64::engine::general_purpose::STANDARD.encode(bytes);
    format!("data:image/png;base64,{encoded}")
}

fn render_blocks(html: &mut String, blocks: &[model::Block], document: Option<&Path>, toc: &mut Vec<(u8, String, String)>) {
    for block in blocks {
        match block {
            model::Block::Paragraph(inlines) => {
                html.push_str("<p>");
                render_inlines(html, inlines, document);
                html.push_str("</p>");
            }
            model::Block::Heading { level, content } => {
                let level = (*level).clamp(1, 6);
                let heading = model::plain_text(content);
                let anchor = format!("wm-heading-{}", toc.len() + 1);
                toc.push((level, heading, anchor.clone()));
                let _ = write!(html, "<h{level} id=\"{anchor}\">");
                render_inlines(html, content, document);
                let _ = write!(html, "</h{level}>");
            }
            model::Block::Quote(inner) => {
                html.push_str("<blockquote>");
                render_blocks(html, inner, document, toc);
                html.push_str("</blockquote>");
            }
            model::Block::List { ordered, start, items } => {
                if *ordered {
                    let _ = write!(html, "<ol start=\"{}\">", start);
                } else {
                    html.push_str("<ul>");
                }
                for item in items {
                    html.push_str("<li>");
                    if let Some(checked) = item.checked {
                        html.push_str(if checked { "☑ " } else { "☐ " });
                    }
                    render_blocks(html, &item.blocks, document, toc);
                    html.push_str("</li>");
                }
                html.push_str(if *ordered { "</ol>" } else { "</ul>" });
            }
            model::Block::CodeBlock { language, source } => {
                let safe_language = escape(language);
                let _ = write!(html, "<div class=\"codebox\"><div class=\"codehead\"><span>{safe_language}</span><span aria-label=\"Code block\">CODE</span></div><pre><code>");
                for span in highlight::highlight_code(language, source) {
                    let decoration = match span.flags & 3 {
                        1 => "font-weight:bold;",
                        2 => "font-style:italic;",
                        3 => "font-weight:bold;font-style:italic;",
                        _ => "",
                    };
                    let _ = write!(html, "<span style=\"color:rgb({},{},{});{}\">{}</span>", span.red, span.green, span.blue, decoration, escape(&span.text));
                }
                html.push_str("</code></pre></div>");
            }
            model::Block::Table { header, rows, alignments } => {
                html.push_str("<table><thead>");
                render_table_row(html, header, true, alignments, document);
                html.push_str("</thead><tbody>");
                for row in rows {
                    render_table_row(html, row, false, alignments, document);
                }
                html.push_str("</tbody></table>");
            }
            model::Block::HorizontalRule => html.push_str("<hr>"),
            model::Block::RawHtml(raw) => {
                // Raw HTML in Markdown is never executed. Show it as text.
                let _ = write!(html, "<pre class=\"source-text\">{}</pre>", escape(raw));
            }
        }
    }
}

fn render_table_row(html: &mut String, row: &model::TableRow, head: bool, alignments: &[model::TableAlignment], document: Option<&Path>) {
    html.push_str("<tr>");
    for (index, cell) in row.cells.iter().enumerate() {
        let tag = if head { "th" } else { "td" };
        let align = match alignments.get(index).copied().unwrap_or_default() {
            model::TableAlignment::Left => "left",
            model::TableAlignment::Center => "center",
            model::TableAlignment::Right => "right",
        };
        let _ = write!(html, "<{tag} style=\"text-align:{align}\">");
        render_inlines(html, cell, document);
        let _ = write!(html, "</{tag}>");
    }
    html.push_str("</tr>");
}

fn render_inlines(html: &mut String, inlines: &[model::Inline], document: Option<&Path>) {
    for inline in inlines {
        match inline {
            model::Inline::Text(value) => html.push_str(&escape(value)),
            model::Inline::Code(value) => {
                let _ = write!(html, "<code>{}</code>", escape(value));
            }
            model::Inline::Strong(children) => wrapped_inlines(html, "strong", children, document),
            model::Inline::Emphasis(children) => wrapped_inlines(html, "em", children, document),
            model::Inline::Strikethrough(children) => wrapped_inlines(html, "del", children, document),
            model::Inline::Link { destination, content } => {
                // Arbitrary external links are deliberately inert in this offline shell.
                if destination.starts_with('#') && destination.len() < 200 {
                    let _ = write!(html, "<a href=\"{}\">", escape_attr(destination));
                    render_inlines(html, content, document);
                    html.push_str("</a>");
                } else {
                    html.push_str("<span title=\"External navigation disabled in prototype\">");
                    render_inlines(html, content, document);
                    html.push_str("</span>");
                }
            }
            model::Inline::Image { source, alt, .. } => {
                if let Some(src) = local_image_data(source, document) {
                    let _ = write!(html, "<img src=\"{src}\" alt=\"{}\">", escape_attr(alt));
                } else {
                    let _ = write!(html, "<span class=\"image-error\" role=\"img\" aria-label=\"{}\">Image unavailable in prototype: {}</span>", escape_attr(alt), escape(source));
                }
            }
            model::Inline::SoftBreak => html.push('\n'),
            model::Inline::HardBreak => html.push_str("<br>"),
            model::Inline::RawHtml(value) => html.push_str(&escape(value)),
            model::Inline::TaskMark(checked) => html.push_str(if *checked { "☑ " } else { "☐ " }),
        }
    }
}

fn wrapped_inlines(html: &mut String, tag: &str, contents: &[model::Inline], document: Option<&Path>) {
    let _ = write!(html, "<{tag}>");
    render_inlines(html, contents, document);
    let _ = write!(html, "</{tag}>");
}

fn local_image_data(source: &str, document: Option<&Path>) -> Option<String> {
    let base = document?.parent()?;
    let decoded = percent_encoding::percent_decode_str(source).decode_utf8().ok()?;
    let relative = Path::new(decoded.as_ref());
    if relative.is_absolute() || decoded.contains('\0') || decoded.contains("://") {
        return None;
    }
    let file = std::fs::canonicalize(base.join(relative)).ok()?;
    let base = std::fs::canonicalize(base).ok()?;
    if !file.starts_with(&base) { return None; }
    let mime = match file.extension()?.to_str()?.to_ascii_lowercase().as_str() {
        "png" => "image/png",
        "jpg" | "jpeg" => "image/jpeg",
        "gif" => "image/gif",
        "webp" => "image/webp",
        _ => return None,
    };
    if std::fs::metadata(&file).ok()?.len() > MAX_IMAGE_BYTES { return None; }
    let bytes = std::fs::read(file).ok()?;
    let data = base64::engine::general_purpose::STANDARD.encode(bytes);
    Some(format!("data:{mime};base64,{data}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ui_has_single_document_list_and_no_open_dropdown() {
        let html = Session::default().render_html();
        assert!(html.contains("class=\"open-doc\""));
        assert!(!html.contains("dropdown"));
        assert!(!html.contains("document-tabs"));
        assert!(html.contains("wolfmark://open/"));
        assert!(html.contains("wolfmark://mode/source"));
        assert!(html.contains("wolfmark://refresh/"));
        assert!(html.contains("footer-art"));
    }

    #[test]
    fn parser_and_highlighter_render_real_markdown() {
        let mut session = Session::default();
        session.source = "# Title\n\nA **wolf**.\n\n\`\`\`rust\nfn main() {}\n\`\`\`\n".into();
        let html = session.render_html();
        assert!(html.contains("<h1 id=\"wm-heading-1\">Title</h1>"));
        assert!(html.contains("<strong>wolf</strong>"));
        assert!(html.contains("class=\"codebox\""));
        assert!(html.contains("color:rgb("));
    }

    #[test]
    fn html_and_external_links_remain_inert() {
        let mut session = Session::default();
        session.source = "# A <script>alert('x')</script>\n\n[bad](https://example.com)".into();
        let html = session.render_html();
        assert!(!html.contains("<script>alert"));
        assert!(!html.contains("href=\"https://example.com\""));
    }
}
