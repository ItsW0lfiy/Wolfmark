use std::fs;
use std::path::{Path, PathBuf};

use windows::Win32::Foundation::{CloseHandle, HWND, LPARAM, WPARAM};
use windows::Win32::System::Com::{
    CLSCTX_INPROC_SERVER, COINIT_APARTMENTTHREADED, CoCreateInstance, CoInitializeEx,
    CoUninitialize, IPersistFile,
};
use windows::Win32::System::Diagnostics::ToolHelp::{
    CreateToolhelp32Snapshot, PROCESSENTRY32W, Process32FirstW, Process32NextW, TH32CS_SNAPPROCESS,
};
use windows::Win32::UI::Shell::{
    IShellLinkW, SHCNE_ASSOCCHANGED, SHCNF_FLUSHNOWAIT, SHCNF_IDLIST, SHChangeNotify, ShellLink,
};
use windows::Win32::UI::WindowsAndMessaging::{
    EnumWindows, GetWindowThreadProcessId, PostMessageW, WM_CLOSE,
};
use windows::core::{BOOL, Interface, PCWSTR};
use winreg::RegKey;
use winreg::enums::{HKEY_LOCAL_MACHINE, KEY_READ, KEY_WRITE};

use crate::InstallOptions;

const APPLICATION_KEY: &str = r"Software\Classes\Applications\Wolfmark.exe";
const APP_PATH_KEY: &str = r"Software\Microsoft\Windows\CurrentVersion\App Paths\Wolfmark.exe";
const UNINSTALL_KEY: &str = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\Wolfmark";
const INSTALLER_KEY: &str = r"Software\ItsW0lfiy\Wolfmark\Installer";
const CAPABILITIES_KEY: &str = r"Software\ItsW0lfiy\Wolfmark\Capabilities";
const MARKDOWN_PROGID: &str = r"Software\Classes\Wolfmark.MarkdownDocument";
const TEXT_PROGID: &str = r"Software\Classes\Wolfmark.TextDocument";

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

    write_application_registration(&hklm, options, &executable, &command)?;

    let (installer, _) = hklm
        .create_subkey_with_flags(INSTALLER_KEY, KEY_READ | KEY_WRITE)
        .map_err(|error| error.to_string())?;
    installer
        .set_value(
            "InstallDir",
            &options.install_directory.to_string_lossy().as_ref(),
        )
        .map_err(|error| error.to_string())?;
    installer
        .set_value("Version", &version)
        .map_err(|error| error.to_string())?;
    installer
        .set_value("FileAssociations", &u32::from(options.file_associations))
        .map_err(|error| error.to_string())?;
    installer
        .set_value("DesktopShortcut", &u32::from(options.desktop_shortcut))
        .map_err(|error| error.to_string())?;
    if options.file_associations {
        write_file_associations(&hklm, &options.install_directory, &executable, &command)?;
    } else {
        remove_file_associations(&hklm)?;
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
        .set_value("URLInfoAbout", &"https://github.com/ItsW0lfiy/Wolfmark")
        .map_err(|error| error.to_string())?;
    uninstall
        .set_value("HelpLink", &"https://github.com/ItsW0lfiy/Wolfmark/issues")
        .map_err(|error| error.to_string())?;
    uninstall
        .set_value("Comments", &"Native Markdown and text document viewer")
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
    notify_associations_changed();
    Ok(())
}

pub fn remove() -> Result<(), String> {
    let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);
    remove_file_associations(&hklm)?;
    for key in [APPLICATION_KEY, APP_PATH_KEY, UNINSTALL_KEY, INSTALLER_KEY] {
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
    notify_associations_changed();
    Ok(())
}

fn notify_associations_changed() {
    // SAFETY: ASSOCCHANGED requires null item pointers; no borrowed data crosses
    // the call. FLUSHNOWAIT starts delivery without blocking on Explorer.
    unsafe {
        SHChangeNotify(
            SHCNE_ASSOCCHANGED,
            SHCNF_IDLIST | SHCNF_FLUSHNOWAIT,
            None,
            None,
        )
    };
}

fn write_application_registration(
    hklm: &RegKey,
    options: &InstallOptions,
    executable: &Path,
    command: &str,
) -> Result<(), String> {
    let (app_path, _) = hklm
        .create_subkey_with_flags(APP_PATH_KEY, KEY_READ | KEY_WRITE)
        .map_err(|error| format!("creating App Paths registration failed: {error}"))?;
    app_path
        .set_value("", &executable.to_string_lossy().as_ref())
        .map_err(|error| error.to_string())?;
    let (application, _) = hklm
        .create_subkey_with_flags(APPLICATION_KEY, KEY_READ | KEY_WRITE)
        .map_err(|error| format!("creating Open With registration failed: {error}"))?;
    application
        .set_value("FriendlyAppName", &"Wolfmark")
        .map_err(|error| error.to_string())?;
    let (icon, _) = application
        .create_subkey_with_flags("DefaultIcon", KEY_READ | KEY_WRITE)
        .map_err(|error| error.to_string())?;
    icon.set_value("", &format!("\"{}\",0", executable.display()))
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
    Ok(())
}

fn write_file_associations(
    hklm: &RegKey,
    install_directory: &Path,
    executable: &Path,
    command: &str,
) -> Result<(), String> {
    let (registered, _) = hklm
        .create_subkey_with_flags(r"Software\RegisteredApplications", KEY_READ | KEY_WRITE)
        .map_err(|error| error.to_string())?;
    registered
        .set_value("Wolfmark", &r"Software\ItsW0lfiy\Wolfmark\Capabilities")
        .map_err(|error| error.to_string())?;
    let (capabilities, _) = hklm
        .create_subkey_with_flags(CAPABILITIES_KEY, KEY_READ | KEY_WRITE)
        .map_err(|error| error.to_string())?;
    capabilities
        .set_value("ApplicationName", &"Wolfmark")
        .map_err(|error| error.to_string())?;
    capabilities
        .set_value(
            "ApplicationDescription",
            &"Native Markdown and text document viewer",
        )
        .map_err(|error| error.to_string())?;
    capabilities
        .set_value("ApplicationIcon", &format!("{},0", executable.display()))
        .map_err(|error| error.to_string())?;
    let (associations, _) = capabilities
        .create_subkey_with_flags("FileAssociations", KEY_READ | KEY_WRITE)
        .map_err(|error| error.to_string())?;
    associations
        .set_value(".md", &"Wolfmark.MarkdownDocument")
        .map_err(|error| error.to_string())?;
    associations
        .set_value(".markdown", &"Wolfmark.MarkdownDocument")
        .map_err(|error| error.to_string())?;
    associations
        .set_value(".txt", &"Wolfmark.TextDocument")
        .map_err(|error| error.to_string())?;
    write_progid(
        hklm,
        MARKDOWN_PROGID,
        "Wolfmark Markdown Document",
        &install_directory.join(r"assets\icons\wolfmark-markdown.ico"),
        command,
    )?;
    write_progid(
        hklm,
        TEXT_PROGID,
        "Wolfmark Text Document",
        &install_directory.join(r"assets\icons\wolfmark-text.ico"),
        command,
    )?;
    for (extension, progid) in [
        (".md", "Wolfmark.MarkdownDocument"),
        (".markdown", "Wolfmark.MarkdownDocument"),
        (".txt", "Wolfmark.TextDocument"),
    ] {
        let (open_with, _) = hklm
            .create_subkey_with_flags(
                format!(r"Software\Classes\{extension}\OpenWithProgids"),
                KEY_READ | KEY_WRITE,
            )
            .map_err(|error| error.to_string())?;
        open_with
            .set_value(progid, &"")
            .map_err(|error| error.to_string())?;
    }
    Ok(())
}

fn write_progid(
    hklm: &RegKey,
    key: &str,
    description: &str,
    icon: &Path,
    command: &str,
) -> Result<(), String> {
    let (progid, _) = hklm
        .create_subkey_with_flags(key, KEY_READ | KEY_WRITE)
        .map_err(|error| error.to_string())?;
    progid
        .set_value("", &description)
        .map_err(|error| error.to_string())?;
    let (default_icon, _) = progid
        .create_subkey_with_flags("DefaultIcon", KEY_READ | KEY_WRITE)
        .map_err(|error| error.to_string())?;
    default_icon
        .set_value("", &icon.to_string_lossy().as_ref())
        .map_err(|error| error.to_string())?;
    let (open, _) = progid
        .create_subkey_with_flags(r"shell\open\command", KEY_READ | KEY_WRITE)
        .map_err(|error| error.to_string())?;
    open.set_value("", &command)
        .map_err(|error| error.to_string())
}

fn remove_file_associations(hklm: &RegKey) -> Result<(), String> {
    if let Ok(registered) =
        hklm.open_subkey_with_flags(r"Software\RegisteredApplications", KEY_READ | KEY_WRITE)
    {
        let _ = registered.delete_value("Wolfmark");
    }
    for key in [CAPABILITIES_KEY, MARKDOWN_PROGID, TEXT_PROGID] {
        match hklm.delete_subkey_all(key) {
            Ok(()) => {}
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
            Err(error) => return Err(format!("removing registry key {key} failed: {error}")),
        }
    }
    for (extension, progid) in [
        (".md", "Wolfmark.MarkdownDocument"),
        (".markdown", "Wolfmark.MarkdownDocument"),
        (".txt", "Wolfmark.TextDocument"),
    ] {
        if let Ok(open_with) = hklm.open_subkey_with_flags(
            format!(r"Software\Classes\{extension}\OpenWithProgids"),
            KEY_READ | KEY_WRITE,
        ) {
            let _ = open_with.delete_value(progid);
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

pub fn request_wolfmark_exit() -> Result<(), String> {
    let mut process_ids = wolfmark_process_ids()?;
    if process_ids.is_empty() {
        return Ok(());
    }
    for process_id in &process_ids {
        unsafe extern "system" fn close_window(window: HWND, parameter: LPARAM) -> BOOL {
            let mut owner = 0_u32;
            unsafe { GetWindowThreadProcessId(window, Some(&mut owner)) };
            if owner == parameter.0 as u32 {
                let _ = unsafe { PostMessageW(Some(window), WM_CLOSE, WPARAM(0), LPARAM(0)) };
            }
            true.into()
        }
        unsafe { EnumWindows(Some(close_window), LPARAM(*process_id as isize)) }
            .map_err(|error| format!("requesting Wolfmark shutdown failed: {error}"))?;
    }
    for _ in 0..100 {
        std::thread::sleep(std::time::Duration::from_millis(100));
        process_ids = wolfmark_process_ids()?;
        if process_ids.is_empty() {
            return Ok(());
        }
    }
    Err(format!(
        "Wolfmark is still running (process {}). Close it and retry.",
        process_ids
            .iter()
            .map(u32::to_string)
            .collect::<Vec<_>>()
            .join(", ")
    ))
}

fn wolfmark_process_ids() -> Result<Vec<u32>, String> {
    let snapshot = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) }
        .map_err(|error| format!("enumerating processes failed: {error}"))?;
    let mut entry = PROCESSENTRY32W {
        dwSize: std::mem::size_of::<PROCESSENTRY32W>() as u32,
        ..Default::default()
    };
    let mut result = Vec::new();
    if unsafe { Process32FirstW(snapshot, &mut entry) }.is_ok() {
        loop {
            let length = entry
                .szExeFile
                .iter()
                .position(|character| *character == 0)
                .unwrap_or(entry.szExeFile.len());
            if String::from_utf16_lossy(&entry.szExeFile[..length])
                .eq_ignore_ascii_case("Wolfmark.exe")
            {
                result.push(entry.th32ProcessID);
            }
            if unsafe { Process32NextW(snapshot, &mut entry) }.is_err() {
                break;
            }
        }
    }
    unsafe { CloseHandle(snapshot) }
        .map_err(|error| format!("closing process snapshot failed: {error}"))?;
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winreg::enums::HKEY_CURRENT_USER;

    struct TestRegistry {
        root: RegKey,
        path: String,
        _directory: tempfile::TempDir,
    }

    impl TestRegistry {
        fn new() -> Self {
            let directory = tempfile::tempdir().unwrap();
            let path = format!(
                r"Software\WolfmarkRegistrationTests\{}",
                directory.path().file_name().unwrap().to_string_lossy()
            );
            let (root, _) = RegKey::predef(HKEY_CURRENT_USER)
                .create_subkey(&path)
                .unwrap();
            Self {
                root,
                path,
                _directory: directory,
            }
        }
    }

    impl Drop for TestRegistry {
        fn drop(&mut self) {
            // Remove only this test's uniquely named, isolated registry subtree.
            RegKey::predef(HKEY_CURRENT_USER)
                .delete_subkey_all(&self.path)
                .unwrap();
        }
    }

    #[test]
    fn shell_registration_exposes_installed_executable_and_default_app_capabilities() {
        let registry = TestRegistry::new();
        let options = InstallOptions {
            install_directory: PathBuf::from(r"C:\Program Files\Wolfmark Ω"),
            file_associations: true,
            desktop_shortcut: false,
        };
        let executable = options.install_directory.join("Wolfmark.exe");
        let command = format!("\"{}\" \"%1\"", executable.display());
        write_application_registration(&registry.root, &options, &executable, &command).unwrap();
        write_file_associations(
            &registry.root,
            &options.install_directory,
            &executable,
            &command,
        )
        .unwrap();
        let app_path = registry.root.open_subkey(APP_PATH_KEY).unwrap();
        assert_eq!(
            app_path.get_value::<String, _>("").unwrap(),
            executable.to_string_lossy()
        );
        let application = registry.root.open_subkey(APPLICATION_KEY).unwrap();
        assert_eq!(
            application
                .get_value::<String, _>("FriendlyAppName")
                .unwrap(),
            "Wolfmark"
        );
        assert_eq!(
            application
                .open_subkey(r"shell\open\command")
                .unwrap()
                .get_value::<String, _>("")
                .unwrap(),
            command
        );
        let registered = registry
            .root
            .open_subkey(r"Software\RegisteredApplications")
            .unwrap();
        assert_eq!(
            registered.get_value::<String, _>("Wolfmark").unwrap(),
            CAPABILITIES_KEY
        );
        let capabilities = registry.root.open_subkey(CAPABILITIES_KEY).unwrap();
        assert_eq!(
            capabilities
                .get_value::<String, _>("ApplicationName")
                .unwrap(),
            "Wolfmark"
        );
        for (extension, progid) in [
            (".md", "Wolfmark.MarkdownDocument"),
            (".markdown", "Wolfmark.MarkdownDocument"),
            (".txt", "Wolfmark.TextDocument"),
        ] {
            assert_eq!(
                application
                    .open_subkey("SupportedTypes")
                    .unwrap()
                    .get_value::<String, _>(extension)
                    .unwrap(),
                ""
            );
            assert_eq!(
                capabilities
                    .open_subkey("FileAssociations")
                    .unwrap()
                    .get_value::<String, _>(extension)
                    .unwrap(),
                progid
            );
            assert_eq!(
                registry
                    .root
                    .open_subkey(format!(r"Software\Classes\{extension}\OpenWithProgids"))
                    .unwrap()
                    .get_value::<String, _>(progid)
                    .unwrap(),
                ""
            );
        }
    }

    #[test]
    fn disabling_associations_preserves_other_handlers_and_user_default() {
        let registry = TestRegistry::new();
        let mut options = InstallOptions {
            install_directory: PathBuf::from(r"C:\Program Files\Wolfmark"),
            file_associations: true,
            desktop_shortcut: false,
        };
        let (extension, _) = registry
            .root
            .create_subkey(r"Software\Classes\.md")
            .unwrap();
        extension.set_value("", &"Other.Markdown").unwrap();
        let (others, _) = extension.create_subkey("OpenWithProgids").unwrap();
        others.set_value("Other.Markdown", &"").unwrap();
        let (registered, _) = registry
            .root
            .create_subkey(r"Software\RegisteredApplications")
            .unwrap();
        registered
            .set_value("OtherApp", &r"Software\OtherApp\Capabilities")
            .unwrap();
        let (choice, _) = registry
            .root
            .create_subkey(
                r"Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.md\UserChoice",
            )
            .unwrap();
        choice.set_value("ProgId", &"Other.Markdown").unwrap();
        choice.set_value("Hash", &"preserve-existing-hash").unwrap();
        let executable = options.install_directory.join("Wolfmark.exe");
        let command = format!("\"{}\" \"%1\"", executable.display());
        write_file_associations(
            &registry.root,
            &options.install_directory,
            &executable,
            &command,
        )
        .unwrap();
        options.file_associations = false;
        write_application_registration(&registry.root, &options, &executable, &command).unwrap();
        remove_file_associations(&registry.root).unwrap();
        assert!(registry.root.open_subkey(CAPABILITIES_KEY).is_err());
        assert!(registered.get_value::<String, _>("Wolfmark").is_err());
        assert!(
            others
                .get_value::<String, _>("Wolfmark.MarkdownDocument")
                .is_err()
        );
        assert_eq!(others.get_value::<String, _>("Other.Markdown").unwrap(), "");
        assert_eq!(
            registered.get_value::<String, _>("OtherApp").unwrap(),
            r"Software\OtherApp\Capabilities"
        );
        assert_eq!(
            extension.get_value::<String, _>("").unwrap(),
            "Other.Markdown"
        );
        assert_eq!(
            choice.get_value::<String, _>("ProgId").unwrap(),
            "Other.Markdown"
        );
        assert_eq!(
            choice.get_value::<String, _>("Hash").unwrap(),
            "preserve-existing-hash"
        );
        assert!(
            registry
                .root
                .open_subkey(format!(r"{APPLICATION_KEY}\SupportedTypes"))
                .unwrap()
                .get_value::<String, _>(".md")
                .is_err()
        );
    }
}
