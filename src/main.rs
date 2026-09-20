#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::ffi::CString;
use std::os::raw::{c_char, c_int};

unsafe extern "C" {
    fn wolfmark_qt_run(
        argc: c_int,
        argv: *const *const c_char,
        api: *const wolfmark::native_api::NativeApiTable,
    ) -> c_int;
}

fn main() {
    let arguments = std::env::args_os()
        .map(|argument| CString::new(argument.to_string_lossy().as_bytes()).expect("argument"))
        .collect::<Vec<_>>();
    let pointers = arguments
        .iter()
        .map(|argument| argument.as_ptr())
        .collect::<Vec<_>>();
    let api = wolfmark::native_api::table_address() as *const wolfmark::native_api::NativeApiTable;
    // SAFETY: the argument strings and stable API table remain alive for the whole Qt event loop.
    // The C++ entry point does not retain either pointer after returning.
    let exit_code = unsafe {
        wolfmark_qt_run(
            c_int::try_from(pointers.len()).unwrap_or(c_int::MAX),
            pointers.as_ptr(),
            api,
        )
    };
    if exit_code != 0 {
        std::process::exit(exit_code);
    }
}
