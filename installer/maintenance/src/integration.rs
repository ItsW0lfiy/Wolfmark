use std::fs;
use std::path::{Path, PathBuf};

use windows::Win32::System::Com::{
    CLSCTX_INPROC_SERVER, COINIT_APARTMENTTHREADED, CoCreateInstance, CoInitializeEx,
    CoUninitialize, IPersistFile,
};
use windows::Win32::UI::Shell::{IShellLinkW, ShellLink};
use windows::core::{Interface, PCWSTR};
use winreg::RegKey;
use winreg::enums::{HKEY_LOCAL_MACHINE, KEY_READ, KEY_WRITE};

use crate::InstallOptions;

const APPLICATION_KEY: &str = r"Software\Classes\Applications\Wolfmark.exe";
const UNINSTALL_KEY: &str = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\Wolfmark";

struct ComGuard;

impl ComGuard {
    fn initialize() -> Result<Self, String> {
        unsafe { CoInitializeEx(None, COINIT_APARTMENTTHREADED) }
            .ok()
            .map_err(|error| format!("COM initialization failed: {error}"))?;
        Ok(Self)
    }
}

impl Drop for ComGuard {
    fn drop(&mut self) {
        unsafe { CoUninitialize() };
    }
}

fn wide(value: &std::ffi::OsStr) -> Vec<u16> {
    use std::os::windows::ffi::OsStrExt;
    value.encode_wide().chain(Some(0)).collect()
}

fn create_shortcut(path: &Path, target: &Path, description: &str) -> Result<(), String> {
    let _com = ComGuard::initialize()?;
    let shell_link: IShellLinkW = unsafe {
        CoCreateInstance(&ShellLink, None, CLSCTX_INPROC_SERVER)
            .map_err(|error| format!("creating shell link failed: {error}"))?
    };
    let target_wide = wide(target.as_os_str());
    let working = target
        .parent()
        .ok_or_else(|| "shortcut target has no parent directory".to_owned())?;
    let working_wide = wide(working.as_os_str());
    let description_wide = wide(std::ffi::OsStr::new(description));
    unsafe {
        shell_link
            .SetPath(PCWSTR(target_wide.as_ptr()))
            .map_err(|error| format!("setting shortcut target failed: {error}"))?;
        shell_link
            .SetWorkingDirectory(PCWSTR(working_wide.as_ptr()))
            .map_err(|error| format!("setting shortcut directory failed: {error}"))?;
        shell_link
            .SetDescription(PCWSTR(description_wide.as_ptr()))
            .map_err(|error| format!("setting shortcut description failed: {error}"))?;
    }
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|error| error.to_string())?;
    }
    let persist: IPersistFile = shell_link
        .cast()
        .map_err(|error| format!("opening shortcut persistence failed: {error}"))?;
    let path_wide = wide(path.as_os_str());
    unsafe { persist.Save(PCWSTR(path_wide.as_ptr()), true) }
        .map_err(|error| format!("saving shortcut failed: {error}"))
}

fn start_menu_shortcut() -> Result<PathBuf, String> {
    let program_data =
        std::env::var_os("ProgramData").ok_or_else(|| "ProgramData is unavailable".to_owned())?;
    Ok(PathBuf::from(program_data).join(r"Microsoft\Windows\Start Menu\Programs\Wolfmark.lnk"))
}

fn desktop_shortcut() -> Result<PathBuf, String> {
    let public = std::env::var_os("PUBLIC").ok_or_else(|| "PUBLIC is unavailable".to_owned())?;
    Ok(PathBuf::from(public).join(r"Desktop\Wolfmark.lnk"))
}

pub fn apply(
    options: &InstallOptions,
    version: &str,
    build_id: &str,
    uninstaller: &Path,
) -> Result<(), String> {
    let executable = options.install_directory.join("Wolfmark.exe");
    let command = format!("\"{}\" \"%1\"", executable.display());
    let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);

    let (application, _) = hklm
        .create_subkey_with_flags(APPLICATION_KEY, KEY_READ | KEY_WRITE)
        .map_err(|error| format!("creating Open With registration failed: {error}"))?;
    application
        .set_value("FriendlyAppName", &"Wolfmark")
        .map_err(|error| error.to_string())?;
    application
        .set_value("NoOpenWith", &"")
        .map_err(|error| error.to_string())?;
    let (command_key, _) = application
        .create_subkey_with_flags(r"shell\open\command", KEY_READ | KEY_WRITE)
        .map_err(|error| error.to_string())?;
    command_key
        .set_value("", &command)
        .map_err(|error| error.to_string())?;
    let (types, _) = application
        .create_subkey_with_flags("SupportedTypes", KEY_READ | KEY_WRITE)
        .map_err(|error| error.to_string())?;
    for extension in [".md", ".markdown", ".txt"] {
        if options.file_associations {
            types
                .set_value(extension, &"")
                .map_err(|error| error.to_string())?;
        } else {
            let _ = types.delete_value(extension);
        }
    }

    let (uninstall, _) = hklm
        .create_subkey_with_flags(UNINSTALL_KEY, KEY_READ | KEY_WRITE)
        .map_err(|error| format!("creating Installed Apps registration failed: {error}"))?;
    uninstall
        .set_value("DisplayName", &"Wolfmark")
        .map_err(|error| error.to_string())?;
    uninstall
        .set_value("DisplayVersion", &version)
        .map_err(|error| error.to_string())?;
    uninstall
        .set_value("Publisher", &"ItsW0lfiy")
        .map_err(|error| error.to_string())?;
    uninstall
        .set_value(
            "InstallLocation",
            &options.install_directory.to_string_lossy().as_ref(),
        )
        .map_err(|error| error.to_string())?;
    uninstall
        .set_value("DisplayIcon", &format!("{},0", executable.display()))
        .map_err(|error| error.to_string())?;
    uninstall
        .set_value(
            "UninstallString",
            &format!("\"{}\" /UNINSTALL", uninstaller.display()),
        )
        .map_err(|error| error.to_string())?;
    uninstall
        .set_value(
            "QuietUninstallString",
            &format!("\"{}\" /S /UNINSTALL", uninstaller.display()),
        )
        .map_err(|error| error.to_string())?;
    uninstall
        .set_value(
            "ModifyPath",
            &format!("\"{}\" /MAINTENANCE", uninstaller.display()),
        )
        .map_err(|error| error.to_string())?;
    uninstall
        .set_value("BuildId", &build_id)
        .map_err(|error| error.to_string())?;
    uninstall
        .set_value("NoModify", &0_u32)
        .map_err(|error| error.to_string())?;
    uninstall
        .set_value("NoRepair", &0_u32)
        .map_err(|error| error.to_string())?;

    create_shortcut(
        &start_menu_shortcut()?,
        &executable,
        "Wolfmark Markdown viewer",
    )?;
    let desktop = desktop_shortcut()?;
    if options.desktop_shortcut {
        create_shortcut(&desktop, &executable, "Wolfmark Markdown viewer")?;
    } else if desktop.exists() {
        fs::remove_file(desktop).map_err(|error| error.to_string())?;
    }
    Ok(())
}

pub fn remove() -> Result<(), String> {
    let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);
    for key in [APPLICATION_KEY, UNINSTALL_KEY] {
        match hklm.delete_subkey_all(key) {
            Ok(()) => {}
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
            Err(error) => return Err(format!("removing registry key {key} failed: {error}")),
        }
    }
    for shortcut in [start_menu_shortcut()?, desktop_shortcut()?] {
        if shortcut.exists() {
            fs::remove_file(&shortcut)
                .map_err(|error| format!("removing {} failed: {error}", shortcut.display()))?;
        }
    }
    Ok(())
}

pub fn registration_exists() -> bool {
    RegKey::predef(HKEY_LOCAL_MACHINE)
        .open_subkey(APPLICATION_KEY)
        .is_ok()
        || RegKey::predef(HKEY_LOCAL_MACHINE)
            .open_subkey(UNINSTALL_KEY)
            .is_ok()
}
