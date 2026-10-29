// SPDX-License-Identifier: GPL-3.0-only
//! First Wolfmark/Servo UI parity slice. Document semantics and syntax
//! highlighting are shared with Wolfmark's existing Rust modules.
//! This is a read-only prototype, not the production editor.

use std::cell::RefCell;
use std::fmt::Write;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::sync::OnceLock;

use base64::Engine;
use url::Url;

use crate::{highlight, model, parser};

const CSS: &str = include_str!("ui.css");
const LOGO: &[u8] = include_bytes!("../../../assets/icons/wolfmark-128x128.png");
const FOOTER: &str = include_str!("footer_mountains.svg");
const BUNDLED_SOURCE: &str = include_str!("../../../docs/architecture/overview.md");
const MAX_DOCUMENT_BYTES: u64 = 2 * 1024 * 1024;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    Reader,
    Live,
    Source,
}

pub struct Session {
    pub path: Option<PathBuf>,
    pub source: String,
    pub mode: Mode,
    pub zoom: u16,
    pub notice: Option<String>,
    pub frameless_test: bool,
    pub settings_open: bool,
    pub settings_page: &'static str,
    pub settings_draft: Option<crate::user_settings::Preferences>,
    pub preferences: crate::user_settings::Preferences,
    pub view_revision: std::cell::Cell<u64>,
    // Sidebar document history belongs to Rust, never to browser storage.
    open_documents: Vec<SavedDocument>,
    active_document: Option<usize>,
    history: Vec<Option<usize>>,
    history_position: usize,
    bundled_view: (Mode, u16),
    reader_cache: Rc<RefCell<Option<CachedReader>>>,
    bundled_cache: Rc<RefCell<Option<CachedReader>>>,
    pub resources: Rc<crate::image_resources::Resources>,
    bundled_resources: Rc<crate::image_resources::Resources>,
}

#[derive(Default)]
struct CachedReader {
    source: String,
    body: String,
    toc: Vec<(u8, String, String)>,
}

struct SavedDocument {
    resources: Rc<crate::image_resources::Resources>,
    editor: crate::editor::Buffer,
    editor_view: (usize, usize, f64),
    reader_cache: Rc<RefCell<Option<CachedReader>>>,
    path: PathBuf,
    source: String,
    mode: Mode,
    zoom: u16,
}

impl Default for Session {
    fn default() -> Self {
        let reader_cache = Rc::new(RefCell::new(None));
        let resources = Rc::new(crate::image_resources::Resources::default());
        let preferences = crate::user_settings::Preferences::load();
        let notice = preferences.recovery_warning.clone();
        Self {
            path: None,
            source: BUNDLED_SOURCE.to_owned(),
            mode: Mode::Reader,
            zoom: 100,
            frameless_test: false,
            settings_open: false,
            settings_page: "updates",
            settings_draft: None,
            preferences,
            view_revision: std::cell::Cell::new(0),
            notice,
            open_documents: Vec::new(),
            active_document: None,
            history: vec![None],
            history_position: 0,
            bundled_view: (Mode::Reader, 100),
            bundled_resources: resources.clone(),
            resources,
            bundled_cache: reader_cache.clone(),
            reader_cache,
        }
    }
}

impl Session {
    pub fn open(&mut self, path: &Path) {
        if !supported_document(path) {
            self.notice = Some(
                "Only .md, .markdown and .txt documents are supported in this prototype.".into(),
            );
            return;
        }
        let path = std::fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
        if let Some(index) = self.open_documents.iter().position(|doc| doc.path == path) {
            self.select(index);
            return;
        }
        match std::fs::metadata(&path) {
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
        match std::fs::read_to_string(&path) {
            Ok(source) => {
                let editor = match crate::editor::Buffer::new(source.clone()) {
                    Ok(editor) => editor,
                    Err(error) => {
                        self.notice = Some(error);
                        return;
                    }
                };
                self.open_documents.push(SavedDocument {
                    editor,
                    editor_view: (0, 0, 0.0),
                    resources: Rc::new(crate::image_resources::Resources::default()),
                    reader_cache: Rc::new(RefCell::new(None)),
                    path,
                    source,
                    mode: Mode::Reader,
                    zoom: 100,
                });
                self.select(self.open_documents.len() - 1);
                if let Some(parent) = self.path.as_deref().and_then(Path::parent) {
                    self.preferences.last_open_directory = parent.to_string_lossy().into_owned();
                    if let Err(error) = self.preferences.save() {
                        self.notice = Some(format!("Unable to remember the last directory: {error}"));
                    }
                }
            }
            Err(error) => self.notice = Some(format!("Unable to open document: {error}")),
        }
    }

    pub fn select(&mut self, index: usize) {
        self.activate(Some(index), true);
    }

    pub fn select_bundled(&mut self) {
        self.activate(None, true);
    }

    // Every document navigation is Rust-owned. The view's data: URL is only
    // a representation of this state, never the source of truth.
    fn activate(&mut self, target: Option<usize>, record: bool) -> bool {
        if target.is_some_and(|index| index >= self.open_documents.len()) {
            return false;
        }
        self.retain_view();
        if let Some(index) = target {
            let Some(document) = self.open_documents.get(index) else {
                return false;
            };
            self.path = Some(document.path.clone());
            self.source = document.editor.source.clone();
            self.resources = document.resources.clone();
            self.reader_cache = document.reader_cache.clone();
            self.mode = document.mode;
            self.zoom = document.zoom;
        } else {
            self.path = None;
            self.resources = self.bundled_resources.clone();
            self.reader_cache = self.bundled_cache.clone();
            self.source = BUNDLED_SOURCE.to_owned();
            (self.mode, self.zoom) = self.bundled_view;
        }
        self.active_document = target;

        self.notice = None;

        if record && self.history.get(self.history_position).copied() != Some(target) {
            self.history.truncate(self.history_position + 1);
            self.history.push(target);
            self.history_position = self.history.len() - 1;
        }
        true
    }

    fn retain_view(&mut self) {
        if let Some(index) = self.active_document {
            if let Some(document) = self.open_documents.get_mut(index) {
                document.mode = self.mode;
                document.zoom = self.zoom;
            }
        } else {
            self.bundled_view = (self.mode, self.zoom);
        }
    }

    pub fn can_go_back(&self) -> bool {
        self.history_position > 0
    }
    pub fn can_go_forward(&self) -> bool {
        self.history_position + 1 < self.history.len()
    }

    pub fn back(&mut self) {
        if self.can_go_back() {
            self.history_position -= 1;
            let target = self.history[self.history_position];
            self.activate(target, false);
        }
    }

    pub fn forward(&mut self) {
        if self.can_go_forward() {
            self.history_position += 1;
            let target = self.history[self.history_position];
            self.activate(target, false);
        }
    }

    pub fn close(&mut self, index: usize) {
        if index >= self.open_documents.len() || !self.confirm_discard(index) {
            return;
        }
        self.retain_view();
        let closing_active = self.active_document == Some(index);
        self.open_documents.remove(index);

        // Previously visited documents cannot point to removed/shifted slots.
        for entry in &mut self.history {
            match entry {
                Some(value) if *value == index => *entry = None,
                Some(value) if *value > index => *value -= 1,
                _ => {}
            }
        }
        if let Some(active) = self.active_document.as_mut() {
            if *active > index {
                *active -= 1;
            }
        }
        if closing_active {
            self.active_document = None;
            (self.mode, self.zoom) = self.bundled_view;
            let next = if self.open_documents.is_empty() {
                None
            } else {
                Some(index.min(self.open_documents.len() - 1))
            };
            self.activate(next, true);
        }
    }

    pub fn close_active(&mut self) {
        if let Some(index) = self.active_document {
            self.close(index);
        }
    }

    pub fn select_adjacent(&mut self, backward: bool) {
        // Sidebar order includes the built-in architecture document first.
        let count = self.open_documents.len() + 1;
        let current = self.active_document.map_or(0, |value| value + 1);
        let next = if backward {
            (current + count - 1) % count
        } else {
            (current + 1) % count
        };
        self.activate(next.checked_sub(1), true);
    }

    pub fn begin_settings(&mut self) {
        self.settings_open = true;
        self.settings_page = "updates";
        self.settings_draft = Some(self.preferences.clone());
    }

    pub fn cancel_settings(&mut self) {
        self.settings_open = false;
        self.settings_draft = None;
    }

    pub fn save_settings(&mut self) -> Result<(), String> {
        let draft = self.settings_draft.as_ref().ok_or("Settings dialog is not open")?;
        draft.save().map_err(|error| {
            self.notice = Some(format!("Could not save settings: {error}"));
            error
        })?;
        self.preferences = draft.clone();
        self.cancel_settings();
        self.notice = None;
        Ok(())
    }

    pub fn settings_page(&mut self, page: &'static str) {
        if matches!(page, "updates" | "editor" | "about") {
            self.settings_page = page;
        }
    }

    pub fn toggle_setting(&mut self, field: &str) -> Result<(), String> {
        let draft = self.settings_draft.as_mut().ok_or("Settings dialog is not open")?;
        match field {
            "word-wrap" => draft.editor_word_wrap = !draft.editor_word_wrap,
            "check-updates" => draft.check_on_startup = !draft.check_on_startup,
            "prereleases" => draft.include_prereleases = !draft.include_prereleases,
            _ => return Err("Unknown settings field".into()),
        }
        Ok(())
    }

    pub fn settings_html(&self) -> String {
        let draft = self.settings_draft.as_ref().unwrap_or(&self.preferences);
        let checked = |enabled: bool| if enabled { "checked" } else { "" };
        let mut html = String::new();
        html.push_str("<section class=\"settings-overlay\" role=\"dialog\" aria-modal=\"true\" aria-labelledby=\"settings-title\">");
        html.push_str("<div class=\"settings-window\"><header class=\"settings-header\">");
        html.push_str("<span class=\"settings-brand\">Wolfmark <span>/ Settings</span></span>");
        html.push_str("<a href=\"wolfmark://settings/close\" class=\"settings-close\" aria-label=\"Cancel and close Settings\" title=\"Cancel\">");
        html.push_str("<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.8\"><path d=\"m6 6 12 12 M18 6 6 18\"/></svg></a></header>");
        html.push_str("<div class=\"settings-columns\"><nav class=\"settings-navigation\" aria-label=\"Settings categories\">");
        html.push_str("<div class=\"settings-identity\">Wolfmark</div>");
        for (page, label) in [("updates", "Updates"), ("editor", "Reader &amp; Editor"), ("about", "About")] {
            let active = if self.settings_page == page { " settings-nav-current" } else { "" };
            let _ = write!(html, "<a class=\"settings-nav-item{active}\" href=\"wolfmark://settings/page-{page}\">{label}</a>");
        }
        html.push_str("<span class=\"settings-nav-version\">0.1.0-dev.7<br>Free and open source</span></nav>");
        html.push_str("<div class=\"settings-content\">");
        match self.settings_page {
            "editor" => {
                html.push_str("<span class=\"settings-eyebrow\">WOLFMARK / DOCUMENTS</span><h1 id=\"settings-title\">Reader &amp; Editor</h1>");
                html.push_str("<p class=\"settings-description\">Newly opened documents begin in Reader. Saving is explicit; autosave is disabled.</p>");
                html.push_str("<h2>Source editing</h2>");
                let _ = write!(html, "<a class=\"setting-row\" href=\"wolfmark://settings/word-wrap\" role=\"checkbox\" aria-checked=\"{}\"><span><strong>Wrap long source lines</strong><small>Wrap text to fit the editor window. The original source is preserved.</small></span><span class=\"setting-checkbox {}\"><svg viewBox=\"0 0 16 16\" aria-hidden=\"true\"><path d=\"m3 8 3 3 7-7\"/></svg></span></a>", draft.editor_word_wrap, checked(draft.editor_word_wrap));
            }
            "about" => {
                html.push_str("<span class=\"settings-eyebrow\">WOLFMARK / INFORMATION</span><h1 id=\"settings-title\">About Wolfmark</h1>");
                html.push_str("<p class=\"settings-description\">A viewer-first Markdown and text reader for ordinary local files.</p>");
                html.push_str("<h2>Wolfmark 0.1.0-dev.7</h2><div class=\"settings-about\"><span>Free and open-source software · GPL-3.0-only</span><span>Rust · Servo · Comrak</span>");
                let _ = write!(html, "<span>Settings location: {}</span><span>{}</span></div>", escape(&draft.path().to_string_lossy()), if draft.portable { "Portable mode" } else { "User configuration" });
            }
            _ => {
                html.push_str("<span class=\"settings-eyebrow\">WOLFMARK / PREFERENCES</span><h1 id=\"settings-title\">Updates</h1>");
                html.push_str("<p class=\"settings-description\">Wolfmark's update source is public GitHub Releases, without an account or telemetry.</p>");
                html.push_str("<h2>Update preferences</h2>");
                let _ = write!(html, "<a class=\"setting-row\" href=\"wolfmark://settings/check-updates\" role=\"checkbox\" aria-checked=\"{}\"><span><strong>Check for updates automatically</strong><small>Check quietly after Wolfmark opens when the GitHub updater is connected.</small></span><span class=\"setting-checkbox {}\"><svg viewBox=\"0 0 16 16\" aria-hidden=\"true\"><path d=\"m3 8 3 3 7-7\"/></svg></span></a>",draft.check_on_startup,checked(draft.check_on_startup));
                let _ = write!(html, "<a class=\"setting-row\" href=\"wolfmark://settings/prereleases\" role=\"checkbox\" aria-checked=\"{}\"><span><strong>Include pre-release versions</strong><small>Development builds may be less stable than regular releases.</small></span><span class=\"setting-checkbox {}\"><svg viewBox=\"0 0 16 16\" aria-hidden=\"true\"><path d=\"m3 8 3 3 7-7\"/></svg></span></a>",draft.include_prereleases,checked(draft.include_prereleases));
                html.push_str("<h2>Current version</h2><div class=\"settings-about\"><strong>Wolfmark 0.1.0-dev.7</strong><span>The GitHub update-check and download transport has not been connected to Servo yet.</span></div>");
            }
        }
        html.push_str("</div></div><footer class=\"settings-footer\"><span>Changes are saved only when you select Save</span><div class=\"settings-actions\"><a class=\"settings-cancel\" href=\"wolfmark://settings/close\">Cancel</a><a class=\"settings-save\" href=\"wolfmark://settings/save\">Save</a></div></footer></div></section>");
        html
    }

    pub fn editor_view(&self) -> (usize, usize, f64) {
        self.active_document
            .map(|index| self.open_documents[index].editor_view)
            .unwrap_or_default()
    }
    pub fn set_editor_view(&mut self, view: (usize, usize, f64)) {
        if let Some(index) = self.active_document {
            self.open_documents[index].editor_view = view;
        }
    }
    pub fn apply_editor_view(&mut self, value: &str) -> Result<(), String> {
        if let Some(index) = self.active_document {
            let document = &mut self.open_documents[index];
            document.editor.apply(value)?;
            document.source.clone_from(&document.editor.source);
            self.source.clone_from(&document.editor.source);
        }
        Ok(())
    }
    pub fn dirty(&self) -> bool {
        self.active_document
            .is_some_and(|index| self.open_documents[index].editor.dirty())
    }
    pub fn undo(&mut self, redo: bool) {
        if let Some(index) = self.active_document {
            let document = &mut self.open_documents[index];
            if redo {
                document.editor.redo();
            } else {
                document.editor.undo();
            }
            document.source.clone_from(&document.editor.source);
            self.source.clone_from(&document.editor.source);
        }
    }
    pub fn save(&mut self, save_as: bool) -> bool {
        let Some(index) = self.active_document else {
            return false;
        };
        self.save_index(index, save_as)
    }
    fn save_index(&mut self, index: usize, save_as: bool) -> bool {
        let result = if save_as {
            let Some(path) = rfd::FileDialog::new()
                .set_file_name(
                    self.open_documents[index]
                        .path
                        .file_name()
                        .unwrap_or_default()
                        .to_string_lossy(),
                )
                .save_file()
            else {
                return false;
            };
            let path = std::fs::canonicalize(&path).unwrap_or(path);
            if self
                .open_documents
                .iter()
                .enumerate()
                .any(|(i, doc)| i != index && doc.path == path)
            {
                self.notice = Some("That file is already open in another document".into());
                return false;
            }
            let expected = match std::fs::read(&path) {
                Ok(bytes) => Some(bytes),
                Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
                Err(error) => {
                    self.notice = Some(error.to_string());
                    return false;
                }
            };
            self.open_documents[index]
                .editor
                .save_as(&path, expected.as_deref())
                .map(|()| {
                    self.open_documents[index].path = path.clone();
                    if self.active_document == Some(index) {
                        self.path = Some(path);
                        self.reader_cache.borrow_mut().take();
                    }
                })
        } else {
            let document = &mut self.open_documents[index];
            document.editor.save(&document.path)
        };
        match result {
            Ok(()) => {
                self.notice = None;
                true
            }
            Err(error) => {
                rfd::MessageDialog::new()
                    .set_title("Save failed")
                    .set_description(&error)
                    .set_level(rfd::MessageLevel::Error)
                    .show();
                self.notice = Some(error);
                false
            }
        }
    }
    fn confirm_discard(&mut self, index: usize) -> bool {
        if !self.open_documents[index].editor.dirty() {
            return true;
        }
        let name = self.open_documents[index]
            .path
            .file_name()
            .unwrap_or_default()
            .to_string_lossy();
        match rfd::MessageDialog::new()
            .set_title("Unsaved changes")
            .set_description(format!(
                "Save changes to {name}? Yes saves; No discards; Cancel keeps the document open."
            ))
            .set_buttons(rfd::MessageButtons::YesNoCancel)
            .show()
        {
            rfd::MessageDialogResult::Yes => self.save_index(index, false),
            rfd::MessageDialogResult::No => true,
            _ => false,
        }
    }
    pub fn confirm_exit(&mut self) -> bool {
        (0..self.open_documents.len()).all(|index| self.confirm_discard(index))
    }
    pub fn reload(&mut self) {
        let Some(index) = self.active_document else {
            self.notice = Some("The bundled demonstration is already current.".into());
            return;
        };
        if !self.confirm_discard(index) {
            return;
        }
        let path = &self.open_documents[index].path;
        match std::fs::metadata(path) {
            Ok(metadata) if metadata.len() > MAX_DOCUMENT_BYTES => {
                self.notice = Some("Document exceeds the 2 MiB prototype limit.".into());
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
                let editor = match crate::editor::Buffer::new(source.clone()) {
                    Ok(editor) => editor,
                    Err(error) => {
                        self.notice = Some(error);
                        return;
                    }
                };
                self.open_documents[index].editor = editor;
                self.open_documents[index].source = source.clone();
                self.source = source;
                self.reader_cache.borrow_mut().take();
                self.resources = Rc::new(crate::image_resources::Resources::default());
                self.open_documents[index].resources = self.resources.clone();
                self.notice = None;
            }
            Err(error) => self.notice = Some(format!("Unable to reload document: {error}")),
        }
    }

    pub fn all_resources(&self) -> Vec<Rc<crate::image_resources::Resources>> {
        std::iter::once(self.bundled_resources.clone())
            .chain(self.open_documents.iter().map(|doc| doc.resources.clone()))
            .collect()
    }

    pub fn title(&self) -> String {
        self.path
            .as_ref()
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
        let encoded =
            percent_encoding::utf8_percent_encode(&html, percent_encoding::NON_ALPHANUMERIC);
        Url::parse(&format!("data:text/html;charset=utf-8,{encoded}")).expect("valid bundled page")
    }

    pub fn render_html(&self) -> String {
        self.view_revision
            .set(self.view_revision.get().wrapping_add(1));
        let title = self.title();
        let mut toc = Vec::new();
        let mut body = String::new();
        let plain = self.path.as_ref().is_some_and(|path| {
            path.extension()
                .is_some_and(|ext| ext.eq_ignore_ascii_case("txt"))
        });
        if matches!(self.mode, Mode::Source | Mode::Live) {
            let read_only = if self.path.is_none() { " readonly" } else { "" };
            let wrap = if self.preferences.editor_word_wrap { "soft" } else { "off" };
            let wrapping_style = if self.preferences.editor_word_wrap {
                "white-space:pre-wrap;overflow-wrap:anywhere"
            } else {
                "white-space:pre;overflow-wrap:normal"
            };
            let _ = write!(
                body,
                "<textarea id=\"source-editor\" class=\"source-editor\" aria-label=\"Markdown source\" spellcheck=\"false\" wrap=\"{wrap}\" style=\"{wrapping_style}\"{read_only}>\n"
            );
            let view = self
                .active_document
                .map(|index| self.open_documents[index].editor.view())
                .unwrap_or_else(|| self.source.clone());
            body.push_str(&escape(&view));
            body.push_str("</textarea>");
            if self.mode == Mode::Live {
                let preview = decorated_source(&view);
                let lines = view.lines().count().max(1);
                let line_numbers = (1..=lines).map(|n| n.to_string()).collect::<Vec<_>>().join("\n");
                let number_html = escape(&line_numbers);
                // The editable textarea is the real input; the preview layer
                // uses the same raw text and only changes visual token colors.
                // Markdown source is NEVER rewritten by the decoration layer.
                body = format!("<div class=\"live-stage\"><pre class=\"live-line-numbers\" aria-hidden=\"true\" id=\"live-line-numbers\">{number_html}</pre><div class=\"live-edit-surface\"><pre class=\"live-highlights\" aria-hidden=\"true\" id=\"live-highlights\">{preview}</pre>{body}</div></div>");
            }
        } else if plain {
            body.push_str("<pre class=\"plain-text\">");
            body.push_str(&escape(&self.source));
            body.push_str("</pre>");
        } else {
            let mut cached = self.reader_cache.borrow_mut();
            if cached
                .as_ref()
                .is_none_or(|entry| entry.source != self.source)
            {
                let parsed = parser::parse(&self.source);
                let mut entry = CachedReader {
                    source: self.source.clone(),
                    ..Default::default()
                };
                render_blocks(
                    &mut entry.body,
                    &parsed.blocks,
                    self.path.as_deref(),
                    &self.resources,
                    &mut entry.toc,
                );
                *cached = Some(entry);
            }
            let entry = cached.as_ref().expect("reader cache initialized");
            body.clone_from(&entry.body);
            toc.clone_from(&entry.toc);
        }

        // Brand assets do not change between document switches. Avoid encoding
        // hundreds of KiB to base64 again every time a mode/zoom button is used.
        static LOGO_URI: OnceLock<String> = OnceLock::new();
        static FOOTER_URI: OnceLock<String> = OnceLock::new();
        let logo = LOGO_URI.get_or_init(|| data_png(LOGO));
        let footer = FOOTER_URI.get_or_init(|| data_svg(FOOTER));
        let mut html = String::with_capacity(CSS.len() + body.len() + 4000);
        html.push_str("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">");
        html.push_str("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
        html.push_str("<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; img-src data: https://wolfmark.invalid; style-src 'unsafe-inline'; script-src 'none'; connect-src 'none'; form-action 'none'; base-uri 'none'\">");
        html.push_str("<title>Wolfmark — Servo prototype</title><style>");
        html.push_str(CSS);
        html.push_str("</style></head><body><div class=\"app\"><header class=\"header\">");
        let _ = write!(
            html,
            "<span class=\"brand\"><img alt=\"\" src=\"{logo}\"><span>Wolfmark</span></span>"
        );
        html.push_str("<span class=\"separator\"></span><span class=\"crumb\"><span>Documents</span><span class=\"chevron\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"m9 5 7 7-7 7\"/></svg></span>");
        let _ = write!(
            html,
            "<span class=\"filename\" title=\"{}\">{}</span></span>",
            escape_attr(&title),
            escape(&title)
        );
        html.push_str("<span class=\"header-spacer\"></span>");
        html.push_str("<a class=\"toolbar-link\" href=\"wolfmark://refresh/\" title=\"Reload document from disk\"><span class=\"glyph\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M20 7v5h-5M20 12a8 8 0 1 0-2 6\"/></svg></span></a>");
        html.push_str("<a class=\"top-search\" href=\"wolfmark://find/open\" title=\"Search in document\">Search in document… <span class=\"shortcut\">Ctrl+F</span></a>");
        html.push_str("<span class=\"separator\"></span><a class=\"toolbar-link top-settings\" href=\"wolfmark://settings/open\" title=\"Open Wolfmark Settings\"><span class=\"glyph\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M9 3h6l1 4 4 1v8l-4 1-1 4H9l-1-4-4-1V8l4-1ZM15 12a3 3 0 1 1-6 0 3 3 0 0 1 6 0\"/></svg></span><span class=\"label\">Settings</span></a>");
        if self.frameless_test {
            html.push_str("<nav class=\"caption-controls\" aria-label=\"Window controls\">");
            html.push_str("<a class=\"caption-button\" href=\"wolfmark://window/minimize\" title=\"Minimize\" aria-label=\"Minimize\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M5 12h14\"/></svg></a>");
            html.push_str("<a class=\"caption-button\" href=\"wolfmark://window/maximize\" title=\"Maximize or restore\" aria-label=\"Maximize or restore\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M5 5h14v14H5Z\"/></svg></a>");
            html.push_str("<a class=\"caption-button caption-close\" href=\"wolfmark://window/close\" title=\"Close\" aria-label=\"Close\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"m6 6 12 12M18 6 6 18\"/></svg></a>");
            html.push_str("</nav>");
        }
        html.push_str("</header><aside class=\"sidebar\"><div class=\"sidebar-inner\">");
        html.push_str("<a class=\"open-doc\" href=\"wolfmark://open/\" title=\"Select a local Markdown or text document\"><span class=\"glyph\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M3 7V5h6l2 2h10v3M3 7h7l2 3h9l-3 10H3Z\"/></svg></span>Open document</a>");
        html.push_str("<p class=\"section-title\">Open Documents</p>");
        html.push_str("<nav class=\"document-list\" aria-label=\"Open documents\">");
        let bundled_class = if self.active_document.is_none() {
            " selected"
        } else {
            ""
        };
        let _ = write!(
            html,
            "<div class=\"document-row{bundled_class}\"><a class=\"document-link\" href=\"wolfmark://document/bundled\" title=\"Bundled Wolfmark architecture\"><span class=\"doc-file\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M6 3h8l4 4v14H6ZM14 3v5h4M9 12h6M9 16h6\"/></svg></span>Architecture.md</a></div>"
        );
        for (index, document) in self.open_documents.iter().enumerate() {
            let selected = if self.active_document == Some(index) {
                " selected"
            } else {
                ""
            };
            let mut label = document
                .path
                .file_name()
                .unwrap_or_default()
                .to_string_lossy()
                .into_owned();
            if document.editor.dirty() {
                label.push_str(" *");
            }
            let _ = write!(
                html,
                "<div class=\"document-row{selected}\"><a class=\"document-link\" href=\"wolfmark://document/{index}\" title=\"{}\"><span class=\"doc-file\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M6 3h8l4 4v14H6ZM14 3v5h4M9 12h6M9 16h6\"/></svg></span>{}</a><a class=\"document-close\" href=\"wolfmark://close/{index}\" title=\"Close document\" aria-label=\"Close {}\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"m6 6 12 12M18 6 6 18\"/></svg></a></div>",
                escape_attr(&document.path.to_string_lossy()),
                escape(&label),
                escape_attr(&label)
            );
        }
        html.push_str("</nav>");
        html.push_str("<p class=\"section-title\">Outline</p><nav class=\"outline-scroll\" aria-label=\"Document outline\">");
        if self.mode == Mode::Reader {
            if toc.is_empty() {
                html.push_str("<span class=\"outline-item\">No headings</span>");
            } else {
                for (level, heading, anchor) in &toc {
                    let _ = write!(
                        html,
                        "<a class=\"outline-item depth-{}\" href=\"#{}\" title=\"{}\">{}</a>",
                        level,
                        anchor,
                        escape_attr(heading),
                        escape(heading)
                    );
                }
            }
        } else {
            html.push_str("<span class=\"outline-item\">Switch to Reader for outline</span>");
        }
        html.push_str("</nav></div><footer class=\"sidebar-footer\">");
        let _ = write!(
            html,
            "<img class=\"footer-art\" alt=\"\" src=\"{footer}\"><div class=\"footer-brand\"><img src=\"{logo}\" alt=\"\"><div><strong>Wolfmark</strong><small>v0.1.0-dev.7 · Servo</small></div></div>"
        );
        html.push_str("</footer></aside><main class=\"main\">");
        html.push_str(
            "<div class=\"viewbar\"><nav class=\"nav-pair\" aria-label=\"Document history\">",
        );
        if self.can_go_back() {
            html.push_str("<a class=\"icon-button\" href=\"wolfmark://history/back\" title=\"Back to previous document\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M19 12H5m6-6-6 6 6 6\"/></svg></a>");
        } else {
            html.push_str("<span class=\"icon-button nav-disabled\" aria-disabled=\"true\" title=\"No previous document\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M19 12H5m6-6-6 6 6 6\"/></svg></span>");
        }
        if self.can_go_forward() {
            html.push_str("<a class=\"icon-button\" href=\"wolfmark://history/forward\" title=\"Forward to next document\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M5 12h14m-6-6 6 6-6 6\"/></svg></a>");
        } else {
            html.push_str("<span class=\"icon-button nav-disabled\" aria-disabled=\"true\" title=\"No next document\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M5 12h14m-6-6 6 6-6 6\"/></svg></span>");
        }
        html.push_str("</nav>");
        html.push_str("<nav class=\"mode-picker\" aria-label=\"Document mode\">");
        let reader = if self.mode == Mode::Reader {
            " selected"
        } else {
            ""
        };
        let source = if self.mode == Mode::Source {
            " selected"
        } else {
            ""
        };
        let live = if self.mode == Mode::Live { " selected" } else { "" };
        let _ = write!(
            html,
            "<a class=\"mode{reader}\" href=\"wolfmark://mode/reader\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M3 4h7l2 2 2-2h7v15h-7l-2 2-2-2H3ZM12 6v15\"/></svg> &nbsp; Reader</a>"
        );
        let _ = write!(html, "<a class=\"mode{live}\" href=\"wolfmark://mode/live\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M3 4h18v16H3ZM12 4v16M6 8h3M6 12h3M15 8h3M15 12h3\"/></svg> &nbsp; Live Preview</a>");
        let _ = write!(
            html,
            "<a class=\"mode{source}\" href=\"wolfmark://mode/source\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"m8 6-6 6 6 6m8-12 6 6-6 6m-3-14-2 16\"/></svg> &nbsp; Source</a>"
        );
        html.push_str("</nav>");
        if matches!(self.mode, Mode::Source | Mode::Live) && self.path.is_some() {
            html.push_str("<a class=\"toolbar-link\" href=\"wolfmark://save/current\">Save</a><a class=\"toolbar-link\" href=\"wolfmark://save/as\">Save As</a>");
        }
        let dirty = if self.dirty() { "Unsaved" } else { "" };
        let _ = write!(
            html,
            "<span id=\"editor-status\" role=\"status\">{dirty}</span>"
        );
        html.push_str("<span class=\"viewbar-spacer\"></span><nav class=\"zoom-controls\" aria-label=\"Document zoom\">");
        let _ = write!(
            html,
            "<a href=\"wolfmark://zoom/out\" title=\"Zoom out\"><svg class=\"ui-icon\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#d7d7de\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"M5 12h14\"/></svg></a><a class=\"zoom-value\" href=\"wolfmark://zoom/reset\" title=\"Reset document zoom\">{}%</a><a href=\"wolfmark://zoom/in\" title=\"Zoom in\">+</a></nav>",
            self.zoom
        );
        html.push_str("</div><div class=\"findbar\" hidden><input id=\"find-query\" aria-label=\"Find in document\" placeholder=\"Find text\"><a href=\"wolfmark://find/previous\">Previous</a><a href=\"wolfmark://find/next\">Next</a><span id=\"find-count\" role=\"status\"></span><a href=\"wolfmark://find/close\" aria-label=\"Close search\">Close</a></div>");
        let document_padding = (48.0 * f64::from(self.zoom) / 100.0).round();
        let top_padding = (22.0 * f64::from(self.zoom) / 100.0).round();
        let bottom_padding = (34.0 * f64::from(self.zoom) / 100.0).round();
        let _ = write!(
            html,
            "<div class=\"document-viewport\"><div class=\"document-inner\" style=\"--qt-padding:{document_padding}px;--qt-top:{top_padding}px;--qt-bottom:{bottom_padding}px\">"
        );
        if let Some(notice) = &self.notice {
            let _ = write!(
                html,
                "<div class=\"notice\" role=\"status\">{}</div>",
                escape(notice)
            );
        }
        let size = self.zoom as f64 * 0.17;
        let _ = write!(
            html,
            "<article class=\"document-body\" style=\"--doc-size:{size:.1}px\">{body}</article>"
        );

        html.push_str("</div></div></main>");
        if self.settings_open {
            html.push_str(&self.settings_html());
        }
        html.push_str("</div></body></html>");
        html
    }
}

/// Contextual, source-preserving edit decorations. Span colors do not alter
/// font metrics; the editable textarea and mirror stay aligned at every caret.
pub fn decorated_source(source: &str) -> String {
    if source.len() > 256 * 1024 {
        return escape(source);
    }
    let total = source.encode_utf16().count();
    let mut color = vec![0u8; total];
    for decoration in crate::markdown_decorations::decorations(source) {
        let shade = match decoration.kind {
            "heading" => 1, "strong" => 2, "emphasis" => 3,
            "code" => 4, "fence" => 5, "link" => 6,
            "quote" => 7, "list" => 8, "strike" => 9,
            _ => 0,
        };
        if shade == 0 { continue; }
        for unit in decoration.start.min(total)..decoration.end.min(total) {
            color[unit] = shade;
        }
        for [start, end] in decoration.markers {
            for unit in start.min(total)..end.min(total) { color[unit] = 10; }
        }
    }
    let mut result = String::with_capacity(source.len() + 512);
    let mut active = 0;
    let mut offset = 0;
    for ch in source.chars() {
        let next = color.get(offset).copied().unwrap_or(0);
        if next != active {
            if active != 0 { result.push_str("</span>"); }
            if next != 0 {
                let _ = write!(result,"<span class=\"live-token-{}\">", next);
            }
            active = next;
        }
        match ch {
            '&' => result.push_str("&amp;"),
            '<' => result.push_str("&lt;"),
            '>' => result.push_str("&gt;"),
            '"' => result.push_str("&quot;"),
            '\'' => result.push_str("&#39;"),
            _ => result.push(ch),
        }
        offset += ch.len_utf16();
    }
    if active != 0 { result.push_str("</span>"); }
    result
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

fn data_svg(source: &str) -> String {
    // Bundled static artwork, never untrusted Markdown content.
    format!(
        "data:image/svg+xml;base64,{}",
        base64::engine::general_purpose::STANDARD.encode(source.as_bytes())
    )
}

fn data_png(bytes: &[u8]) -> String {
    let encoded = base64::engine::general_purpose::STANDARD.encode(bytes);
    format!("data:image/png;base64,{encoded}")
}

fn render_blocks(
    html: &mut String,
    blocks: &[model::Block],
    document: Option<&Path>,
    resources: &crate::image_resources::Resources,
    toc: &mut Vec<(u8, String, String)>,
) {
    for block in blocks {
        match block {
            model::Block::Paragraph(inlines) => {
                html.push_str("<p>");
                render_inlines(html, inlines, document, resources);
                html.push_str("</p>");
            }
            model::Block::Heading { level, content } => {
                let level = (*level).clamp(1, 6);
                let heading = model::plain_text(content);
                let base = crate::anchors::heading_slug(&heading);
                let mut anchor = base.clone();
                let mut suffix = 1;
                while toc.iter().any(|(_, _, existing)| existing == &anchor) {
                    anchor = format!("{base}-{suffix}");
                    suffix += 1;
                }
                toc.push((level, heading, anchor.clone()));
                let _ = write!(html, "<h{level} id=\"{anchor}\">");
                render_inlines(html, content, document, resources);
                let _ = write!(html, "</h{level}>");
            }
            model::Block::Quote(inner) => {
                html.push_str("<blockquote>");
                render_blocks(html, inner, document, resources, toc);
                html.push_str("</blockquote>");
            }
            model::Block::List {
                ordered,
                start,
                items,
            } => {
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
                    render_blocks(html, &item.blocks, document, resources, toc);
                    html.push_str("</li>");
                }
                html.push_str(if *ordered { "</ol>" } else { "</ul>" });
            }
            model::Block::CodeBlock { language, source } => {
                let safe_language = escape(language);
                let code_id = html.len();
                let _ = write!(
                    html,
                    "<div class=\"codebox\"><div class=\"codehead\"><span>{safe_language}</span><a class=\"code-copy\" href=\"wolfmark://copy/{code_id}\" title=\"Copy code block\">Copy</a></div><pre><code id=\"wm-code-{code_id}\">"
                );
                for span in highlight::highlight_code(language, source) {
                    let decoration = match span.flags & 3 {
                        1 => "font-weight:bold;",
                        2 => "font-style:italic;",
                        3 => "font-weight:bold;font-style:italic;",
                        _ => "",
                    };
                    let _ = write!(
                        html,
                        "<span style=\"color:rgb({},{},{});{}\">{}</span>",
                        span.red,
                        span.green,
                        span.blue,
                        decoration,
                        escape(&span.text)
                    );
                }
                html.push_str("</code></pre></div>");
            }
            model::Block::Table {
                header,
                rows,
                alignments,
            } => {
                html.push_str("<table><thead>");
                render_table_row(html, header, true, alignments, document, resources);
                html.push_str("</thead><tbody>");
                for row in rows {
                    render_table_row(html, row, false, alignments, document, resources);
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

fn render_table_row(
    html: &mut String,
    row: &model::TableRow,
    head: bool,
    alignments: &[model::TableAlignment],
    document: Option<&Path>,
    resources: &crate::image_resources::Resources,
) {
    html.push_str("<tr>");
    for (index, cell) in row.cells.iter().enumerate() {
        let tag = if head { "th" } else { "td" };
        let align = match alignments.get(index).copied().unwrap_or_default() {
            model::TableAlignment::Left => "left",
            model::TableAlignment::Center => "center",
            model::TableAlignment::Right => "right",
        };
        let _ = write!(html, "<{tag} style=\"text-align:{align}\">");
        render_inlines(html, cell, document, resources);
        let _ = write!(html, "</{tag}>");
    }
    html.push_str("</tr>");
}

fn render_inlines(
    html: &mut String,
    inlines: &[model::Inline],
    document: Option<&Path>,
    resources: &crate::image_resources::Resources,
) {
    for inline in inlines {
        match inline {
            model::Inline::Text(value) => html.push_str(&escape(value)),
            model::Inline::Code(value) => {
                let _ = write!(html, "<code>{}</code>", escape(value));
            }
            model::Inline::Strong(children) => {
                wrapped_inlines(html, "strong", children, document, resources)
            }
            model::Inline::Emphasis(children) => {
                wrapped_inlines(html, "em", children, document, resources)
            }
            model::Inline::Strikethrough(children) => {
                wrapped_inlines(html, "del", children, document, resources)
            }
            model::Inline::Link {
                destination,
                content,
            } => {
                // Arbitrary external links are deliberately inert in this offline shell.
                if destination.starts_with('#') && destination.len() < 200 {
                    let _ = write!(html, "<a href=\"{}\">", escape_attr(destination));
                    render_inlines(html, content, document, resources);
                    html.push_str("</a>");
                } else {
                    html.push_str("<span title=\"External navigation disabled in prototype\">");
                    render_inlines(html, content, document, resources);
                    html.push_str("</span>");
                }
            }
            model::Inline::Image { source, alt, .. } => {
                if let Some(src) = resources.register(source, document) {
                    let _ = write!(html, "<img src=\"{src}\" alt=\"{}\">", escape_attr(alt));
                } else {
                    let _ = write!(
                        html,
                        "<span class=\"image-error\" role=\"img\" aria-label=\"{}\">Image unavailable in prototype: {}</span>",
                        escape_attr(alt),
                        escape(source)
                    );
                }
            }
            model::Inline::SoftBreak => html.push('\n'),
            model::Inline::HardBreak => html.push_str("<br>"),
            model::Inline::RawHtml(value) => html.push_str(&escape(value)),
            model::Inline::TaskMark(checked) => {
                html.push_str(if *checked { "☑ " } else { "☐ " })
            }
        }
    }
}

fn wrapped_inlines(
    html: &mut String,
    tag: &str,
    contents: &[model::Inline],
    document: Option<&Path>,
    resources: &crate::image_resources::Resources,
) {
    let _ = write!(html, "<{tag}>");
    render_inlines(html, contents, document, resources);
    let _ = write!(html, "</{tag}>");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn live_preview_retains_raw_source_and_has_contextual_decoration() {
        let source = "# Heading\n\n**bold** and <script>invalid</script>";
        let decorated = decorated_source(source);
        assert!(decorated.contains("live-token-1"));
        assert!(decorated.contains("live-token-2"));
        assert!(!decorated.contains("<script>"));
        let mut session = Session::default();
        session.mode = Mode::Live;
        let html = session.render_html();
        assert!(html.contains("live-highlights"));
        assert!(html.contains("source-editor"));
        assert!(html.contains("wolfmark://mode/live"));
    }

    #[test]
    fn settings_screen_is_actionable_and_matches_qt_schema() {
        let mut session = Session::default();
        assert!(session.render_html().contains("wolfmark://settings/open"));
        session.begin_settings();
        let html = session.render_html();
        for url in [
            "wolfmark://settings/close",
            "wolfmark://settings/save",
            "wolfmark://settings/page-editor",
            "wolfmark://settings/check-updates",
            "wolfmark://settings/prereleases",
        ] {
            assert!(html.contains(url), "missing {url}");
        }
        session.settings_page("editor");
        assert!(session.render_html().contains("wolfmark://settings/word-wrap"));
        assert!(html.contains("Changes are saved only when you select Save"));
        let previous = session.preferences.editor_word_wrap;
        session.toggle_setting("word-wrap").unwrap();
        assert_eq!(session.preferences.editor_word_wrap, previous);
        session.cancel_settings();
        assert_eq!(session.preferences.editor_word_wrap, previous);
    }

    #[test]
    fn bundled_reader_has_no_development_notice_but_errors_remain_visible() {
        let mut session = Session::default();
        let html = session.render_html();
        assert!(!html.contains("Bundled architecture document —"));
        assert!(!html.contains("class=\"notice\""));
        assert!(html.contains("<svg class=\"ui-icon\""));
        assert!(!html.contains('▱'));
        session.notice = Some("Unable to open <missing>".into());
        assert!(
            session
                .render_html()
                .contains("Unable to open &lt;missing&gt;")
        );
    }

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
    fn frameless_controls_exist_only_in_opt_in_experiment() {
        let mut ui = Session::default();
        assert!(!ui.render_html().contains("wolfmark://window/minimize"));
        ui.frameless_test = true;
        let html = ui.render_html();
        assert!(html.contains("wolfmark://window/minimize"));
        assert!(html.contains("wolfmark://window/maximize"));
        assert!(html.contains("wolfmark://window/close"));
    }

    #[test]
    fn reader_uses_full_width_and_mountain_footer() {
        let ui = Session::default();
        let html = ui.render_html();
        assert!(html.contains("--qt-padding:48px"));
        assert!(html.contains("--qt-top:22px"));
        assert!(html.contains("data:image/svg+xml;base64,"));
        assert!(!html.contains("wolfmark-banner.png"));
        assert!(!CSS.contains("max-width:1150px"));
        assert!(CSS.contains("white-space:pre;word-break:normal"));
    }

    #[test]
    fn sidebar_selection_reuses_distinct_document_buffers() {
        let dir =
            std::env::temp_dir().join(format!("wolfmark-servo-multidoc-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let one = dir.join("one.md");
        let two = dir.join("two.md");
        std::fs::write(&one, "# First").unwrap();
        std::fs::write(&two, "# Second").unwrap();
        let mut session = Session::default();
        session.open(&one);
        session.mode = Mode::Source;
        session.zoom = 140;
        session.open(&two);
        assert_eq!(session.open_documents.len(), 2);
        assert!(session.source.contains("Second"));
        assert_eq!(session.mode, Mode::Reader);
        assert_eq!(session.zoom, 100);
        session.select(0);
        assert_eq!(session.mode, Mode::Source);
        assert_eq!(session.zoom, 140);
        assert!(session.source.contains("First"));
        assert!(session.render_html().contains("wolfmark://document/1"));
        session.select_bundled();
        assert!(session.path.is_none());
        session.open(&one);
        assert_eq!(session.open_documents.len(), 2);
        let _ = std::fs::remove_dir_all(dir);
    }

    #[test]
    fn history_and_close_adjust_document_indexes() {
        let dir =
            std::env::temp_dir().join(format!("wolfmark-servo-history-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let one = dir.join("alpha.md");
        let two = dir.join("beta.md");
        let three = dir.join("gamma.md");
        for (path, text) in [(&one, "# Alpha"), (&two, "# Beta"), (&three, "# Gamma")] {
            std::fs::write(path, text).unwrap();
        }
        let mut session = Session::default();
        session.open(&one);
        session.mode = Mode::Source;
        session.zoom = 140;
        session.open(&two);
        session.open(&three);
        assert!(session.can_go_back());
        session.back();
        assert!(session.source.contains("Beta"));
        session.back();
        assert!(session.source.contains("Alpha"));
        session.forward();
        assert!(session.source.contains("Beta"));
        // Removing a nonactive document shifts indexes but not the active buffer.
        session.close(0);
        assert!(session.source.contains("Beta"));
        assert!(session.render_html().contains("wolfmark://close/1"));
        session.close_active();
        assert!(session.source.contains("Gamma"));
        session.close_active();
        assert!(session.path.is_none());
        assert_eq!(session.open_documents.len(), 0);
        assert!(session.can_go_back());
        let _ = std::fs::remove_dir_all(dir);
    }

    #[test]
    fn adjacent_navigation_wraps_through_bundled_reader() {
        let dir = std::env::temp_dir().join(format!("wolfmark-servo-cycle-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let one = dir.join("single.md");
        std::fs::write(&one, "# Single").unwrap();
        let mut session = Session::default();
        session.open(&one);
        session.select_adjacent(false);
        assert!(session.path.is_none());
        session.select_adjacent(true);
        assert!(session.source.contains("Single"));
        let _ = std::fs::remove_dir_all(dir);
    }

    #[test]
    fn parser_and_highlighter_render_real_markdown() {
        let mut session = Session::default();
        session.source = "# Title\n\nA **wolf**.\n\n```rust\nfn main() {}\n```\n".into();
        let html = session.render_html();
        assert!(html.contains("<h1 id=\"title\">Title</h1>"));
        assert!(html.contains("<strong>wolf</strong>"));
        assert!(html.contains("class=\"codebox\""));
        assert!(html.contains("color:rgb("));
    }

    #[test]
    fn image_resources_follow_wolfmark_path_rules() {
        let root =
            std::env::temp_dir().join(format!("wolfmark-servo-images-{}", std::process::id()));
        std::fs::create_dir_all(&root).unwrap();
        let document = root.join("document.md");
        let image = root.join("wolf symbol.png");
        std::fs::write(&document, "# Image").unwrap();
        std::fs::write(
            &image,
            include_bytes!("../../../assets/icons/wolfmark-128x128.png"),
        )
        .unwrap();
        let resources = crate::image_resources::Resources::default();
        for src in [
            "wolf symbol.png".to_string(),
            "wolf%20symbol.png".to_string(),
            image.to_string_lossy().into_owned(),
            Url::from_file_path(&image).unwrap().to_string(),
        ] {
            let data = resources
                .register(&src, Some(&document))
                .expect("locally allowed image");
            assert!(resources.contains(&Url::parse(&data).unwrap()));
        }
        assert!(
            resources
                .register("https://example.com/image.png", Some(&document))
                .is_none()
        );
        assert!(
            resources
                .register("file:///nonexistent/wolf.png", Some(&document))
                .is_none()
        );
        let _ = std::fs::remove_dir_all(root);
    }

    #[test]
    fn reader_cache_survives_zoom_and_invalidates_on_source_change() {
        let mut session = Session::default();
        session.source = "# Original".into();
        session.render_html();
        let pointer = session
            .reader_cache
            .borrow()
            .as_ref()
            .unwrap()
            .body
            .as_ptr();
        session.change_zoom(10);
        session.render_html();
        assert_eq!(
            pointer,
            session
                .reader_cache
                .borrow()
                .as_ref()
                .unwrap()
                .body
                .as_ptr()
        );
        session.source = "# Changed".into();
        assert!(session.render_html().contains("id=\"changed\""));
    }

    #[test]
    fn natural_heading_links_and_duplicate_ids() {
        let mut session = Session::default();
        session.source =
            "# Hello World\n\n[Jump](#hello-world)\n\n# Hello World\n\n# Hello World-1".into();
        let html = session.render_html();
        assert!(html.contains("id=\"hello-world\""));
        assert!(html.contains("href=\"#hello-world\""));
        assert!(html.contains("id=\"hello-world-1\""));
        assert!(html.contains("id=\"hello-world-1-1\""));
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
