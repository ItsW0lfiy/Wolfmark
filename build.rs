use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    let manifest = PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").expect("Cargo manifest"));
    let target_os = std::env::var("CARGO_CFG_TARGET_OS").expect("Cargo target operating system");
    if target_os == "windows" {
        let qt = find_qt(&manifest).unwrap_or_else(|message| panic!("{message}"));
        compile_qt_bridge_windows(&manifest, &qt);
        stage_windows_runtime(&qt);
        stage_application_assets(&manifest);
        compile_windows_resources();
    } else if target_os == "linux" {
        compile_qt_bridge_linux(&manifest);
    } else {
        panic!("Moonmark's Qt desktop build currently supports Windows and Linux targets");
    }

    for file in [
        "native/qt/moonmark_api.h",
        "native/qt/moonmark_qt.h",
        "native/qt/moonmark_qt.cpp",
        "native/qt/moonmark_settings.h",
        "native/qt/moonmark_settings.cpp",
        "native/qt/moon_style.h",
        "native/qt/moon_style.cpp",
        "native/qt/document_zoom.h",
        "native/qt/document_zoom.cpp",
        "native/qt/document_sidebar.h",
        "native/qt/document_sidebar.cpp",
        "native/qt/smooth_scroll_controller.h",
        "native/qt/smooth_scroll_controller.cpp",
        "native/qt/moon_title_bar.h",
        "native/qt/moon_title_bar.cpp",
        "native/qt/windows_window_frame.h",
        "native/qt/windows_window_frame.cpp",
        "assets/icons/moonmark.rc",
        "assets/icons/moonmark.ico",
    ] {
        println!("cargo:rerun-if-changed={file}");
    }
    println!("cargo:rerun-if-env-changed=MOONMARK_QT_DIR");
    println!("cargo:rerun-if-env-changed=QTDIR");
    println!("cargo:rerun-if-env-changed=PKG_CONFIG_PATH");
}

fn find_qt(manifest: &Path) -> Result<PathBuf, String> {
    for variable in ["MOONMARK_QT_DIR", "QTDIR"] {
        if let Some(value) = std::env::var_os(variable) {
            let candidate = PathBuf::from(value);
            if qt_is_usable(&candidate) {
                return Ok(candidate);
            }
        }
    }

    let project_local = manifest.join("out/toolchains/qt");
    if qt_is_usable(&project_local) {
        return Ok(project_local);
    }

    for executable in ["qmake6", "qmake"] {
        if let Ok(output) = Command::new(executable)
            .args(["-query", "QT_INSTALL_PREFIX"])
            .output()
            && output.status.success()
        {
            let candidate = PathBuf::from(String::from_utf8_lossy(&output.stdout).trim());
            if qt_is_usable(&candidate) {
                return Ok(candidate);
            }
        }
    }

    Err("Qt 6.11.2 or another compatible Qt 6 Widgets SDK was not found. Set MOONMARK_QT_DIR or QTDIR, or run scripts/bootstrap_qt.ps1 on Windows.".into())
}

fn qt_is_usable(path: &Path) -> bool {
    path.join("include/QtWidgets/QApplication").is_file()
        && (path.join("lib/Qt6Widgets.lib").is_file()
            || path.join("lib/libQt6Widgets.so").is_file())
}

fn qt_bridge_build(manifest: &Path) -> cc::Build {
    let mut build = cc::Build::new();
    let product_version = format!(
        "\"{}\"",
        std::env::var("CARGO_PKG_VERSION").expect("Cargo package version")
    );
    build
        .cpp(true)
        .std("c++20")
        .file(manifest.join("native/qt/moonmark_qt.cpp"))
        .file(manifest.join("native/qt/moonmark_settings.cpp"))
        .file(manifest.join("native/qt/moon_style.cpp"))
        .file(manifest.join("native/qt/document_zoom.cpp"))
        .file(manifest.join("native/qt/document_sidebar.cpp"))
        .file(manifest.join("native/qt/smooth_scroll_controller.cpp"))
        .file(manifest.join("native/qt/moon_title_bar.cpp"))
        .file(manifest.join("native/qt/windows_window_frame.cpp"))
        .include(manifest.join("native/qt"))
        .define("MOONMARK_PRODUCT_VERSION", product_version.as_str())
        .warnings(true);
    build
}

fn compile_qt_bridge_windows(manifest: &Path, qt: &Path) {
    let mut build = qt_bridge_build(manifest);
    build
        .include(qt.join("include"))
        .include(qt.join("include/QtCore"))
        .include(qt.join("include/QtGui"))
        .include(qt.join("include/QtWidgets"))
        .flag("/EHsc")
        .flag("/permissive-")
        .flag("/Zc:__cplusplus")
        .flag("/utf-8")
        .flag("/wd4996")
        .flag("/W4");
    build.compile("moonmark_qt");

    println!(
        "cargo:rustc-link-search=native={}",
        qt.join("lib").display()
    );
    for library in ["Qt6Widgets", "Qt6Gui", "Qt6Core"] {
        println!("cargo:rustc-link-lib=dylib={library}");
    }
    for library in ["dwmapi", "uxtheme", "user32", "shell32"] {
        println!("cargo:rustc-link-lib=dylib={library}");
    }
}

fn compile_qt_bridge_linux(manifest: &Path) {
    let qt = pkg_config::Config::new()
        .atleast_version("6")
        .probe("Qt6Widgets")
        .unwrap_or_else(|error| panic!("Qt 6 Widgets development package not found: {error}"));
    let mut build = qt_bridge_build(manifest);
    build
        .pic(true)
        .flag("-Wall")
        .flag("-Wextra")
        .flag("-Wpedantic");
    for include in qt.include_paths {
        build.include(include);
    }
    for (name, value) in qt.defines {
        build.define(&name, value.as_deref());
    }
    build.compile("moonmark_qt");
}

fn profile_output_dir() -> PathBuf {
    let output = PathBuf::from(std::env::var_os("OUT_DIR").expect("Cargo OUT_DIR"));
    output
        .ancestors()
        .nth(3)
        .expect("Cargo profile output directory")
        .to_path_buf()
}

fn stage_windows_runtime(qt: &Path) {
    let output = profile_output_dir();
    std::fs::create_dir_all(output.join("platforms")).expect("create Qt platform directory");
    for name in ["Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll"] {
        copy_if_changed(&qt.join("bin").join(name), &output.join(name));
    }
    copy_if_changed(
        &qt.join("plugins/platforms/qwindows.dll"),
        &output.join("platforms/qwindows.dll"),
    );
}

fn stage_application_assets(manifest: &Path) {
    let output = profile_output_dir();
    let relative = "assets/branding/moonmark-symbol.png";
    copy_if_changed(&manifest.join(relative), &output.join(relative));
}

fn copy_if_changed(source: &Path, target: &Path) {
    if let Some(parent) = target.parent() {
        std::fs::create_dir_all(parent).expect("create staged runtime directory");
    }
    let source_size = std::fs::metadata(source)
        .unwrap_or_else(|_| panic!("required Qt runtime file missing: {}", source.display()))
        .len();
    let current_size = std::fs::metadata(target).map_or(0, |metadata| metadata.len());
    if source_size != current_size {
        std::fs::copy(source, target)
            .unwrap_or_else(|_| panic!("copy Qt runtime file: {}", source.display()));
    }
}

fn compile_windows_resources() {
    let Some(program_files) = std::env::var_os("ProgramFiles(x86)") else {
        println!("cargo:warning=Windows SDK not found; executable icon was not embedded");
        return;
    };
    let bin = PathBuf::from(program_files).join("Windows Kits/10/bin");
    let mut versions = std::fs::read_dir(&bin)
        .into_iter()
        .flatten()
        .flatten()
        .map(|entry| entry.path())
        .filter(|path| path.is_dir())
        .collect::<Vec<_>>();
    versions.sort_by(|left, right| right.file_name().cmp(&left.file_name()));
    let Some(rc) = versions
        .into_iter()
        .map(|version| version.join("x64/rc.exe"))
        .find(|candidate| candidate.is_file())
    else {
        println!("cargo:warning=rc.exe not found; executable icon was not embedded");
        return;
    };
    let output_dir = PathBuf::from(std::env::var_os("OUT_DIR").expect("Cargo OUT_DIR"));
    let output = output_dir.join("moonmark.res");
    let package_version = std::env::var("CARGO_PKG_VERSION").expect("Cargo package version");
    let numeric_version = windows_numeric_version(&package_version);
    let version_header = format!(
        "#define MOONMARK_VERSION_COMMAS {},{},{},{}\n#define MOONMARK_VERSION_STRING \"{}\"\n",
        numeric_version[0],
        numeric_version[1],
        numeric_version[2],
        numeric_version[3],
        package_version
    );
    std::fs::write(output_dir.join("moonmark_version.h"), version_header)
        .expect("write Windows version resource header");
    let status = Command::new(rc)
        .arg("/nologo")
        .arg(format!("/I{}", output_dir.display()))
        .arg(format!("/fo{}", output.display()))
        .arg("assets/icons/moonmark.rc")
        .status()
        .expect("launch Windows resource compiler");
    assert!(status.success(), "Windows resource compilation failed");
    println!("cargo:rustc-link-arg-bin=moonmark={}", output.display());
}

fn windows_numeric_version(version: &str) -> [u16; 4] {
    let (core, prerelease) = version.split_once('-').unwrap_or((version, ""));
    let mut core_parts = core.split('.').map(|part| part.parse::<u16>().unwrap_or(0));
    let prerelease_number = prerelease
        .rsplit('.')
        .next()
        .and_then(|part| part.parse::<u16>().ok())
        .unwrap_or(0);
    [
        core_parts.next().unwrap_or(0),
        core_parts.next().unwrap_or(0),
        core_parts.next().unwrap_or(0),
        prerelease_number,
    ]
}
