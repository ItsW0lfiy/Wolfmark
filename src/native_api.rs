//! Stable C ABI consumed by Moonmark's Qt Widgets presentation adapter.
//!
//! The boundary deliberately exchanges UTF-8 JSON presentation data, integer identifiers, and
//! decoded RGBA buffers. No UI-framework type is part of Moonmark's core contract.

use std::panic::{AssertUnwindSafe, catch_unwind};

use crate::app::Backend;
use crate::window::{WindowMode, WindowState};

#[repr(C)]
pub struct NativeBuffer {
    pub data: *mut u8,
    pub len: usize,
    pub capacity: usize,
}

impl NativeBuffer {
    fn from_vec(mut data: Vec<u8>) -> Self {
        let buffer = Self {
            data: data.as_mut_ptr(),
            len: data.len(),
            capacity: data.capacity(),
        };
        std::mem::forget(data);
        buffer
    }

    fn empty() -> Self {
        Self {
            data: std::ptr::null_mut(),
            len: 0,
            capacity: 0,
        }
    }
}

#[repr(C)]
pub struct NativeImageResult {
    pub id: u32,
    pub width: u32,
    pub height: u32,
    pub pixels: NativeBuffer,
    pub error: NativeBuffer,
}

#[repr(C)]
pub struct NativeCounters {
    pub parse_count: u64,
    pub load_count: u64,
    pub image_request_count: u64,
    pub image_cache_bytes: u64,
}

#[repr(C)]
pub struct NativeApiTable {
    pub version: u32,
    pub backend_new: extern "C" fn() -> *mut Backend,
    pub backend_free: unsafe extern "C" fn(*mut Backend),
    pub open_document: unsafe extern "C" fn(*mut Backend, *const u8, usize) -> NativeBuffer,
    pub queue_image: unsafe extern "C" fn(*mut Backend, u32, u32) -> bool,
    pub poll_image: unsafe extern "C" fn(*const Backend) -> NativeImageResult,
    pub backend_counters: unsafe extern "C" fn(*const Backend) -> NativeCounters,
    pub select_update: unsafe extern "C" fn(*const u8, usize, bool) -> NativeBuffer,
    pub verify_update:
        unsafe extern "C" fn(*const u8, usize, *const u8, usize, *const u8, usize) -> NativeBuffer,
    pub buffer_free: unsafe extern "C" fn(NativeBuffer),
    pub window_state_new: extern "C" fn() -> *mut WindowState,
    pub window_state_free: unsafe extern "C" fn(*mut WindowState),
    pub window_set_mode: unsafe extern "C" fn(*mut WindowState, u8),
    pub window_enter_fullscreen: unsafe extern "C" fn(*mut WindowState) -> u8,
    pub window_leave_fullscreen: unsafe extern "C" fn(*mut WindowState) -> u8,
}

static NATIVE_API: NativeApiTable = NativeApiTable {
    version: 3,
    backend_new: moonmark_backend_new,
    backend_free: moonmark_backend_free,
    open_document: moonmark_open_document,
    queue_image: moonmark_queue_image,
    poll_image: moonmark_poll_image,
    backend_counters: moonmark_backend_counters,
    select_update: moonmark_select_update,
    verify_update: moonmark_verify_update,
    buffer_free: moonmark_buffer_free,
    window_state_new: moonmark_window_state_new,
    window_state_free: moonmark_window_state_free,
    window_set_mode: moonmark_window_set_mode,
    window_enter_fullscreen: moonmark_window_enter_fullscreen,
    window_leave_fullscreen: moonmark_window_leave_fullscreen,
};

pub fn table_address() -> usize {
    std::ptr::from_ref(&NATIVE_API) as usize
}

impl NativeImageResult {
    fn empty() -> Self {
        Self {
            id: 0,
            width: 0,
            height: 0,
            pixels: NativeBuffer::empty(),
            error: NativeBuffer::empty(),
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn moonmark_api_version() -> u32 {
    3
}

#[unsafe(no_mangle)]
pub extern "C" fn moonmark_backend_new() -> *mut Backend {
    Box::into_raw(Box::<Backend>::default())
}

/// # Safety
/// `backend` must be either null or a pointer returned by `moonmark_backend_new` that has not
/// previously been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn moonmark_backend_free(backend: *mut Backend) {
    if !backend.is_null() {
        // SAFETY: guaranteed by this function's contract.
        let backend = unsafe { Box::from_raw(backend) };
        // Dropping the owned Rayon pool waits for any decode already inside a codec.
        // Final application shutdown must not block Qt's UI thread on that wait.
        let _ = std::thread::Builder::new()
            .name("moonmark-image-shutdown".into())
            .spawn(move || drop(backend));
    }
}

/// # Safety
/// `backend` must reference a live Moonmark backend. `path` must point to `path_len` readable
/// bytes for the duration of the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn moonmark_open_document(
    backend: *mut Backend,
    path: *const u8,
    path_len: usize,
) -> NativeBuffer {
    let result = catch_unwind(AssertUnwindSafe(|| {
        // SAFETY: guaranteed by this function's contract and checked for null below.
        let backend = unsafe { backend.as_mut() }.ok_or("Moonmark backend is unavailable")?;
        if path.is_null() {
            return Err("Document path is unavailable");
        }
        // SAFETY: guaranteed by this function's contract.
        let bytes = unsafe { std::slice::from_raw_parts(path, path_len) };
        let path = std::str::from_utf8(bytes).map_err(|_| "Document path is not valid UTF-8")?;
        serde_json::to_vec(&backend.open_document(path))
            .map_err(|_| "Could not encode Moonmark presentation data")
    }));
    match result {
        Ok(Ok(bytes)) => NativeBuffer::from_vec(bytes),
        Ok(Err(message)) => error_document(message),
        Err(_) => error_document("Moonmark core failed while opening the document"),
    }
}

/// # Safety
/// `backend` must reference a live Moonmark backend.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn moonmark_queue_image(
    backend: *mut Backend,
    id: u32,
    max_width: u32,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        // SAFETY: guaranteed by this function's contract and checked for null below.
        unsafe { backend.as_mut() }.is_some_and(|backend| backend.queue_image(id, max_width))
    }))
    .unwrap_or(false)
}

/// # Safety
/// `backend` must reference a live Moonmark backend.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn moonmark_poll_image(backend: *const Backend) -> NativeImageResult {
    catch_unwind(AssertUnwindSafe(|| {
        // SAFETY: guaranteed by this function's contract and checked for null below.
        let Some(backend) = (unsafe { backend.as_ref() }) else {
            return NativeImageResult::empty();
        };
        let Some(result) = backend.poll_image() else {
            return NativeImageResult::empty();
        };
        NativeImageResult {
            id: result.id,
            width: result.width,
            height: result.height,
            pixels: NativeBuffer::from_vec(result.rgba),
            error: NativeBuffer::from_vec(result.error.into_bytes()),
        }
    }))
    .unwrap_or_else(|_| NativeImageResult::empty())
}

/// # Safety
/// `backend` must reference a live Moonmark backend.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn moonmark_backend_counters(backend: *const Backend) -> NativeCounters {
    let Some(backend) = (unsafe { backend.as_ref() }) else {
        return NativeCounters {
            parse_count: 0,
            load_count: 0,
            image_request_count: 0,
            image_cache_bytes: 0,
        };
    };
    NativeCounters {
        parse_count: backend.parse_count(),
        load_count: backend.load_count(),
        image_request_count: backend.image_request_count(),
        image_cache_bytes: backend.cache_cost(),
    }
}

/// # Safety
/// `releases_json` must point to `releases_len` readable bytes for the duration of the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn moonmark_select_update(
    releases_json: *const u8,
    releases_len: usize,
    include_prereleases: bool,
) -> NativeBuffer {
    let result = catch_unwind(AssertUnwindSafe(|| {
        let bytes = unsafe { required_bytes(releases_json, releases_len)? };
        match crate::updates::select_release(bytes, env!("CARGO_PKG_VERSION"), include_prereleases)
        {
            Ok(Some(release)) => serde_json::to_vec(&serde_json::json!({
                "status": "available",
                "release": release,
            })),
            Ok(None) => serde_json::to_vec(&serde_json::json!({"status": "current"})),
            Err(message) => serde_json::to_vec(&serde_json::json!({
                "status": "error",
                "message": message,
            })),
        }
        .map_err(|_| "Could not encode Moonmark update data")
    }));
    match result {
        Ok(Ok(bytes)) => NativeBuffer::from_vec(bytes),
        Ok(Err(message)) => update_error_buffer(message),
        Err(_) => update_error_buffer("Moonmark failed while selecting an update"),
    }
}

/// # Safety
/// Each pointer must reference its corresponding readable byte length for the duration of the
/// call. The file path and asset name must be UTF-8.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn moonmark_verify_update(
    file_path: *const u8,
    file_path_len: usize,
    manifest: *const u8,
    manifest_len: usize,
    asset_name: *const u8,
    asset_name_len: usize,
) -> NativeBuffer {
    let result = catch_unwind(AssertUnwindSafe(|| {
        let file_path = unsafe { required_utf8(file_path, file_path_len)? };
        let manifest = unsafe { required_bytes(manifest, manifest_len)? };
        let asset_name = unsafe { required_utf8(asset_name, asset_name_len)? };
        let verification = crate::updates::verify_file_checksum(
            std::path::Path::new(file_path),
            manifest,
            asset_name,
        );
        let value = match verification {
            Ok(()) => serde_json::json!({"valid": true}),
            Err(message) => serde_json::json!({"valid": false, "message": message}),
        };
        serde_json::to_vec(&value).map_err(|_| "Could not encode update verification result")
    }));
    match result {
        Ok(Ok(bytes)) => NativeBuffer::from_vec(bytes),
        Ok(Err(message)) => update_verification_error_buffer(message),
        Err(_) => update_verification_error_buffer("Moonmark failed while verifying the update"),
    }
}

/// # Safety
/// `buffer` must be empty or have been returned by a Moonmark native API function, and must not
/// have previously been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn moonmark_buffer_free(buffer: NativeBuffer) {
    if !buffer.data.is_null() {
        // SAFETY: guaranteed by this function's contract.
        drop(unsafe { Vec::from_raw_parts(buffer.data, buffer.len, buffer.capacity) });
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn moonmark_window_state_new() -> *mut WindowState {
    Box::into_raw(Box::<WindowState>::default())
}

/// # Safety
/// `state` must be either null or a pointer returned by `moonmark_window_state_new` that has not
/// previously been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn moonmark_window_state_free(state: *mut WindowState) {
    if !state.is_null() {
        // SAFETY: guaranteed by this function's contract.
        drop(unsafe { Box::from_raw(state) });
    }
}

/// # Safety
/// `state` must reference a live Moonmark window state.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn moonmark_window_set_mode(state: *mut WindowState, mode: u8) {
    if let Some(state) = unsafe { state.as_mut() } {
        state.mode = decode_window_mode(mode);
    }
}

/// # Safety
/// `state` must reference a live Moonmark window state.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn moonmark_window_enter_fullscreen(state: *mut WindowState) -> u8 {
    let Some(state) = (unsafe { state.as_mut() }) else {
        return 0;
    };
    state.enter_fullscreen_model();
    encode_window_mode(state.mode)
}

/// # Safety
/// `state` must reference a live Moonmark window state.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn moonmark_window_leave_fullscreen(state: *mut WindowState) -> u8 {
    let Some(state) = (unsafe { state.as_mut() }) else {
        return 0;
    };
    state.leave_fullscreen_model();
    encode_window_mode(state.mode)
}

fn decode_window_mode(mode: u8) -> WindowMode {
    match mode {
        1 => WindowMode::Maximized,
        2 => WindowMode::BorderlessFullscreen,
        _ => WindowMode::Normal,
    }
}

fn encode_window_mode(mode: WindowMode) -> u8 {
    match mode {
        WindowMode::Normal => 0,
        WindowMode::Maximized => 1,
        WindowMode::BorderlessFullscreen => 2,
    }
}

fn error_document(message: &str) -> NativeBuffer {
    let document = crate::presentation::PresentationDocument::error(message.to_owned());
    NativeBuffer::from_vec(serde_json::to_vec(&document).unwrap_or_default())
}

unsafe fn required_bytes<'a>(data: *const u8, len: usize) -> Result<&'a [u8], &'static str> {
    if data.is_null() {
        return Err("Required update data is unavailable");
    }
    Ok(unsafe { std::slice::from_raw_parts(data, len) })
}

unsafe fn required_utf8<'a>(data: *const u8, len: usize) -> Result<&'a str, &'static str> {
    let bytes = unsafe { required_bytes(data, len)? };
    std::str::from_utf8(bytes).map_err(|_| "Required update text is not valid UTF-8")
}

fn update_error_buffer(message: &str) -> NativeBuffer {
    NativeBuffer::from_vec(
        serde_json::to_vec(&serde_json::json!({"status": "error", "message": message}))
            .unwrap_or_default(),
    )
}

fn update_verification_error_buffer(message: &str) -> NativeBuffer {
    NativeBuffer::from_vec(
        serde_json::to_vec(&serde_json::json!({"valid": false, "message": message}))
            .unwrap_or_default(),
    )
}
