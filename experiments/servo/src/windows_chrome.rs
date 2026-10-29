// SPDX-License-Identifier: GPL-3.0-only
//! Minimal Win32/DWM parity for the Servo-owned top-level winit window.
//! Mirrors the proven Qt adapter's style flags and corner preference without
//! introducing any Wolfmark-authored C++ or taking over winit's window proc.
#![cfg(target_os = "windows")]

use winit::raw_window_handle::{HasWindowHandle, RawWindowHandle};
use winit::window::Window;
use windows_sys::Win32::Foundation::HWND;
use windows_sys::Win32::Graphics::Dwm::DwmSetWindowAttribute;
use windows_sys::Win32::UI::WindowsAndMessaging::{
    GWL_STYLE, GetWindowLongPtrW, SWP_FRAMECHANGED, SWP_NOACTIVATE, SWP_NOMOVE,
    SWP_NOOWNERZORDER, SWP_NOSIZE, SWP_NOZORDER, SetWindowLongPtrW,
    SetWindowPos, WS_MAXIMIZEBOX, WS_MINIMIZEBOX, WS_SYSMENU, WS_THICKFRAME,
};

fn hwnd(window: &Window) -> Option<HWND> {
    match window.window_handle().ok()?.as_raw() {
        RawWindowHandle::Win32(handle) => Some(handle.hwnd.get() as HWND),
        _ => None,
    }
}

/// Install native resizability and Snap eligibility. Does not use WS_CAPTION:
/// the Qt implementation found that enabling it can break client painting.
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
    }
    refresh_corners(window);
}

/// DWM prefers rounded corners only in normal/restored mode. Windows 10 may
/// ignore this Windows 11-only attribute safely.
pub fn refresh_corners(window: &Window) {
    let Some(handle) = hwnd(window) else { return };
    let preference: u32 = if window.is_maximized() || window.fullscreen().is_some() { 1 } else { 2 };
    unsafe {
        let _ = DwmSetWindowAttribute(
            handle,
            33,
            (&preference as *const u32).cast(),
            std::mem::size_of::<u32>() as u32,
        );
    }
}
