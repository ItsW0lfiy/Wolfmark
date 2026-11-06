// SPDX-License-Identifier: GPL-3.0-only
//! Win32/DWM parity for Wolfmark's Servo-owned, frameless winit HWND.
//! Native caption hit roles are necessary for Windows 11 Snap Layout hover.
//! All app code is Rust; no project C++ bridge or WebView2.
#![cfg(target_os = "windows")]

use winit::raw_window_handle::{HasWindowHandle, RawWindowHandle};
use winit::window::Window;
use windows_sys::Win32::Foundation::{HWND, POINT, RECT};
use windows_sys::Win32::Graphics::Dwm::{DwmDefWindowProc, DwmSetWindowAttribute};
use windows_sys::Win32::UI::Shell::{DefSubclassProc, RemoveWindowSubclass, SetWindowSubclass};
use windows_sys::Win32::UI::HiDpi::GetDpiForWindow;
use windows_sys::Win32::UI::WindowsAndMessaging::{
    GWL_STYLE, GetClientRect, GetWindowLongPtrW, HTBOTTOM, HTBOTTOMLEFT, HTBOTTOMRIGHT,
    HTCAPTION, HTCLIENT, HTLEFT, HTMAXBUTTON, HTRIGHT, HTTOP, HTTOPLEFT, HTTOPRIGHT,
    IsZoomed, SW_MAXIMIZE, SW_RESTORE, SWP_FRAMECHANGED, SWP_NOACTIVATE, SWP_NOMOVE,
    SWP_NOOWNERZORDER, SWP_NOSIZE, SWP_NOZORDER,
    SetWindowLongPtrW, SetWindowPos, ShowWindow, WM_NCHITTEST, WM_NCLBUTTONDOWN,
    WM_NCLBUTTONUP, WM_NCDESTROY, WS_MAXIMIZEBOX, WS_MINIMIZEBOX, WS_SYSMENU,
    WS_THICKFRAME,
};

// Windows-sys 0.61 exposes ScreenToClient inconsistently across features;
// import the documented Win32 ABI directly, without a C++ wrapper.
#[link(name = "user32")]
unsafe extern "system" {
    fn ScreenToClient(hwnd: HWND, point: *mut POINT) -> i32;
}

const FRAME_ID: usize = 0x574f_4c46;

fn hwnd(window: &Window) -> Option<HWND> {
    match window.window_handle().ok()?.as_raw() {
        RawWindowHandle::Win32(handle) => Some(handle.hwnd.get() as HWND),
        _ => None,
    }
}

fn screen_coordinate(value: isize) -> POINT {
    // The packed coordinates are signed 16-bit values, including on monitors
    // positioned to the left/top of the primary screen.
    POINT {
        x: (value as u16 as i16) as i32,
        y: ((value >> 16) as u16 as i16) as i32,
    }
}

/// Install native resizability, system-menu and Snap eligibility while keeping
/// the client-drawn Wolfmark caption. Never enable WS_CAPTION.
pub fn install(window: &Window) {
    let Some(handle) = hwnd(window) else { return };
    unsafe {
        let old = GetWindowLongPtrW(handle, GWL_STYLE);
        let required = WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        let updated = old | required as isize;
        if old != updated {
            SetWindowLongPtrW(handle, GWL_STYLE, updated);
            SetWindowPos(
                handle, std::ptr::null_mut(), 0, 0, 0, 0,
                SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE
                    | SWP_NOOWNERZORDER | SWP_NOZORDER,
            );
        }
        if SetWindowSubclass(handle, Some(wolfmark_subclass), FRAME_ID, 0) == 0 {
            eprintln!("Wolfmark could not register native Snap/resize hit-testing");
        }
    }
    refresh_corners(window);
}

/// Called from the native winit HWND message path before default processing.
/// The maximize region returns HTMAXBUTTON so Windows can show its own
/// Snap Layout hover flyout. Native down/up is paired to ShowWindow.
unsafe extern "system" fn wolfmark_subclass(
    hwnd: HWND,
    message: u32,
    wparam: usize,
    lparam: isize,
    id: usize,
    data: usize,
) -> isize {
    match message {
        WM_NCHITTEST => {
            let mut point = screen_coordinate(lparam);
            let mut client = RECT::default();
            if unsafe { ScreenToClient(hwnd, &mut point) } == 0 ||
               unsafe { GetClientRect(hwnd, &mut client) } == 0 {
                return unsafe { DefSubclassProc(hwnd, message, wparam, lparam) };
            }
            let dpi = unsafe { GetDpiForWindow(hwnd) }.max(96) as f64 / 96.0;
            let x = point.x as f64;
            let y = point.y as f64;
            let width = (client.right - client.left) as f64;
            let height = (client.bottom - client.top) as f64;
            // Return native resize hit codes while normal. Maximized windows
            // must not expose outer resize grips along screen edges.
            if unsafe { IsZoomed(hwnd) } == 0 {
                let border = 7.0 * dpi;
                let left = x < border;
                let right = x >= width - border;
                let top = y < border;
                let bottom = y >= height - border;
                let edge = match (top, bottom, left, right) {
                    (true, _, true, _) => Some(HTTOPLEFT),
                    (true, _, _, true) => Some(HTTOPRIGHT),
                    (_, true, true, _) => Some(HTBOTTOMLEFT),
                    (_, true, _, true) => Some(HTBOTTOMRIGHT),
                    (true, _, _, _) => Some(HTTOP),
                    (_, true, _, _) => Some(HTBOTTOM),
                    (_, _, true, _) => Some(HTLEFT),
                    (_, _, _, true) => Some(HTRIGHT),
                    _ => None,
                };
                if let Some(edge) = edge { return edge as isize; }
            }
            // On Windows, Wolfmark's 3 caption buttons are 44 CSS px wide.
            // HTMAXBUTTON, not HTCLIENT, is required for the native Snap flyout.
            let caption_height = 48.0 * dpi;
            if y >= 0.0 && y < caption_height
                && x >= width - 88.0 * dpi && x < width - 44.0 * dpi
            {
                return HTMAXBUTTON as isize;
            }
            // Give DWM its first opportunity to handle native interactions.
            let mut dwm_result = 0;
            if unsafe { DwmDefWindowProc(hwnd, message, wparam, lparam, &mut dwm_result) } != 0 {
                return dwm_result;
            }
            // Restrict draggable caption to empty top-bar space. Interactive
            // brand/breadcrumb/search/settings regions stay HTCLIENT.
            if y >= 8.0 * dpi && y < caption_height
                && x > (width.min(330.0 * dpi) * 0.85)
                && x < width - 425.0 * dpi
            {
                return HTCAPTION as isize;
            }
            HTCLIENT as isize
        }
        WM_NCLBUTTONDOWN if wparam == HTMAXBUTTON as usize => {
            // DWM owns Snap hover; the native button release owns maximize.
            0
        }
        WM_NCLBUTTONUP if wparam == HTMAXBUTTON as usize => {
            unsafe {
                ShowWindow(hwnd, if IsZoomed(hwnd) != 0 { SW_RESTORE } else { SW_MAXIMIZE });
            }
            refresh_hwnd_corners(hwnd);
            0
        }
        WM_NCDESTROY => {
            unsafe { RemoveWindowSubclass(hwnd, Some(wolfmark_subclass), id); }
            unsafe { DefSubclassProc(hwnd, message, wparam, lparam) }
        }
        _ => unsafe { DefSubclassProc(hwnd, message, wparam, lparam) },
    }
}

fn refresh_hwnd_corners(handle: HWND) {
    let preference: u32 = if unsafe { IsZoomed(handle) } != 0 { 1 } else { 2 };
    unsafe {
        let _ = DwmSetWindowAttribute(
            handle, 33, (&preference as *const u32).cast(),
            std::mem::size_of::<u32>() as u32,
        );
    }
}

/// DWM prefers rounded corners only in restored mode. This attribute is
/// ignored safely on Windows versions without rounded-corner support.
pub fn refresh_corners(window: &Window) {
    let Some(handle) = hwnd(window) else { return };
    let preference: u32 = if window.is_maximized() || window.fullscreen().is_some() { 1 } else { 2 };
    unsafe {
        let _ = DwmSetWindowAttribute(
            handle, 33, (&preference as *const u32).cast(),
            std::mem::size_of::<u32>() as u32,
        );
    }
}
