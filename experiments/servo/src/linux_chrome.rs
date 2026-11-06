// SPDX-License-Identifier: GPL-3.0-only
//! Linux/X11 fallback for the custom Servo frame's resize grips.
//! winit 0.30.13 supports drag_window() on X11, but drag_resize_window()
//! reports NotSupported. EWMH _NET_WM_MOVERESIZE delegates resizing to the
//! existing window manager. This module is pure Rust (x11rb), no C++/Qt.
#![cfg(target_os = "linux")]

use winit::raw_window_handle::{HasWindowHandle, RawWindowHandle};
use winit::window::{ResizeDirection, Window};
use x11rb::connection::Connection;
use x11rb::protocol::xproto::{ClientMessageEvent, ConnectionExt, EventMask};

/// EWMH _NET_WM_MOVERESIZE directions, not winit cursor or hit-test values.
fn direction_code(direction: ResizeDirection) -> u32 {
    use ResizeDirection::*;
    match direction {
        NorthWest => 0,
        North => 1,
        NorthEast => 2,
        East => 3,
        SouthEast => 4,
        South => 5,
        SouthWest => 6,
        West => 7,
    }
}

fn x11_window(window: &Window) -> Option<u32> {
    match window.window_handle().ok()?.as_raw() {
        RawWindowHandle::Xlib(handle) => u32::try_from(handle.window).ok(),
        RawWindowHandle::Xcb(handle) => Some(handle.window.get()),
        // Wayland resize is already supported directly by winit.
        _ => None,
    }
}

/// Ask an EWMH-compliant X11 window manager to start resize from the current
/// pointer position. Returns an error for unsupported Wayland/non-EWMH cases,
/// leaving the existing native-frame fallback available.
pub fn begin_resize(window: &Window, direction: ResizeDirection) -> Result<(), String> {
    let id = x11_window(window).ok_or("The window is not hosted by X11")?;
    let (conn, screen) = x11rb::connect(None)
        .map_err(|error| format!("Cannot connect to the X11 window manager: {error}"))?;
    let root = conn.setup().roots.get(screen)
        .ok_or("No X11 root window available")?.root;
    let atom = conn.intern_atom(false, b"_NET_WM_MOVERESIZE")
        .map_err(|error| error.to_string())?
        .reply()
        .map_err(|error| error.to_string())?
        .atom;
    let pointer = conn.query_pointer(id)
        .map_err(|error| error.to_string())?
        .reply()
        .map_err(|error| error.to_string())?;
    // Use root coordinates from X11, avoiding the DPI/screen-origin mistakes
    // common with mixed-scale and negative-origin monitor configurations.
    let coordinates = [
        pointer.root_x as i32 as u32,
        pointer.root_y as i32 as u32,
        direction_code(direction),
        1, // left mouse button
        1, // source indication: normal application
    ];
    let event = ClientMessageEvent::new(32, id, atom, coordinates);
    conn.send_event(
        false,
        root,
        EventMask::SUBSTRUCTURE_REDIRECT | EventMask::SUBSTRUCTURE_NOTIFY,
        event,
    ).map_err(|error| error.to_string())?;
    conn.flush().map_err(|error| error.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn ewmh_resize_directions_match_spec() {
        assert_eq!(direction_code(ResizeDirection::NorthWest), 0);
        assert_eq!(direction_code(ResizeDirection::North), 1);
        assert_eq!(direction_code(ResizeDirection::NorthEast), 2);
        assert_eq!(direction_code(ResizeDirection::East), 3);
        assert_eq!(direction_code(ResizeDirection::SouthEast), 4);
        assert_eq!(direction_code(ResizeDirection::South), 5);
        assert_eq!(direction_code(ResizeDirection::SouthWest), 6);
        assert_eq!(direction_code(ResizeDirection::West), 7);
    }
}
