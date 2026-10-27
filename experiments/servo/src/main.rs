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

use std::cell::RefCell;
use std::error::Error;
use std::rc::Rc;
use std::path::PathBuf;

// Shared Rust code from the existing Wolfmark core, without linking Qt.
#[path = "../../../src/markdown/model.rs"] mod model;
#[path = "../../../src/markdown/parser.rs"] mod parser;
#[path = "../../../src/markdown/highlight.rs"] mod highlight;
#[path = "../../../src/presentation.rs"] mod presentation;
mod ui;

use euclid::Scale;
use percent_encoding::{NON_ALPHANUMERIC, utf8_percent_encode};
use servo::{
    Code as ServoCode, InputEvent, Key as ServoKey, KeyState as ServoKeyState,
    KeyboardEvent as ServoKeyboardEvent, Location as ServoLocation,
    Modifiers as ServoModifiers, MouseButton as ServoMouseButton, MouseButtonAction,
    MouseButtonEvent, MouseMoveEvent, NamedKey as ServoNamedKey, NavigationRequest,
    RenderingContext, Servo, ServoBuilder, WebView, WebViewBuilder, WheelDelta, WheelEvent,
    WheelMode, WindowRenderingContext,
};
use tracing::warn;
use url::Url;
use webrender_api::units::DevicePoint;
use winit::application::ApplicationHandler;
use winit::event::{
    ElementState, KeyEvent, MouseButton as WinitMouseButton, MouseScrollDelta, WindowEvent,
};
use winit::keyboard::{Key as WinitKey, KeyCode, KeyLocation, NamedKey as WinitNamedKey, PhysicalKey};
use winit::event_loop::{ActiveEventLoop, EventLoop, EventLoopProxy};
use winit::raw_window_handle::{HasDisplayHandle, HasWindowHandle};
use winit::window::Window;

fn main() -> Result<(), Box<dyn Error>> {
    rustls::crypto::aws_lc_rs::default_provider()
        .install_default()
        .expect("Servo TLS crypto provider could not be installed");

    // No Qt link, localhost web server, loose HTML file, or system WebView2.
    let loop_ = EventLoop::<Wake>::with_user_event().build()?;
    // The accepted UI is now the default. Keep the tiny original page as an
    // explicit graphics diagnostic, not as the Wolfmark replacement.
    let mut session = ui::Session::default();
    let mut smoke_page = false;
    let mut args = std::env::args_os().skip(1);
    while let Some(arg) = args.next() {
        if arg == "--smoke-page" {
            smoke_page = true;
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
    smoke_page: bool,
}

impl servo::WebViewDelegate for State {
    fn notify_new_frame_ready(&self, _webview: WebView) {
        self.window.request_redraw();
    }

    fn load_web_resource(&self, _webview: WebView, load: servo::WebResourceLoad) {
        // Servo 0.7 also invokes this callback for our embedded data: page.
        // Dropping an unintercepted load lets Servo process its own local data.
        // Intercept all other loads while network access remains forbidden.
        // ServoDelegate's non-webview/background requests need a separate audit.
        let url = load.request().url.clone();
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
            eprintln!("Blocked external document navigation (scheme: {})", request.url.scheme());
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

enum App {
    Initial(Waker, ui::Session, bool),
    Running(Rc<State>),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Action {
    Open, Refresh, Reader, Source, ZoomIn, ZoomOut,
}

impl Action {
    fn from_url(url: &Url) -> Option<Self> {
        if url.scheme() != "wolfmark" { return None; }
        match (url.host_str()?, url.path()) {
            ("open", "/") => Some(Self::Open),
            ("refresh", "/") => Some(Self::Refresh),
            ("mode", "/reader") => Some(Self::Reader),
            ("mode", "/source") => Some(Self::Source),
            ("zoom", "/in") => Some(Self::ZoomIn),
            ("zoom", "/out") => Some(Self::ZoomOut),
            _ => None,
        }
    }
}

impl State {
    fn apply_action(&self, action: Action) {
        if self.smoke_page { return; }
        match action {
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
            Action::Refresh => self.document.borrow_mut().reload(),
            Action::Reader => self.document.borrow_mut().mode = ui::Mode::Reader,
            Action::Source => self.document.borrow_mut().mode = ui::Mode::Source,
            Action::ZoomIn => self.document.borrow_mut().change_zoom(10),
            Action::ZoomOut => self.document.borrow_mut().change_zoom(-10),
        }
        let url = self.document.borrow().page_url();
        if let Some(view) = self.view.borrow().last() {
            view.load(url);
        }
        self.servo.spin_event_loop();
    }

    fn take_pending_action(&self) {
        // Avoid invoking Servo WebView operations from inside the delegate's
        // navigation callback (reentrant callbacks can borrow the webview).
        let action = self.pending.borrow_mut().take();
        if let Some(action) = action { self.apply_action(action); }
    }
}

#[cfg(test)]
mod host_actions_tests {
    use super::Action;
    use url::Url;

    #[test]
    fn only_known_ui_commands_dispatch() {
        let good = Url::parse("wolfmark://mode/source").unwrap();
        assert_eq!(Action::from_url(&good), Some(Action::Source));
        for bad in ["https://example.com/", "wolfmark://evil/", "wolfmark://mode/remove", "file:///C:/windows"] {
            assert_eq!(Action::from_url(&Url::parse(bad).unwrap()), None);
        }
    }
}

impl ApplicationHandler<Wake> for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if let App::Initial(waker, session, smoke_page) = self {
            let display = event_loop.display_handle().expect("display handle");
            let window = event_loop
                .create_window(
                    Window::default_attributes()
                        .with_title("Wolfmark / Servo embedding experiment")
                        .with_inner_size(winit::dpi::LogicalSize::new(1100.0, 760.0)),
                )
                .expect("test window");
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
            context.make_current().expect(
                "Servo created the OpenGL context but could not make it current"
            );
            eprintln!("Servo OpenGL window context is current.");

            let servo = ServoBuilder::default()
                .event_loop_waker(Box::new(waker.clone()))
                .build();
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
                smoke_page: *smoke_page,
            });

            let page_url = if state.smoke_page { local_probe_url() } else { state.document.borrow().page_url() };
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
            WindowEvent::CloseRequested => event_loop.exit(),
            WindowEvent::DroppedFile(path) => {
                if let App::Running(state) = self {
                    if !state.smoke_page {
                        state.document.borrow_mut().open(&path);
                        if let Some(view) = state.view.borrow().last() {
                            view.load(state.document.borrow().page_url());
                        }
                        state.servo.spin_event_loop();
                    }
                }
            }
            WindowEvent::RedrawRequested => {
                if let App::Running(state) = self {
                    state.take_pending_action();
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
                    if let Some(input) = keyboard_input(&event, *state.modifiers.borrow()) {
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
                    if let Some(view) = state.view.borrow().last() {
                        view.notify_input_event(InputEvent::MouseMove(
                            MouseMoveEvent::new(point.into()),
                        ));
                        state.servo.spin_event_loop();
                    }
                }
            }
            WindowEvent::MouseInput { state: pressed, button, .. } => {
                if let App::Running(state) = self {
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
                            action, button, (*state.cursor.borrow()).into(),
                        )));
                        state.servo.spin_event_loop();
                    }
                }
            }
            WindowEvent::MouseWheel { delta, .. } => {
                if let App::Running(state) = self {
                    let (x, y, mode) = match delta {
                        MouseScrollDelta::LineDelta(x, y) =>
                            ((x * 76.0) as f64, (y * 76.0) as f64, WheelMode::DeltaLine),
                        MouseScrollDelta::PixelDelta(p) =>
                            (p.x, p.y, WheelMode::DeltaPixel),
                    };
                    if let Some(view) = state.view.borrow().last() {
                        view.notify_input_event(InputEvent::Wheel(WheelEvent::new(
                            WheelDelta { x, y, z: 0.0, mode },
                            (*state.cursor.borrow()).into(),
                        )));
                        state.servo.spin_event_loop();
                    }
                }
            }
            _ => {}
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
                WinitNamedKey::Space => return Some(InputEvent::Keyboard(
                    ServoKeyboardEvent::new_without_event(
                        key_state(event.state),
                        ServoKey::Character(" ".into()),
                        ServoCode::Space,
                        ServoLocation::Standard,
                        modifiers,
                        event.repeat,
                        false,
                    ),
                )),
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
    let mut html = String::from(r###"<!doctype html>
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
"###);
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
