// SPDX-License-Identifier: MPL-2.0
//! Minimal Servo embedding investigation. Not the approved Wolfmark interface.
//!
//! Source basis: Servo v0.7.0 components/servo/examples/winit_minimal.rs (MPL-2.0).
//! Adapted for a bundled, script-free data: page and basic pointer input.
//!
//! This is NOT a validated no-network sandbox: engine background traffic,
//! navigation, resource requests, and OS-level network isolation need auditing.

// Surfman uses these exports to select a hardware GPU on hybrid Windows laptops.
// Without them it warns and may choose an unsuitable adapter.
surfman::declare_surfman!();

use std::cell::{Cell, RefCell};
use std::error::Error;
use std::path::PathBuf;
use std::rc::Rc;
use std::sync::mpsc;
use std::time::Instant;

// Shared Rust code from the existing Wolfmark core, without linking Qt.
#[path = "../../../src/markdown/highlight.rs"]
mod highlight;
#[allow(dead_code, unused_imports)]
#[path = "../../../src/images/mod.rs"]
mod images;
#[path = "../../../src/markdown/model.rs"]
mod model;
#[path = "../../../src/markdown/parser.rs"]
mod parser;
#[allow(dead_code)]
#[path = "../../../src/presentation.rs"]
mod presentation;

#[path = "../../../src/markdown/anchors.rs"]
mod anchors;
#[path = "../../../src/markdown/editor.rs"]
mod markdown_decorations;
mod editor;
mod image_resources;
mod search;
mod smooth_scroll;
mod middle_scroll;
mod ui;
mod user_settings;
#[path = "../../../src/updates.rs"]
mod updates;
mod update_checks;
#[cfg(target_os = "windows")]
mod windows_chrome;

use euclid::Scale;
use percent_encoding::{NON_ALPHANUMERIC, utf8_percent_encode};
use servo::{
    Code as ServoCode, InputEvent, Key as ServoKey, KeyState as ServoKeyState,
    KeyboardEvent as ServoKeyboardEvent, Location as ServoLocation, Modifiers as ServoModifiers,
    MouseButton as ServoMouseButton, MouseButtonAction, MouseButtonEvent, MouseMoveEvent,
    NamedKey as ServoNamedKey, NavigationRequest, RenderingContext, Servo, ServoBuilder, WebView,
    WebViewBuilder, WheelDelta, WheelEvent, WheelMode, WindowRenderingContext,
};
use tracing::warn;
use url::Url;
use webrender_api::units::DevicePoint;
use winit::application::ApplicationHandler;
use winit::event::{
    ElementState, KeyEvent, MouseButton as WinitMouseButton, MouseScrollDelta, WindowEvent,
};
use winit::event_loop::{ActiveEventLoop, ControlFlow, EventLoop, EventLoopProxy};
use winit::keyboard::{
    Key as WinitKey, KeyCode, KeyLocation, NamedKey as WinitNamedKey, PhysicalKey,
};
use winit::raw_window_handle::{HasDisplayHandle, HasWindowHandle};
use winit::window::{ResizeDirection, Window};

fn main() -> Result<(), Box<dyn Error>> {
    rustls::crypto::aws_lc_rs::default_provider()
        .install_default()
        .expect("Servo TLS crypto provider could not be installed");

    // No Qt link, localhost web server, loose HTML file, or system WebView2.
    let loop_ = EventLoop::<Wake>::with_user_event().build()?;
    // The accepted UI is now the default. Keep the tiny original page as an
    // explicit graphics diagnostic, not as the Wolfmark replacement.
    let mut session = ui::Session::default();
    // On Windows use Wolfmark's own caption by default. Keep the OS frame on
    // X11/Linux pending actual native resize/snap parity.
    session.frameless_test = cfg!(target_os = "windows");
    let mut smoke_page = false;
    let mut args = std::env::args_os().skip(1);
    while let Some(arg) = args.next() {
        if arg == "--smoke-page" {
            smoke_page = true;
        } else if arg == "--frameless-test" {
            session.frameless_test = true;
        } else if arg == "--native-frame" {
            session.frameless_test = false;
        } else if arg == "--document" {
            if let Some(path) = args.next() {
                session.open(&PathBuf::from(path));
            }
        } else {
            let path = PathBuf::from(arg);
            if ui::supported_document(&path) {
                session.open(&path);
            }
        }
    }
    if std::env::var("WOLFMARK_SNAPSHOT_MODE").is_ok_and(|mode| mode == "source") {
        session.mode = ui::Mode::Source;
    }
    let mut app = App::Initial(Waker(loop_.create_proxy()), session, smoke_page);
    loop_.run_app(&mut app)?;
    Ok(())
}

struct State {
    window: Window,
    servo: Servo,
    context: Rc<WindowRenderingContext>,
    view: RefCell<Vec<WebView>>,
    cursor: RefCell<DevicePoint>,
    modifiers: RefCell<ServoModifiers>,
    document: RefCell<ui::Session>,
    pending: RefCell<Option<Action>>,
    smooth_wheel: RefCell<smooth_scroll::SmoothWheel>,
    middle_scroll: RefCell<middle_scroll::MiddleScroll>,
    scroll_origin: RefCell<DevicePoint>,
    reduced_motion: bool,
    frameless: bool,
    close_requested: Rc<Cell<bool>>,
    snapshot_requested: Cell<bool>,
    pre_fullscreen_maximized: Cell<bool>,
    last_caption_click: Cell<Option<(Instant, f64, f64)>>,
    clipboard: Rc<RefCell<Option<arboard::Clipboard>>>,
    search: Rc<RefCell<search::Search>>,
    update_receiver: RefCell<Option<mpsc::Receiver<Result<update_checks::Report, String>>>>,
    synced_action: RefCell<Option<Action>>,
    syncing: Cell<bool>,
    waker: Waker,
    dropped_file: RefCell<Option<PathBuf>>,
    scripts: Rc<RefCell<Vec<(WebView, String)>>>,
    gui_probe_started: Cell<bool>,
    gui_probe_done: Cell<bool>,
    smoke_page: bool,
}

impl servo::WebViewDelegate for State {
    fn notify_load_status_changed(&self, webview: WebView, status: embedder_traits::LoadStatus) {
        if status != embedder_traits::LoadStatus::Complete || self.smoke_page {
            return;
        }
        if std::env::var("WOLFMARK_SERVO_GUI_TEST").is_ok_and(|value| value == "source-save") {
            if !self.gui_probe_started.replace(true) {
                webview.evaluate_javascript("{const e=document.getElementById('source-editor');if(e){e.value+='\\nServo GUI edit ✓\\n';console.debug('WOLFMARK_GUI_SAVE');}}", |_| {});
            } else if !self.document.borrow().dirty()
                && self.document.borrow().source.contains("Servo GUI edit ✓")
            {
                eprintln!("SERVO_GUI source edit synchronized and saved");
                self.gui_probe_done.set(true);
                self.window.request_redraw();
            }
        }
        if std::env::var("WOLFMARK_SERVO_GUI_TEST").is_ok_and(|value| value == "reader")
            && !self.gui_probe_started.replace(true)
        {
            self.scripts.borrow_mut().push((webview.clone(),"document.querySelector('.findbar').hidden=false;document.getElementById('find-query').value='Wolfmark';console.debug('WOLFMARK_GUI_FIND');".into()));
            let _ = self.waker.0.send_event(Wake);
        }
        if self.document.borrow().mode == ui::Mode::Reader {
            let top = self.document.borrow().reader_scroll.max(0.0);
            webview.evaluate_javascript(format!(
                "if(document.querySelector('.document-viewport'))document.querySelector('.document-viewport').scrollTop={top};"
            ), |_| {});
        }
        let revision = self.document.borrow().view_revision.get();
        let (selection_start, selection_end, scroll_top) = self.document.borrow().editor_view();
        // Only host-supplied bridge code executes. Markdown and CSS cannot supply scripts.
        webview.evaluate_javascript(format!(r#"(() => {{
            const editor=document.getElementById('source-editor');
            if(!editor || editor.readOnly)return;
            editor.setSelectionRange({selection_start},{selection_end});editor.scrollTop={scroll_top};
            let dirty=document.getElementById('editor-status')?.textContent.includes('Unsaved')||false;
            let pendingUpdate=null;
            const updateStatus=()=>{{
                const before=editor.value.slice(0,editor.selectionStart);
                const line=before.split('\n').length;
                const column=before.length-before.lastIndexOf('\n');
                const label='Ln '+line+':'+column+(dirty?' · Unsaved':'');
                const status=document.getElementById('editor-status');
                if(status)status.textContent=label;
            }};
            const changed=()=>{{
                dirty=true;
                updateStatus();
                if(pendingUpdate!==null)clearTimeout(pendingUpdate);
                // Large files must not be re-serialized/reparsed on every
                // physical key stroke. Save/switch still forces a fresh sync.
                pendingUpdate=setTimeout(()=>{{
                    console.debug('WOLFMARK_EDITOR:'+JSON.stringify([{revision},editor.value,editor.selectionStart,editor.selectionEnd,editor.scrollTop]));
                    pendingUpdate=null;
                }},75);
            }};
            editor.addEventListener('input',changed);
            editor.addEventListener('click',updateStatus);
            editor.addEventListener('keyup',updateStatus);
            editor.addEventListener('select',updateStatus);
            updateStatus();
            const mirror=document.getElementById('live-highlights');
            if(mirror){{
                const syncScroll=()=>{{mirror.scrollTop=editor.scrollTop;mirror.scrollLeft=editor.scrollLeft;}};
                editor.addEventListener('scroll',syncScroll);
                syncScroll();
            }}
            editor.addEventListener('keydown',event=>{{
                if(event.key==='Tab' && !event.ctrlKey && !event.altKey){{
                    event.preventDefault();
                    if(!event.shiftKey){{editor.setRangeText('    ',editor.selectionStart,editor.selectionEnd,'end');changed();}}
                    else{{const start=editor.selectionStart;const line=editor.value.lastIndexOf('\n',start-1)+1;const count=Math.min(4,(editor.value.slice(line).match(/^ */)||[''])[0].length);editor.setRangeText('',line,line+count,'preserve');changed();}}
                }} else if((event.ctrlKey||event.metaKey) && !event.altKey
                   && (event.key.toLowerCase()==='b'||event.key.toLowerCase()==='i')){{
                    event.preventDefault();
                    const marker=event.key.toLowerCase()==='b'?'**':'*';
                    const start=editor.selectionStart,end=editor.selectionEnd;
                    const selected=editor.value.slice(start,end);
                    const already=selected.startsWith(marker)&&selected.endsWith(marker)
                                  &&selected.length>=marker.length*2;
                    const replacement=already?selected.slice(marker.length,-marker.length)
                                             :marker+selected+marker;
                    editor.setRangeText(replacement,start,end,'select');
                    if(already){{
                        editor.setSelectionRange(start,start+replacement.length);
                    }}else{{
                        editor.setSelectionRange(start+marker.length,start+marker.length+selected.length);
                    }}
                    changed();
                }}
            }});
        }})()"#), |_| {});
    }
    fn show_console_message(
        &self,
        webview: WebView,
        _level: embedder_traits::ConsoleLogLevel,
        message: String,
    ) {
        if std::env::var("WOLFMARK_SERVO_GUI_TEST").is_ok_and(|value| value == "reader") {
            if message == "WOLFMARK_GUI_FIND" {
                *self.synced_action.borrow_mut() = Some(Action::ZoomIn);
                *self.pending.borrow_mut() = Some(Action::FindNext(false));
                self.window.request_redraw();
                return;
            }
            if let Some(json) = message.strip_prefix("WOLFMARK_GUI_READER:") {
                if let Ok([selection, zoom, count]) = serde_json::from_str::<[String; 3]>(json) {
                    if selection == "Wolfmark" && zoom == "110%" && count.starts_with("1 /") {
                        eprintln!("SERVO_GUI reader search selection and retained-DOM zoom passed");
                        self.gui_probe_done.set(true);
                        self.window.request_redraw();
                    } else {
                        eprintln!("SERVO_GUI reader failed: {selection:?} {zoom:?} {count:?}");
                    }
                }
                return;
            }
        }
        if message == "WOLFMARK_GUI_SAVE"
            && std::env::var("WOLFMARK_SERVO_GUI_TEST").is_ok_and(|value| value == "source-save")
        {
            *self.pending.borrow_mut() = Some(Action::Save(false));
            self.window.request_redraw();
            return;
        }
        let Some(payload) = message.strip_prefix("WOLFMARK_EDITOR:") else {
            return;
        };
        let Ok((revision, value, start, end, top)) =
            serde_json::from_str::<(u64, String, usize, usize, f64)>(payload)
        else {
            return;
        };
        let mut document = self.document.borrow_mut();
        if revision != document.view_revision.get()
            || !matches!(document.mode, ui::Mode::Source | ui::Mode::Live) {
            return;
        }
        document.set_editor_view((start, end, top));
        if let Err(error) = document.apply_editor_view(&value) {
            document.notice = Some(error);
        } else if matches!(document.mode, ui::Mode::Live | ui::Mode::Source) {
            // Value is already the normalized textarea view; using raw source
            // here would shift decoration offsets for BOM/mixed line endings.
            let markup = ui::decorated_source(&value);
            let escaped = serde_json::to_string(&markup).unwrap_or_else(|_| "\"\"".into());
            let line_numbers = (1..=value.split('\n').count()).map(|line| line.to_string()).collect::<Vec<_>>().join("\n");
            let number_json = serde_json::to_string(&line_numbers).unwrap_or_else(|_| "\"\"".into());
            self.scripts.borrow_mut().push((webview.clone(),
                format!("if(document.getElementById('live-highlights')) document.getElementById('live-highlights').innerHTML={escaped};if(document.getElementById('live-line-numbers')) document.getElementById('live-line-numbers').textContent={number_json};")));
            let _ = self.waker.0.send_event(Wake);
        }
        self.window.set_title(&format!(
            "{}{} — Wolfmark (Servo experiment)",
            document.title(),
            if document.dirty() { " *" } else { "" }
        ));
    }
    fn notify_new_frame_ready(&self, webview: WebView) {
        if std::env::var("WOLFMARK_SERVO_GUI_TEST")
            .is_ok_and(|value| matches!(value.as_str(), "reader" | "source-save"))
            && !self.gui_probe_done.get()
        {
            self.window.request_redraw();
            return;
        }
        if let Some(path) = std::env::var_os("WOLFMARK_SERVO_SNAPSHOT") {
            if !self.snapshot_requested.replace(true) {
                let done = self.close_requested.clone();
                webview.take_screenshot(None, move |result| {
                    match result {
                        Ok(image) => match image.save(PathBuf::from(path)) {
                            Ok(()) => eprintln!("SERVO_SNAPSHOT saved"),
                            Err(error) => eprintln!("SERVO_SNAPSHOT save failed: {error}"),
                        },
                        Err(error) => eprintln!("SERVO_SNAPSHOT capture failed: {error:?}"),
                    }
                    done.set(true);
                });
            }
        }
        self.window.request_redraw();
    }

    fn load_web_resource(&self, _webview: WebView, load: servo::WebResourceLoad) {
        // Servo 0.7 also invokes this callback for our embedded data: page.
        // Dropping an unintercepted load lets Servo process its own local data.
        // Intercept all other loads while network access remains forbidden.
        // ServoDelegate's non-webview/background requests need a separate audit.
        let url = load.request().url.clone();
        if let Some(resources) = self
            .document
            .borrow()
            .all_resources()
            .into_iter()
            .find(|resources| resources.contains(&url))
        {
            resources.load(load);
            self.window.request_redraw();
            return;
        }
        if allow_local_probe_resource(&url) {
            return;
        }
        // Avoid printing huge data: payloads or other user-controlled URL data.
        eprintln!("Blocked Servo resource request (scheme: {})", url.scheme());
        let response = embedder_traits::WebResourceResponse::new(url)
            .status_code(http::StatusCode::FORBIDDEN)
            .status_message(b"Offline-only experiment".to_vec());
        load.intercept(response).finish();
    }

    fn request_permission(&self, _webview: WebView, request: servo::PermissionRequest) {
        request.deny();
    }

    fn request_navigation(&self, _webview: WebView, request: NavigationRequest) {
        // Rust remains authoritative: UI links enqueue a command for the host,
        // rather than giving the document script or filesystem privileges.
        if request.url.scheme() == "wolfmark" {
            *self.pending.borrow_mut() = Action::from_url(&request.url);
            if self.pending.borrow().is_some() {
                self.window.request_redraw();
            }
            request.deny();
        } else if request.url.scheme() == "data" {
            request.allow();
        } else {
            eprintln!(
                "Blocked external document navigation (scheme: {})",
                request.url.scheme()
            );
            request.deny();
        }
    }
}

fn allow_local_probe_resource(url: &Url) -> bool {
    // This probe packages its HTML and small image into data: URLs. This is
    // intentionally narrower than general local-file or custom-protocol access.
    url.scheme() == "data"
}

#[cfg(test)]
mod offline_resource_tests {
    use super::allow_local_probe_resource;
    use url::Url;

    #[test]
    fn embedded_data_resources_can_load() {
        for source in [
            "data:text/html;charset=utf-8,%3Cp%3EProbe%3C/p%3E",
            "data:image/png;base64,iVBORw0KGgo=",
        ] {
            assert!(allow_local_probe_resource(&Url::parse(source).unwrap()));
        }
    }

    #[test]
    fn external_and_file_resources_are_not_exempt() {
        for source in [
            "https://example.com/",
            "http://127.0.0.1:8080/",
            "file:///C:/private.txt",
            "ftp://example.com/file",
        ] {
            assert!(!allow_local_probe_resource(&Url::parse(source).unwrap()));
        }
    }
}

// Cover engine requests that are not associated with a document WebView too.
struct OfflineEngine;
impl servo::ServoDelegate for OfflineEngine {
    fn load_web_resource(&self, load: servo::WebResourceLoad) {
        let response = embedder_traits::WebResourceResponse::new(load.request().url.clone())
            .status_code(http::StatusCode::FORBIDDEN)
            .status_message(b"Offline-only application".to_vec());
        load.intercept(response).finish();
    }
}
enum App {
    Initial(Waker, ui::Session, bool),
    Running(Rc<State>),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Action {
    Open,
    OpenDropped,
    Refresh,
    Reader,
    Live,
    Source,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    Fullscreen,
    Save(bool),
    Undo(bool),
    CopyCode(usize),
    FindOpen,
    FindClose,
    FindNext(bool),
    CheckUpdates,
    SettingsOpen, SettingsClose, SettingsSave, SettingsPage(&'static str), SettingsToggle(&'static str),
    SidebarToggle,
    OutlineLine(usize),
    SelectDocument(usize),
    SelectBundled,
    CloseDocument(usize),
    CloseActive,
    HistoryBack,
    HistoryForward,
    AdjacentDocument(bool),
    WindowMinimize,
    WindowMaximize,
    WindowClose,
}

impl Action {
    fn from_url(url: &Url) -> Option<Self> {
        if url.scheme() != "wolfmark" {
            return None;
        }
        match (url.host_str()?, url.path()) {
            ("open", "/") => Some(Self::Open),
            ("save", "/current") => Some(Self::Save(false)),
            ("save", "/as") => Some(Self::Save(true)),
            ("find", "/open") => Some(Self::FindOpen),
            ("find", "/close") => Some(Self::FindClose),
            ("find", "/next") => Some(Self::FindNext(false)),
            ("find", "/previous") => Some(Self::FindNext(true)),
            ("updates", "/check") => Some(Self::CheckUpdates),
            ("settings", "/open") => Some(Self::SettingsOpen),
            ("sidebar", "/toggle") => Some(Self::SidebarToggle),
            ("outline", path) => path.strip_prefix("/line/")
                .and_then(|line| line.parse::<usize>().ok())
                .filter(|line| *line > 0)
                .map(Self::OutlineLine),
            ("settings", "/close") => Some(Self::SettingsClose),
            ("settings", "/save") => Some(Self::SettingsSave),
            ("settings", "/page-updates") => Some(Self::SettingsPage("updates")),
            ("settings", "/page-editor") => Some(Self::SettingsPage("editor")),
            ("settings", "/page-about") => Some(Self::SettingsPage("about")),
            ("settings", "/word-wrap") => Some(Self::SettingsToggle("word-wrap")),
            ("settings", "/check-updates") => Some(Self::SettingsToggle("check-updates")),
            ("settings", "/prereleases") => Some(Self::SettingsToggle("prereleases")),
            ("copy", path) => path
                .strip_prefix('/')
                .and_then(|id| id.parse().ok())
                .map(Self::CopyCode),
            ("refresh", "/") => Some(Self::Refresh),
            ("mode", "/reader") => Some(Self::Reader),
            ("mode", "/live") => Some(Self::Live),
            ("mode", "/source") => Some(Self::Source),
            ("zoom", "/in") => Some(Self::ZoomIn),
            ("zoom", "/out") => Some(Self::ZoomOut),
            ("zoom", "/reset") => Some(Self::ZoomReset),
            ("document", "/bundled") => Some(Self::SelectBundled),
            ("history", "/back") => Some(Self::HistoryBack),
            ("history", "/forward") => Some(Self::HistoryForward),
            ("window", "/minimize") => Some(Self::WindowMinimize),
            ("window", "/maximize") => Some(Self::WindowMaximize),
            ("window", "/close") => Some(Self::WindowClose),
            ("close", path) => path
                .strip_prefix('/')
                .and_then(|index| index.parse::<usize>().ok())
                .map(Self::CloseDocument),
            ("document", path) => path
                .strip_prefix('/')
                .and_then(|index| index.parse::<usize>().ok())
                .map(Self::SelectDocument),
            _ => None,
        }
    }
}

impl State {
    fn apply_action(self: &Rc<Self>, action: Action) {
        // Qt restores the Reader scroll for each opened document. Snapshot the
        // actual Servo scroll offset before any page-replacing navigation.
        // In-place toolbar actions and Settings must not trigger a reload.
        let reader_navigation = matches!(action,
            Action::Open | Action::OpenDropped | Action::Refresh
            | Action::SelectDocument(_) | Action::SelectBundled
            | Action::CloseDocument(_) | Action::CloseActive
            | Action::HistoryBack | Action::HistoryForward
            | Action::AdjacentDocument(_) | Action::Source | Action::Live);
        if self.document.borrow().mode == ui::Mode::Reader && reader_navigation {
            if self.syncing.replace(true) { return; }
            if let Some(view) = self.view.borrow().last() {
                let weak = Rc::downgrade(self);
                view.evaluate_javascript(
                    "JSON.stringify(document.querySelector('.document-viewport')?.scrollTop ?? 0)",
                    move |result| {
                        let Some(state) = weak.upgrade() else { return; };
                        if let Ok(embedder_traits::JSValue::String(value)) = result {
                            if let Ok(top) = serde_json::from_str::<f64>(&value) {
                                if top.is_finite() && top >= 0.0 {
                                    state.document.borrow_mut().reader_scroll = top;
                                }
                            }
                        }
                        state.syncing.set(false);
                        *state.synced_action.borrow_mut() = Some(action);
                        let _ = state.waker.0.send_event(Wake);
                    },
                );
                return;
            }
            self.syncing.set(false);
        }
        if !matches!(self.document.borrow().mode, ui::Mode::Source | ui::Mode::Live) || self.document.borrow().path.is_none()
        {
            self.apply_synced_action(action);
            return;
        }
        if self.syncing.replace(true) {
            return;
        }
        let state = Rc::downgrade(self);
        if let Some(view) = self.view.borrow().last() {
            view.evaluate_javascript("(() => {const editor=document.getElementById('source-editor');if(!editor)return null;editor.readOnly=true;return JSON.stringify([editor.value,editor.selectionStart,editor.selectionEnd,editor.scrollTop]);})()",move |result| {
                let Some(state)=state.upgrade() else {return;};
                let result=match result {
                                        Ok(embedder_traits::JSValue::String(json)) => {
                        match serde_json::from_str::<(String,usize,usize,f64)>(&json) {
                            Ok((value,start,end,top))=>{let mut document=state.document.borrow_mut();document.set_editor_view((start,end,top));document.apply_editor_view(&value)},
                            Err(error)=>Err(error.to_string()),
                        }
                    },
                    _=>Err("Could not synchronize source; action cancelled to preserve edits".into()),
                };
                state.syncing.set(false);
                if let Err(error)=result {
                    state.document.borrow_mut().notice=Some(error.clone());
                    let error=serde_json::to_string(&error).unwrap();
                    if let Some(view)=state.view.borrow().last() {
                        state.scripts.borrow_mut().push((view.clone(),format!("document.getElementById('source-editor').readOnly=false;document.getElementById('editor-status').textContent={error};")));
                    }
                }
                else {*state.synced_action.borrow_mut()=Some(action);}
                let _=state.waker.0.send_event(Wake);
            });
        }
    }
    // Qt Settings is modal. Keyboard focus must not fall through to a Source
    // editor while the settings overlay owns input. Only these host-supplied
    // scripts may execute; document Markdown remains inert.
    fn settings_focus_script() -> &'static str {
        r#"(() => {
            const overlay=document.querySelector('.settings-overlay');
            if(!overlay)return;
            overlay.setAttribute('tabindex','-1');
            overlay.addEventListener('keydown',event=>{
                if(event.key!=='Tab')return;
                const items=Array.from(overlay.querySelectorAll('a[href]'))
                    .filter(item=>item.getClientRects().length>0);
                if(!items.length){event.preventDefault();overlay.focus();return;}
                const current=items.indexOf(document.activeElement);
                const next=event.shiftKey
                    ? (current<=0?items.length-1:current-1)
                    : (current<0||current===items.length-1?0:current+1);
                event.preventDefault();items[next].focus();
            });
            (overlay.querySelector('.settings-nav-current')
              ||overlay.querySelector('.settings-close')||overlay).focus();
        })()"#
    }

    fn apply_synced_action(&self, action: Action) {
        // The Settings overlay behaves as a modal dialog: document/navigation
        // commands must not act behind it while a draft is being edited.
        if self.document.borrow().settings_open
            && !matches!(action, Action::SettingsOpen | Action::SettingsClose
                | Action::SettingsSave | Action::SettingsPage(_) | Action::SettingsToggle(_)
                | Action::CheckUpdates)
        {
            return;
        }
        if let Some(view) = self.view.borrow().last() {
            if self.document.borrow().path.is_some() {
                view.evaluate_javascript("{const editor=document.getElementById('source-editor');if(editor)editor.readOnly=false;}", |_| {});
            }
        }
        if self.smoke_page {
            return;
        }
        // Window commands do not reload the document and cannot be delegated
        // to untrusted HTML or JavaScript.
        match action {
            Action::CheckUpdates => {
                self.request_update_check();
                return;
            }
            Action::SettingsOpen => {
                self.document.borrow_mut().begin_settings();
                let html = self.document.borrow().settings_html();
                let serialized = serde_json::to_string(&html).expect("settings HTML serializable");
                if let Some(view) = self.view.borrow().last() {
                    // Do not navigate/reparse/reload images just to show a dialog.
                    view.evaluate_javascript(format!(
                        "window.__wmSettingsPreviousFocus=document.activeElement;document.querySelector('.settings-overlay')?.remove();document.querySelector('.app')?.insertAdjacentHTML('beforeend',{serialized});{}",
                        Self::settings_focus_script()
                    ), |_| {});
                    self.servo.spin_event_loop();
                }
                return;
            }
            Action::SettingsClose => {
                self.document.borrow_mut().cancel_settings();
                if let Some(view) = self.view.borrow().last() {
                    view.evaluate_javascript("document.querySelector('.settings-overlay')?.remove();if(window.__wmSettingsPreviousFocus?.isConnected)window.__wmSettingsPreviousFocus.focus();window.__wmSettingsPreviousFocus=null;", |_| {});
                    self.servo.spin_event_loop();
                }
                return;
            }
            Action::SettingsSave => {
                let result = self.document.borrow_mut().save_settings();
                if let Some(view) = self.view.borrow().last() {
                    match result {
                        Ok(()) => {
                            let wrapping = self.document.borrow().preferences.editor_word_wrap;
                            view.evaluate_javascript(format!(r#"(() => {{
                                document.querySelector('.settings-overlay')?.remove();
                                if(window.__wmSettingsPreviousFocus?.isConnected)window.__wmSettingsPreviousFocus.focus();
                                window.__wmSettingsPreviousFocus=null;
                                const editor=document.getElementById('source-editor');
                                if(editor) {{
                                    editor.wrap='{wrap}';
                                    editor.style.whiteSpace='{white_space}';
                                    editor.style.overflowWrap='{overflow_wrap}';
                                }}
                                const live=document.querySelector('.live-stage');
                                if(live)live.classList.toggle('live-wrap',{wrapping});
                            }})()"#,
                            wrap=if wrapping{"soft"}else{"off"},
                            white_space=if wrapping{"pre-wrap"}else{"pre"},
                            overflow_wrap=if wrapping{"anywhere"}else{"normal"}), |_| {});
                        }
                        Err(error) => {
                            let serialized = serde_json::to_string(&error).unwrap_or_default();
                            view.evaluate_javascript(format!(
                                "document.querySelector('.settings-footer span').textContent={serialized};"
                            ), |_| {});
                        }
                    }
                    self.servo.spin_event_loop();
                }
                return;
            }
            Action::SettingsPage(page) => {
                if self.document.borrow().settings_open {
                    self.document.borrow_mut().settings_page(page);
                    let html = self.document.borrow().settings_html();
                    let serialized = serde_json::to_string(&html).expect("settings HTML serializable");
                    if let Some(view) = self.view.borrow().last() {
                        view.evaluate_javascript(format!(
                            "document.querySelector('.settings-overlay').outerHTML={serialized};{}",
                            Self::settings_focus_script()
                        ), |_| {});
                        self.servo.spin_event_loop();
                    }
                }
                return;
            }
            Action::SettingsToggle(field) => {
                if self.document.borrow().settings_open {
                    if let Err(error) = self.document.borrow_mut().toggle_setting(field) {
                        eprintln!("Settings update failed: {error}");
                    }
                    let html = self.document.borrow().settings_html();
                    let serialized = serde_json::to_string(&html).expect("settings HTML serializable");
                    if let Some(view) = self.view.borrow().last() {
                        view.evaluate_javascript(format!(
                            "document.querySelector('.settings-overlay').outerHTML={serialized};{}",
                            Self::settings_focus_script()
                        ), |_| {});
                        self.servo.spin_event_loop();
                    }
                }
                return;
            }
            Action::SidebarToggle => {
                let collapsed = {
                    let mut document = self.document.borrow_mut();
                    document.sidebar_collapsed = !document.sidebar_collapsed;
                    document.sidebar_collapsed
                };
                if let Some(view) = self.view.borrow().last() {
                    view.evaluate_javascript(format!(
                        "document.querySelector('.app')?.classList.toggle('sidebar-collapsed',{collapsed});"
                    ), |_| {});
                    self.servo.spin_event_loop();
                }
                return;
            }
            Action::OutlineLine(line) => {
                if let Some(view) = self.view.borrow().last() {
                    // This action is only offered for editable source outlines.
                    // Restrict navigation to that mode and use numeric lines.
                    if matches!(self.document.borrow().mode, ui::Mode::Source | ui::Mode::Live) {
                        view.evaluate_javascript(format!(r#"(() => {{
                            const editor=document.getElementById('source-editor');
                            if(!editor)return;
                            const start=editor.value.split('\n').slice(0,{before}).join('\n').length;
                            const index={before}===0 ? 0 : Math.min(editor.value.length,start+1);
                            editor.focus();editor.setSelectionRange(index,index);
                            const height=parseFloat(getComputedStyle(editor).lineHeight)||21;
                            editor.scrollTop=Math.max(0,{before}*height-editor.clientHeight*.35);
                        }})()"#, before=line.saturating_sub(1)), |_| {});
                        self.servo.spin_event_loop();
                    }
                }
                return;
            }
            Action::CopyCode(id) => {
                if let Some(view) = self.view.borrow().last() {
                    let clipboard = self.clipboard.clone();
                    view.evaluate_javascript(
                        format!("document.getElementById('wm-code-{id}')?.textContent"),
                        move |result| {
                            if let Ok(embedder_traits::JSValue::String(text)) = result {
                                let mut clipboard = clipboard.borrow_mut();
                                if clipboard.is_none() {
                                    *clipboard = arboard::Clipboard::new().ok();
                                }
                                if let Some(clipboard) = clipboard.as_mut() {
                                    if let Err(error) = clipboard.set_text(text) {
                                        eprintln!("Clipboard copy failed: {error}");
                                    }
                                }
                            }
                        },
                    );
                }
                return;
            }
            Action::FindOpen | Action::FindClose => {
                if let Some(view) = self.view.borrow().last() {
                    view.evaluate_javascript(if action == Action::FindOpen {
                        "document.querySelector('.findbar').hidden=false;document.getElementById('find-query').focus();document.getElementById('find-query').select();"
                    } else { "document.querySelector('.findbar').hidden=true;" }, |_| {});
                }
                return;
            }
            Action::FindNext(backwards) => {
                if let Some(view) = self.view.borrow().last() {
                    let search = self.search.clone();
                    let gui_test = std::env::var("WOLFMARK_SERVO_GUI_TEST")
                        .is_ok_and(|value| value == "reader");
                    let target = view.clone();
                    let scripts = self.scripts.clone();
                    let waker = self.waker.clone();
                    let defer = move |script: String| {
                        scripts.borrow_mut().push((target.clone(), script));
                        let _ = waker.0.send_event(Wake);
                    };
                    view.evaluate_javascript(r#"JSON.stringify([document.getElementById('source-editor')?.value ?? document.querySelector('.document-body').textContent,document.getElementById('find-query').value])"#, move |result| {
                        let Ok(embedder_traits::JSValue::String(json)) = result else { return; };
                        let Ok([text, query]) = serde_json::from_str::<[String;2]>(&json) else { return; };
                        let found = search.borrow_mut().next(&text, &query, backwards);
                        let Some((start,end,current,count)) = found else {
                            defer("document.getElementById('find-count').textContent='No matches'".into());
                            return;
                        };
                        defer(format!(r#"(() => {{
                            const editor=document.getElementById('source-editor');
                            if(editor) {{
                                editor.focus();editor.setSelectionRange({start},{end});
                                const line=editor.value.slice(0,{start}).split('\n').length-1;
                                editor.scrollTop=Math.max(0,line*parseFloat(getComputedStyle(editor).lineHeight)-editor.clientHeight/2);
                                document.getElementById('find-count').textContent='{current} / {count}';return;
                            }}
                            const root=document.querySelector('.document-body');
                            const walker=document.createTreeWalker(root,NodeFilter.SHOW_TEXT);
                            const range=document.createRange();let offset=0,node,started=false;
                            while(node=walker.nextNode()) {{
                                const limit=offset+node.length;
                                if(!started && {start}<=limit) {{range.setStart(node,{start}-offset);started=true;}}
                                if(started && {end}<=limit) {{range.setEnd(node,{end}-offset);break;}}
                                offset=limit;
                            }}
                            const selection=window.getSelection();selection.removeAllRanges();selection.addRange(range);
                            range.startContainer.parentElement.scrollIntoView({{block:'center'}});
                            document.getElementById('find-count').textContent='{current} / {count}';
                        }})()"#));
                    });
                }
                return;
            }
            Action::Fullscreen => {
                let entering = self.window.fullscreen().is_none();
                if !entering {
                    self.window.set_fullscreen(None);
                    self.window
                        .set_maximized(self.pre_fullscreen_maximized.get());
                } else {
                    self.pre_fullscreen_maximized
                        .set(self.window.is_maximized());
                    self.window
                        .set_fullscreen(Some(winit::window::Fullscreen::Borderless(None)));
                }
                #[cfg(target_os = "windows")]
                windows_chrome::refresh_corners(&self.window);
                if let Some(view) = self.view.borrow().last() {
                    view.evaluate_javascript(format!(
                        "document.querySelector('.app')?.classList.toggle('wm-fullscreen',{entering});"
                    ), |_| {});
                    self.servo.spin_event_loop();
                }
                return;
            }
            Action::ZoomIn | Action::ZoomOut | Action::ZoomReset => {
                let zoom = {
                    let mut document = self.document.borrow_mut();
                    match action {
                        Action::ZoomIn => document.change_zoom(10),
                        Action::ZoomOut => document.change_zoom(-10),
                        _ => document.zoom = 100,
                    }
                    document.zoom
                };
                if let Some(view) = self.view.borrow().last() {
                    // Host-owned numeric values only; never interpolate document source.
                    // Updating the retained DOM avoids reparse, image reload and selection loss.
                    view.evaluate_javascript(format!(r#"(() => {{
                        const viewport = document.querySelector('.document-viewport');
                        const ratio = {zoom} / Number(document.querySelector('.zoom-value').textContent.replace('%',''));
                        const oldTop = viewport.scrollTop;
                        document.querySelector('.document-body').style.setProperty('--doc-size', '{size}px');
                        const inner = document.querySelector('.document-inner');
                        inner.style.setProperty('--qt-padding', '{padding}px');
                        inner.style.setProperty('--qt-top', '{top}px');
                        inner.style.setProperty('--qt-bottom', '{bottom}px');
                        document.querySelector('.zoom-value').textContent = '{zoom}%';
                        viewport.scrollTop = oldTop * ratio;
                    }})()"#, size = f64::from(zoom) * 0.17, padding = f64::from(zoom) * 0.48,
                        top = f64::from(zoom) * 0.32, bottom = f64::from(zoom) * 0.34),
                        |result| { if let Err(error) = result { eprintln!("Zoom update failed: {error:?}"); } });
                    self.servo.spin_event_loop();
                }
                return;
            }
            Action::WindowMinimize if self.frameless => {
                self.window.set_minimized(true);
                return;
            }
            Action::WindowMaximize if self.frameless => {
                self.window.set_maximized(!self.window.is_maximized());
                #[cfg(target_os = "windows")]
                windows_chrome::refresh_corners(&self.window);
                return;
            }
            Action::WindowClose => {
                if !self.document.borrow_mut().confirm_exit() {
                    return;
                }
                self.close_requested.set(true);
                self.window.request_redraw();
                return;
            }
            Action::WindowMinimize | Action::WindowMaximize => return,
            _ => {}
        }
        self.smooth_wheel.borrow_mut().cancel();
        if self.middle_scroll.borrow().active() {
            self.middle_scroll.borrow_mut().cancel();
            if let Some(view) = self.view.borrow().last() {
                view.evaluate_javascript("document.getElementById('wolfmark-autoscroll')?.remove();", |_| {});
            }
        }
        match action {
            Action::OpenDropped => {
                if let Some(path) = self.dropped_file.borrow_mut().take() {
                    self.document.borrow_mut().open(&path);
                }
            }
            Action::Open => {
                // Local native OS dialog, not a browser/web file chooser.
                if let Some(path) = rfd::FileDialog::new()
                    .add_filter("Markdown and text", &["md", "markdown", "txt"])
                    .pick_file()
                {
                    self.document.borrow_mut().open(&path);
                } else {
                    return;
                }
            }
            Action::Save(save_as) => {
                self.document.borrow_mut().save(save_as);
            }
            Action::Undo(redo) => self.document.borrow_mut().undo(redo),
            Action::Refresh => self.document.borrow_mut().reload(),
            Action::Reader => self.document.borrow_mut().mode = ui::Mode::Reader,
            Action::Live => self.document.borrow_mut().mode = ui::Mode::Live,
            Action::Source => self.document.borrow_mut().mode = ui::Mode::Source,


            Action::SelectDocument(index) => self.document.borrow_mut().select(index),
            Action::SelectBundled => self.document.borrow_mut().select_bundled(),
            Action::CloseDocument(index) => self.document.borrow_mut().close(index),
            Action::CloseActive => self.document.borrow_mut().close_active(),
            Action::HistoryBack => self.document.borrow_mut().back(),
            Action::HistoryForward => self.document.borrow_mut().forward(),
            Action::AdjacentDocument(backward) => {
                self.document.borrow_mut().select_adjacent(backward)
            }
            Action::WindowMinimize
            | Action::WindowMaximize
            | Action::WindowClose
            | Action::ZoomIn
            | Action::ZoomOut
            | Action::ZoomReset
            | Action::Fullscreen
            | Action::CopyCode(_)
            | Action::FindOpen
            | Action::FindClose
            | Action::FindNext(_)
            | Action::SettingsOpen
            | Action::SettingsClose
            | Action::SettingsSave
            | Action::SettingsPage(_)
            | Action::SettingsToggle(_)
            | Action::CheckUpdates
            | Action::OutlineLine(_)
            | Action::SidebarToggle => unreachable!(),
        }
        let (url, title) = {
            let session = self.document.borrow();
            (session.page_url(), session.title())
        };
        self.window
            .set_title(&format!("{title} — Wolfmark (Servo experiment)"));
        if let Some(view) = self.view.borrow().last() {
            view.load(url);
        }
        self.servo.spin_event_loop();
    }

    fn request_update_check(&self) {
        if self.update_receiver.borrow().is_some() {
            return;
        }
        let include_prereleases = self.document.borrow().preferences.include_prereleases;
        self.document.borrow_mut().update_status = "Checking GitHub Releases…".into();
        if let Some(view) = self.view.borrow().last() {
            view.evaluate_javascript(
                "if(document.getElementById('update-status'))document.getElementById('update-status').textContent='Checking GitHub Releases…';",
                |_| {});
        }
        let (send, receive) = mpsc::channel();
        *self.update_receiver.borrow_mut() = Some(receive);
        let waker = self.waker.clone();
        std::thread::spawn(move || {
            let result = update_checks::check(include_prereleases);
            let _ = send.send(result);
            let _ = waker.0.send_event(Wake);
        });
    }

    fn collect_update_check(&self) {
        let finished = {
            let receiver = self.update_receiver.borrow();
            receiver.as_ref().and_then(|receiver| {
                match receiver.try_recv() {
                    Ok(result) => Some(result),
                    Err(mpsc::TryRecvError::Disconnected) =>
                        Some(Err("The update check stopped unexpectedly.".into())),
                    Err(mpsc::TryRecvError::Empty) => None,
                }
            })
        };
        if let Some(result) = finished {
            self.update_receiver.borrow_mut().take();
            let message = match result {
                Ok(report) => report.message,
                Err(error) => error,
            };
            self.document.borrow_mut().update_status = message.clone();
            if let Some(view) = self.view.borrow().last() {
                let payload = serde_json::to_string(&message).unwrap_or_default();
                view.evaluate_javascript(format!(
                    "if(document.getElementById('update-status'))document.getElementById('update-status').textContent={payload};"
                ), |_| {});
            }
        }
    }

    fn take_pending_action(self: &Rc<Self>) {
        self.collect_update_check();
        let scripts = std::mem::take(&mut *self.scripts.borrow_mut());
        for (view, script) in scripts {
            view.evaluate_javascript(script, |result| {
                if let Err(error) = result {
                    eprintln!("DOM update failed: {error:?}");
                }
            });
        }
        let synced = self.synced_action.borrow_mut().take();
        if let Some(action) = synced {
            self.apply_synced_action(action);
        }
        // Avoid invoking Servo WebView operations from inside the delegate's
        // navigation callback (reentrant callbacks can borrow the webview).
        let action = self.pending.borrow_mut().take();
        if let Some(action) = action {
            self.apply_action(action);
        }
    }
}

#[cfg(test)]
mod host_actions_tests {
    use super::Action;
    use url::Url;

    #[test]
    fn only_known_ui_commands_dispatch() {
        let good = Url::parse("wolfmark://mode/source").unwrap();
        assert_eq!(Action::from_url(&Url::parse("wolfmark://mode/live").unwrap()), Some(Action::Live));
        assert_eq!(Action::from_url(&Url::parse("wolfmark://outline/line/13").unwrap()),Some(Action::OutlineLine(13)));
        assert_eq!(Action::from_url(&Url::parse("wolfmark://updates/check").unwrap()),Some(Action::CheckUpdates));
        assert_eq!(Action::from_url(&Url::parse("wolfmark://sidebar/toggle").unwrap()), Some(Action::SidebarToggle));
        assert_eq!(Action::from_url(&good), Some(Action::Source));
        assert_eq!(
            Action::from_url(&Url::parse("wolfmark://document/3").unwrap()),
            Some(Action::SelectDocument(3))
        );
        assert_eq!(
            Action::from_url(&Url::parse("wolfmark://document/bundled").unwrap()),
            Some(Action::SelectBundled)
        );
        assert_eq!(
            Action::from_url(&Url::parse("wolfmark://close/2").unwrap()),
            Some(Action::CloseDocument(2))
        );
        assert_eq!(
            Action::from_url(&Url::parse("wolfmark://history/back").unwrap()),
            Some(Action::HistoryBack)
        );
        assert_eq!(
            Action::from_url(&Url::parse("wolfmark://history/forward").unwrap()),
            Some(Action::HistoryForward)
        );
        assert_eq!(
            Action::from_url(&Url::parse("wolfmark://window/minimize").unwrap()),
            Some(Action::WindowMinimize)
        );
        assert_eq!(
            Action::from_url(&Url::parse("wolfmark://window/maximize").unwrap()),
            Some(Action::WindowMaximize)
        );
        assert_eq!(
            Action::from_url(&Url::parse("wolfmark://window/close").unwrap()),
            Some(Action::WindowClose)
        );
        assert_eq!(
            Action::from_url(&Url::parse("wolfmark://settings/open").unwrap()),
            Some(Action::SettingsOpen)
        );
        assert_eq!(
            Action::from_url(&Url::parse("wolfmark://settings/word-wrap").unwrap()),
            Some(Action::SettingsToggle("word-wrap"))
        );
        for bad in [
            "https://example.com/",
            "wolfmark://evil/",
            "wolfmark://mode/remove",
            "file:///C:/windows",
        ] {
            assert_eq!(Action::from_url(&Url::parse(bad).unwrap()), None);
        }
    }
}

// Winit delegates actual resize/move operations to the window manager.
// This is a deliberately *opt-in* native-frame parity experiment: Windows
// DWM Snap Layout hover, rounded corners, mixed-DPI and system menus remain
// unverified, so normal Servo launches retain their working OS decorations.
fn resize_at(x: f64, y: f64, width: f64, height: f64) -> Option<ResizeDirection> {
    let left = x < 7.0;
    let right = x >= width - 7.0;
    let top = y < 7.0;
    let bottom = y >= height - 7.0;
    match (top, bottom, left, right) {
        (true, _, true, _) => Some(ResizeDirection::NorthWest),
        (true, _, _, true) => Some(ResizeDirection::NorthEast),
        (_, true, true, _) => Some(ResizeDirection::SouthWest),
        (_, true, _, true) => Some(ResizeDirection::SouthEast),
        (true, _, _, _) => Some(ResizeDirection::North),
        (_, true, _, _) => Some(ResizeDirection::South),
        (_, _, true, _) => Some(ResizeDirection::West),
        (_, _, _, true) => Some(ResizeDirection::East),
        _ => None,
    }
}

fn title_drag_area(x: f64, y: f64, width: f64) -> bool {
    // Avoid brand/breadcrumb on the left and settings/caption controls on
    // the right; only the otherwise-empty header region starts system move.
    y >= 8.0 && y < 48.0 && x > width.min(330.0) * 0.85 && x < width - 425.0
}

#[cfg(test)]
mod window_hit_tests {
    use super::*;
    #[test]
    fn resize_edges_are_distinct_from_caption_drag() {
        assert_eq!(
            resize_at(1.0, 1.0, 1100.0, 760.0),
            Some(ResizeDirection::NorthWest)
        );
        assert_eq!(
            resize_at(1098.0, 759.0, 1100.0, 760.0),
            Some(ResizeDirection::SouthEast)
        );
        assert!(resize_at(550.0, 250.0, 1100.0, 760.0).is_none());
        assert!(title_drag_area(450.0, 25.0, 1100.0));
        assert!(!title_drag_area(1070.0, 25.0, 1100.0));
        assert!(!title_drag_area(450.0, 80.0, 1100.0));
    }
}

impl ApplicationHandler<Wake> for App {
    fn about_to_wait(&mut self, event_loop: &ActiveEventLoop) {
        if let App::Running(state) = self {
            if state.close_requested.get() {
                event_loop.exit();
                return;
            }
            let image_pending = state
                .document
                .borrow()
                .all_resources()
                .iter()
                .fold(false, |pending, resources| resources.poll() || pending);
            let mut due = state.smooth_wheel.borrow().next_tick();
            if let Some(middle_due) = state.middle_scroll.borrow().next_tick() {
                due = Some(due.map_or(middle_due, |due| due.min(middle_due)));
            }
            if image_pending {
                let image_tick = Instant::now() + std::time::Duration::from_millis(16);
                due = Some(due.map_or(image_tick, |due| due.min(image_tick)));
            }
            match due {
                Some(due) if due <= Instant::now() => {
                    state.window.request_redraw();
                    event_loop.set_control_flow(ControlFlow::Wait);
                }
                Some(due) => event_loop.set_control_flow(ControlFlow::WaitUntil(due)),
                None => event_loop.set_control_flow(ControlFlow::Wait),
            }
        }
    }

    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if let App::Initial(waker, session, smoke_page) = self {
            let frameless = session.frameless_test && !*smoke_page;
            let display = event_loop.display_handle().expect("display handle");
            let caption = if *smoke_page {
                "Wolfmark — Servo graphics diagnostic".to_owned()
            } else {
                format!("{} — Wolfmark (Servo experiment)", session.title())
            };
            let window = event_loop
                .create_window(
                    Window::default_attributes()
                        .with_title(caption)
                        .with_decorations(!frameless)
                        .with_inner_size(
                            if std::env::var_os("WOLFMARK_SERVO_SNAPSHOT").is_some() {
                                winit::dpi::LogicalSize::new(
                                    std::env::var("WOLFMARK_SNAPSHOT_WIDTH")
                                        .ok()
                                        .and_then(|s| s.parse::<f64>().ok())
                                        .filter(|v| v.is_finite() && *v >= 400.0 && *v <= 4000.0)
                                        .unwrap_or(1648.0),
                                    std::env::var("WOLFMARK_SNAPSHOT_HEIGHT")
                                        .ok()
                                        .and_then(|s| s.parse::<f64>().ok())
                                        .filter(|v| v.is_finite() && *v >= 300.0 && *v <= 2500.0)
                                        .unwrap_or(928.0),
                                )
                            } else {
                                winit::dpi::LogicalSize::new(1280.0, 820.0)
                            },
                        ),
                )
                .expect("test window");
            #[cfg(target_os = "windows")]
            if frameless {
                windows_chrome::install(&window);
            }
            let handle = window.window_handle().expect("window handle");
            eprintln!(
                "Initializing Servo OpenGL window context ({}x{}, scale={:.2})",
                window.inner_size().width,
                window.inner_size().height,
                window.scale_factor()
            );
            let context = Rc::new(
                WindowRenderingContext::new(display, handle, window.inner_size())
                    .expect("Servo window rendering context"),
            );
            context
                .make_current()
                .expect("Servo created the OpenGL context but could not make it current");
            eprintln!("Servo OpenGL window context is current.");

            let servo = ServoBuilder::default()
                .event_loop_waker(Box::new(waker.clone()))
                .build();
            servo.set_delegate(Rc::new(OfflineEngine));
            servo.setup_logging();

            let state = Rc::new(State {
                window,
                servo,
                context,
                view: RefCell::new(Vec::new()),
                cursor: RefCell::new(DevicePoint::new(0.0, 0.0)),
                modifiers: RefCell::new(ServoModifiers::empty()),
                document: RefCell::new(std::mem::take(session)),
                pending: RefCell::new(None),
                smooth_wheel: RefCell::new(smooth_scroll::SmoothWheel::default()),
                middle_scroll: RefCell::new(middle_scroll::MiddleScroll::default()),
                scroll_origin: RefCell::new(DevicePoint::new(0.0, 0.0)),
                reduced_motion: std::env::var("WOLFMARK_REDUCED_MOTION").is_ok_and(|v| v == "1"),
                frameless,
                close_requested: Rc::new(Cell::new(false)),
                snapshot_requested: Cell::new(false),
                pre_fullscreen_maximized: Cell::new(false),
                last_caption_click: Cell::new(None),
                clipboard: Rc::new(RefCell::new(None)),
                search: Rc::new(RefCell::new(search::Search::default())),
                update_receiver: RefCell::new(None),
                synced_action: RefCell::new(None),
                syncing: Cell::new(false),
                waker: waker.clone(),
                dropped_file: RefCell::new(None),
                scripts: Rc::new(RefCell::new(Vec::new())),
                gui_probe_started: Cell::new(false),
                gui_probe_done: Cell::new(false),
                smoke_page: *smoke_page,
            });

            if !state.smoke_page
                && state.document.borrow().preferences.check_on_startup
                && std::env::var_os("WOLFMARK_DISABLE_UPDATE_CHECKS").is_none()
            {
                state.request_update_check();
            }
            let page_url = if state.smoke_page {
                local_probe_url()
            } else {
                state.document.borrow().page_url()
            };
            let view = WebViewBuilder::new(&state.servo, state.context.clone())
                .url(page_url)
                .hidpi_scale_factor(Scale::new(state.window.scale_factor() as f32))
                .delegate(state.clone())
                .build();
            state.view.borrow_mut().push(view);
            *self = App::Running(state);
        }
    }

    fn user_event(&mut self, _event_loop: &ActiveEventLoop, _event: Wake) {
        if let App::Running(state) = self {
            state.servo.spin_event_loop();
            state.take_pending_action();
        }
    }

    fn window_event(
        &mut self,
        event_loop: &ActiveEventLoop,
        _window_id: winit::window::WindowId,
        event: WindowEvent,
    ) {
        if let App::Running(state) = self {
            state.servo.spin_event_loop();
        }

        match event {
            WindowEvent::CloseRequested => {
                if let App::Running(state) = self {
                    let action = if state.document.borrow().settings_open {
                        Action::SettingsClose
                    } else {
                        Action::WindowClose
                    };
                    state.apply_action(action);
                } else {
                    event_loop.exit();
                }
            }
            WindowEvent::DroppedFile(path) => {
                if let App::Running(state) = self {
                    *state.dropped_file.borrow_mut() = Some(path);
                    state.apply_action(Action::OpenDropped);
                }
            }
            WindowEvent::RedrawRequested => {
                if let App::Running(state) = self {
                    state.take_pending_action();
                    if let Some((x, y)) = state.smooth_wheel.borrow_mut().tick(Instant::now()) {
                        if (x.abs() > f64::EPSILON || y.abs() > f64::EPSILON)
                            && let Some(view) = state.view.borrow().last()
                        {
                            view.notify_input_event(InputEvent::Wheel(WheelEvent::new(
                                WheelDelta {
                                    x,
                                    y,
                                    z: 0.0,
                                    mode: WheelMode::DeltaPixel,
                                },
                                (*state.scroll_origin.borrow()).into(),
                            )));
                            state.servo.spin_event_loop();
                        }
                    }
                    let middle_motion = { state.middle_scroll.borrow_mut().tick(Instant::now()) };
                    if let Some((x, y)) = middle_motion {
                        if (x.abs() > 0.01 || y.abs() > 0.01)
                            && let Some(view) = state.view.borrow().last()
                        {
                            let (cx, cy) = state.middle_scroll.borrow().origin();
                            view.notify_input_event(InputEvent::Wheel(WheelEvent::new(
                                WheelDelta { x, y, z: 0.0, mode: WheelMode::DeltaPixel },
                                DevicePoint::new(cx as f32, cy as f32).into(),
                            )));
                            state.servo.spin_event_loop();
                        }
                    }
                    if let Some(view) = state.view.borrow().last() {
                        view.paint();
                        state.context.present();
                    }
                }
            }
            WindowEvent::Resized(size) => {
                if let App::Running(state) = self {
                    if let Some(view) = state.view.borrow().last() {
                        view.resize(size);
                    }
                }
            }
            WindowEvent::ScaleFactorChanged { scale_factor, .. } => {
                if let App::Running(state) = self {
                    if let Some(view) = state.view.borrow().last() {
                        view.set_hidpi_scale_factor(Scale::new(scale_factor as f32));
                    }
                }
            }
            WindowEvent::ModifiersChanged(modifiers) => {
                if let App::Running(state) = self {
                    let current = modifiers.state();
                    let mut flags = ServoModifiers::empty();
                    flags.set(ServoModifiers::SHIFT, current.shift_key());
                    flags.set(ServoModifiers::CONTROL, current.control_key());
                    flags.set(ServoModifiers::ALT, current.alt_key());
                    flags.set(ServoModifiers::META, current.super_key());
                    *state.modifiers.borrow_mut() = flags;
                }
            }
            WindowEvent::KeyboardInput { event, .. } => {
                if let App::Running(state) = self {
                    let modifiers = *state.modifiers.borrow();
                    if event.state == ElementState::Pressed
                        && event.logical_key == WinitKey::Named(WinitNamedKey::Escape)
                        && state.middle_scroll.borrow().active()
                    {
                        state.middle_scroll.borrow_mut().cancel();
                        if let Some(view) = state.view.borrow().last() {
                            view.evaluate_javascript("document.getElementById('wolfmark-autoscroll')?.remove();", |_| {});
                        }
                        return;
                    }
                    if !state.document.borrow().settings_open
                        && event.state == ElementState::Pressed
                        && event.logical_key == WinitKey::Named(WinitNamedKey::Escape)
                        && state.window.fullscreen().is_some()
                    {
                        state.apply_action(Action::Fullscreen);
                        return;
                    }
                    if state.document.borrow().settings_open {
                        if event.state == ElementState::Pressed
                            && event.logical_key == WinitKey::Named(WinitNamedKey::Escape)
                        {
                            state.apply_action(Action::SettingsClose);
                            return;
                        }
                    } else if !state.smoke_page {
                        if let Some(action) = application_shortcut(&event, modifiers) {
                            state.apply_action(action);
                            return;
                        }
                    }
                    if let Some(input) = keyboard_input(&event, modifiers) {
                        if let Some(view) = state.view.borrow().last() {
                            view.notify_input_event(input);
                            state.servo.spin_event_loop();
                        }
                    }
                }
            }
            WindowEvent::CursorMoved { position, .. } => {
                if let App::Running(state) = self {
                    let point = DevicePoint::new(position.x as f32, position.y as f32);
                    *state.cursor.borrow_mut() = point;
                    state.middle_scroll.borrow_mut().update_cursor((position.x, position.y));
                    if let Some(view) = state.view.borrow().last() {
                        view.notify_input_event(InputEvent::MouseMove(MouseMoveEvent::new(
                            point.into(),
                        )));
                        state.servo.spin_event_loop();
                    }
                }
            }
            WindowEvent::MouseInput {
                state: pressed,
                button,
                ..
            } => {
                if let App::Running(state) = self {
                    if pressed == ElementState::Released && button == WinitMouseButton::Middle
                        && state.middle_scroll.borrow().active() {
                        return;
                    }
                    if pressed == ElementState::Pressed && button == WinitMouseButton::Middle {
                        if !state.document.borrow().settings_open
                            && state.document.borrow().mode == ui::Mode::Reader
                        {
                            let cursor = *state.cursor.borrow();
                            let scale = state.window.scale_factor();
                            let x = f64::from(cursor.x) / scale;
                            let y = f64::from(cursor.y) / scale;
                            let sidebar = if state.document.borrow().sidebar_collapsed { 0.0 } else { 304.0 };
                            if x > sidebar && y >= 96.0 {
                                let active = state.middle_scroll.borrow_mut().toggle(
                                    (f64::from(cursor.x), f64::from(cursor.y)), Instant::now());
                                if active {
                                    state.smooth_wheel.borrow_mut().cancel();
                                }
                                if let Some(view) = state.view.borrow().last() {
                                    let script = if active {
                                        format!(r#"(() => {{
                                            document.getElementById('wolfmark-autoscroll')?.remove();
                                            document.body.insertAdjacentHTML('beforeend',
                                              '<span id="wolfmark-autoscroll" aria-hidden="true" style="left:{x}px;top:{y}px"><svg viewBox="0 0 32 32" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="16" cy="16" r="11"/><path d="m11 10 5-5 5 5m-10 12 5 5 5-5"/></svg></span>');
                                        }})()"#)
                                    } else {
                                        "document.getElementById('wolfmark-autoscroll')?.remove();".to_owned()
                                    };
                                    view.evaluate_javascript(script, |_| {});
                                }
                                state.window.request_redraw();
                                return;
                            }
                        }
                    }
                    if pressed == ElementState::Pressed
                        && state.middle_scroll.borrow().active()
                    {
                        state.middle_scroll.borrow_mut().cancel();
                        if let Some(view) = state.view.borrow().last() {
                            view.evaluate_javascript("document.getElementById('wolfmark-autoscroll')?.remove();", |_| {});
                        }
                        state.window.request_redraw();
                        return;
                    }
                    if state.frameless
                        && pressed == ElementState::Pressed
                        && button == WinitMouseButton::Left
                    {
                        let scale = state.window.scale_factor();
                        let cursor = state.cursor.borrow();
                        let x = f64::from(cursor.x) / scale;
                        let y = f64::from(cursor.y) / scale;
                        let size = state.window.inner_size();
                        let width = f64::from(size.width) / scale;
                        let height = f64::from(size.height) / scale;
                        if !state.window.is_maximized() {
                            if let Some(edge) = resize_at(x, y, width, height) {
                                let _ = state.window.drag_resize_window(edge);
                                return;
                            }
                        }
                        if title_drag_area(x, y, width) {
                            let now = Instant::now();
                            let double_click = state.last_caption_click.get().is_some_and(
                                |(when, old_x, old_y)| {
                                    now.duration_since(when).as_millis() <= 400
                                        && (x - old_x).abs() <= 6.0
                                        && (y - old_y).abs() <= 6.0
                                },
                            );
                            if double_click {
                                state.last_caption_click.set(None);
                                state.window.set_maximized(!state.window.is_maximized());
                                return;
                            }
                            state.last_caption_click.set(Some((now, x, y)));
                            let _ = state.window.drag_window();
                            return;
                        }
                    }
                    let button = match button {
                        WinitMouseButton::Left => ServoMouseButton::Primary,
                        WinitMouseButton::Right => ServoMouseButton::Secondary,
                        WinitMouseButton::Middle => ServoMouseButton::Auxiliary,
                        WinitMouseButton::Back => ServoMouseButton::Back,
                        WinitMouseButton::Forward => ServoMouseButton::Forward,
                        WinitMouseButton::Other(number) => ServoMouseButton::Other(number),
                    };
                    let action = match pressed {
                        ElementState::Pressed => MouseButtonAction::Down,
                        ElementState::Released => MouseButtonAction::Up,
                    };
                    if let Some(view) = state.view.borrow().last() {
                        view.notify_input_event(InputEvent::MouseButton(MouseButtonEvent::new(
                            action,
                            button,
                            (*state.cursor.borrow()).into(),
                        )));
                        state.servo.spin_event_loop();
                    }
                }
            }
            WindowEvent::MouseWheel { delta, .. } => {
                if let App::Running(state) = self {
                    match delta {
                        MouseScrollDelta::LineDelta(x, y) if !state.reduced_motion => {
                            // Keep the wheel's target under the pointer position
                            // at input time; later mouse movement must not move
                            // in-flight scroll deltas into a different panel.
                            *state.scroll_origin.borrow_mut() = *state.cursor.borrow();
                            // Match Qt's continuous wheel trajectory. Pixel/trackpad
                            // events stay direct and never enter the integrator.
                            state
                                .smooth_wheel
                                .borrow_mut()
                                .add_lines(x, y, Instant::now());
                            state.window.request_redraw();
                        }
                        delta => {
                            state.smooth_wheel.borrow_mut().cancel();
                            let (x, y) = match delta {
                                MouseScrollDelta::LineDelta(x, y) => {
                                    (f64::from(x) * 48.0, f64::from(y) * 48.0)
                                }
                                MouseScrollDelta::PixelDelta(pixel) => (pixel.x, pixel.y),
                            };
                            if let Some(view) = state.view.borrow().last() {
                                view.notify_input_event(InputEvent::Wheel(WheelEvent::new(
                                    WheelDelta {
                                        x,
                                        y,
                                        z: 0.0,
                                        mode: WheelMode::DeltaPixel,
                                    },
                                    (*state.cursor.borrow()).into(),
                                )));
                                state.servo.spin_event_loop();
                            }
                        }
                    }
                }
            }
            _ => {}
        }
    }
}

// Command shortcuts always terminate in Rust and never become arbitrary
// browser key combinations. Normal text keys still go to Servo below.
fn application_shortcut(event: &KeyEvent, modifiers: ServoModifiers) -> Option<Action> {
    if event.state != ElementState::Pressed || event.repeat {
        return None;
    }
    let ctrl = modifiers.contains(ServoModifiers::CONTROL);
    let alt = modifiers.contains(ServoModifiers::ALT);
    let shift = modifiers.contains(ServoModifiers::SHIFT);
    match event.physical_key {
        PhysicalKey::Code(KeyCode::KeyS) if ctrl && !alt => Some(Action::Save(shift)),
        PhysicalKey::Code(KeyCode::KeyZ) if ctrl && !alt => Some(Action::Undo(shift)),
        PhysicalKey::Code(KeyCode::KeyY) if ctrl && !alt => Some(Action::Undo(true)),
        PhysicalKey::Code(KeyCode::KeyF) if ctrl && !alt => Some(Action::FindOpen),
        PhysicalKey::Code(KeyCode::F3) if !ctrl && !alt => Some(Action::FindNext(shift)),
        PhysicalKey::Code(KeyCode::Escape) if !ctrl && !alt => Some(Action::FindClose),
        PhysicalKey::Code(KeyCode::F11) if !ctrl && !alt => Some(Action::Fullscreen),
        PhysicalKey::Code(KeyCode::Equal) if ctrl && !alt => Some(Action::ZoomIn),
        PhysicalKey::Code(KeyCode::Minus) if ctrl && !alt => Some(Action::ZoomOut),
        PhysicalKey::Code(KeyCode::Digit0) if ctrl && !alt => Some(Action::ZoomReset),
        PhysicalKey::Code(KeyCode::KeyO) if ctrl && !alt => Some(Action::Open),
        PhysicalKey::Code(KeyCode::KeyW) if ctrl && !alt => Some(Action::CloseActive),
        PhysicalKey::Code(KeyCode::KeyR) if ctrl && !alt => Some(Action::Refresh),
        PhysicalKey::Code(KeyCode::Tab) if ctrl && !alt => Some(Action::AdjacentDocument(shift)),
        PhysicalKey::Code(KeyCode::ArrowLeft) if alt && !ctrl => Some(Action::HistoryBack),
        PhysicalKey::Code(KeyCode::ArrowRight) if alt && !ctrl => Some(Action::HistoryForward),
        _ => None,
    }
}

#[cfg(test)]
mod application_shortcut_tests {
    use super::*;
    #[test]
    fn expected_shortcuts_are_local_and_nonrepeating() {
        // Full winit event construction is platform-version-specific; parsing
        // host actions above is checked separately. This verifies the navigation
        // event transport does not permit external URLs or arbitrary commands.
        for source in [
            "wolfmark://close/../../../windows",
            "wolfmark://history/delete",
            "wolfmark://close/99999999999999999999999999",
        ] {
            assert!(Action::from_url(&Url::parse(source).unwrap()).is_none());
        }
    }
}

// This is intentionally limited to the keys needed to smoke-test selection,
// scrolling, ordinary typing and shortcuts. IME/dead keys, clipboard and
// comprehensive keyboard mappings still require separate acceptance tests.
fn keyboard_input(event: &KeyEvent, modifiers: ServoModifiers) -> Option<InputEvent> {
    let key = match &event.logical_key {
        WinitKey::Character(text) => ServoKey::Character(text.to_string()),
        WinitKey::Named(named) => {
            let value = match named {
                WinitNamedKey::Space => {
                    return Some(InputEvent::Keyboard(ServoKeyboardEvent::new_without_event(
                        key_state(event.state),
                        ServoKey::Character(" ".into()),
                        ServoCode::Space,
                        ServoLocation::Standard,
                        modifiers,
                        event.repeat,
                        false,
                    )));
                }
                WinitNamedKey::Tab => ServoNamedKey::Tab,
                WinitNamedKey::Enter => ServoNamedKey::Enter,
                WinitNamedKey::Escape => ServoNamedKey::Escape,
                WinitNamedKey::Backspace => ServoNamedKey::Backspace,
                WinitNamedKey::Delete => ServoNamedKey::Delete,
                WinitNamedKey::Home => ServoNamedKey::Home,
                WinitNamedKey::End => ServoNamedKey::End,
                WinitNamedKey::PageUp => ServoNamedKey::PageUp,
                WinitNamedKey::PageDown => ServoNamedKey::PageDown,
                WinitNamedKey::ArrowUp => ServoNamedKey::ArrowUp,
                WinitNamedKey::ArrowDown => ServoNamedKey::ArrowDown,
                WinitNamedKey::ArrowLeft => ServoNamedKey::ArrowLeft,
                WinitNamedKey::ArrowRight => ServoNamedKey::ArrowRight,
                WinitNamedKey::Shift => ServoNamedKey::Shift,
                WinitNamedKey::Control => ServoNamedKey::Control,
                WinitNamedKey::Alt => ServoNamedKey::Alt,
                WinitNamedKey::Super => ServoNamedKey::Meta,
                _ => return None,
            };
            ServoKey::Named(value)
        }
        _ => return None,
    };
    let code = match event.physical_key {
        PhysicalKey::Code(KeyCode::KeyA) => ServoCode::KeyA,
        PhysicalKey::Code(KeyCode::KeyC) => ServoCode::KeyC,
        PhysicalKey::Code(KeyCode::KeyV) => ServoCode::KeyV,
        PhysicalKey::Code(KeyCode::KeyX) => ServoCode::KeyX,
        PhysicalKey::Code(KeyCode::KeyZ) => ServoCode::KeyZ,
        PhysicalKey::Code(KeyCode::KeyF) => ServoCode::KeyF,
        PhysicalKey::Code(KeyCode::Tab) => ServoCode::Tab,
        PhysicalKey::Code(KeyCode::Enter) => ServoCode::Enter,
        PhysicalKey::Code(KeyCode::Escape) => ServoCode::Escape,
        PhysicalKey::Code(KeyCode::Backspace) => ServoCode::Backspace,
        PhysicalKey::Code(KeyCode::Delete) => ServoCode::Delete,
        PhysicalKey::Code(KeyCode::ArrowUp) => ServoCode::ArrowUp,
        PhysicalKey::Code(KeyCode::ArrowDown) => ServoCode::ArrowDown,
        PhysicalKey::Code(KeyCode::ArrowLeft) => ServoCode::ArrowLeft,
        PhysicalKey::Code(KeyCode::ArrowRight) => ServoCode::ArrowRight,
        _ => ServoCode::Unidentified,
    };
    let location = match event.location {
        KeyLocation::Left => ServoLocation::Left,
        KeyLocation::Right => ServoLocation::Right,
        KeyLocation::Numpad => ServoLocation::Numpad,
        KeyLocation::Standard => ServoLocation::Standard,
    };
    Some(InputEvent::Keyboard(ServoKeyboardEvent::new_without_event(
        key_state(event.state),
        key,
        code,
        location,
        modifiers,
        event.repeat,
        false,
    )))
}

fn key_state(state: ElementState) -> ServoKeyState {
    match state {
        ElementState::Pressed => ServoKeyState::Down,
        ElementState::Released => ServoKeyState::Up,
    }
}

#[derive(Debug)]
struct Wake;

#[derive(Clone)]
struct Waker(EventLoopProxy<Wake>);

impl embedder_traits::EventLoopWaker for Waker {
    fn clone_box(&self) -> Box<dyn embedder_traits::EventLoopWaker> {
        Box::new(self.clone())
    }

    fn wake(&self) {
        if let Err(error) = self.0.send_event(Wake) {
            warn!(?error, "Servo event loop is no longer available");
        }
    }
}

fn local_probe_url() -> Url {
    // Inline source: packaged into the Rust executable, not loaded from disk
    // or localhost. It intentionally does NOT imitate the locked Wolfmark UI.
    let mut html = String::from(
        r###"<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; img-src data:; connect-src 'none'; form-action 'none'; base-uri 'none'">
<title>Wolfmark Servo Trial</title>
<style>
* { box-sizing: border-box; }
body { background: #1b1a1d; color: #e8e2df; font: 16px/1.6 system-ui, sans-serif; margin: 0; }
header { padding: 18px 36px; background: #29262b; border-bottom: 1px solid #443b42; }
h1 { margin: 0; color: #f1e9e7; font-size: 22px; }
small { color: #aaa2a9; }
main { margin: 25px auto; max-width: 920px; padding: 0 28px 80px; }
h2 { margin-top: 32px; color: #eeb0ab; }
blockquote { border-left: 4px solid #b74a56; padding-left: 15px; margin-left: 0; color: #c8bec1; }
input, textarea { display: block; width: 100%; padding: 12px; margin: 9px 0; color: #eee; background: #302b31; border: 1px solid #71646a; border-radius: 5px; font: inherit; }
table { border-collapse: collapse; width: 100%; }
th, td { padding: 10px; border-bottom: 1px solid #504750; text-align: left; }
pre { background: #29252b; padding: 16px; overflow-x: auto; }
code { font-family: ui-monospace, Consolas, monospace; }
img { max-width: 100%; }
a { color: #e4a5a5; }
.selection-target { padding: 10px 0; }
::selection { background: #853a45; color: white; }
</style></head><body>
<header><h1>Wolfmark — Servo engine trial</h1><small>Embedded data: content • no localhost • Qt untouched</small></header>
<main><h2 id="test">Text layout and selection</h2>
<p class="selection-target">Try dragging across this paragraph and the next. This is an actual Servo-rendered
document, not Wolfmark's existing QTextDocument implementation.</p>
<p class="selection-target"><strong>Bold</strong>, <em>italic</em>, <code>inline code</code>, and <a href="#table">an internal anchor</a>.
Try scrolling, resizing, and mouse selection. Clipboard and keyboard forwarding are not yet implemented.</p>
<blockquote>A local, script-free page used for early rendering tests.</blockquote>
<h2 id="keyboard">Keyboard and selection smoke test</h2>
<p>Tab into this field, type characters, use Shift+arrows, Ctrl+A/C/V, Home/End
and PageUp/PageDown. Clipboard and IME behavior must be validated separately.</p>
<input aria-label="Keyboard smoke input" value="Select or replace this local text">
<textarea aria-label="Keyboard smoke editor" rows="3">Local-only editing surface. No document is saved.</textarea>
<p><a href="https://example.com/should-be-blocked">Attempt external navigation (must be denied)</a>.</p>
<p><img width="32" height="32" alt="Embedded local PNG" src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9VEGlRIAAAAASUVORK5CYII="></p>
<h2 id="table">Tables and code</h2>
<table><thead><tr><th>Capability</th><th>Initial target</th></tr></thead>
<tbody><tr><td>Embedded rendering</td><td>Observe on Windows</td></tr>
<tr><td>Selection</td><td>Mouse input, unverified</td></tr>
<tr><td>Offline isolation</td><td>Not fully validated</td></tr></tbody></table>
<pre><code>fn renderer() {
    // Rust remains Wolfmark's application authority.
    println!("Servo is an optional experiment");
}</code></pre>
<h2>Large scrolling sample</h2>
"###,
    );
    for index in 1..=160 {
        html.push_str(&format!(
            "<p>Paragraph {index}: a representative long-document scrolling sample with text, \
             wrapping, and selection behavior. Wolfmark's existing Qt baseline stays separate.</p>"
        ));
    }
    html.push_str("</main></body></html>");
    let encoded = utf8_percent_encode(&html, NON_ALPHANUMERIC).to_string();
    Url::parse(&format!("data:text/html;charset=utf-8,{encoded}"))
        .expect("static packaged smoke page must make a valid data URL")
}
