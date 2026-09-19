use std::env;
use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};

fn project_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .expect("Cargo alias crate must remain below scripts/cargo_alias")
        .to_path_buf()
}

fn main() -> ExitCode {
    let Some(action) = env::args().nth(1) else {
        eprintln!("Moonmark Cargo alias action is missing.");
        return ExitCode::FAILURE;
    };
    let root = project_root();
    let scripts = root.join("scripts");
    let (script, extra_arguments): (&str, &[&str]) = match action.as_str() {
        "setup" => ("setup.ps1", &[]),
        "package" => ("package_windows.ps1", &[]),
        "clean" => ("clean.ps1", &[]),
        "deep-clean" => ("clean.ps1", &["-Deep"]),
        _ => {
            eprintln!("Unknown Moonmark Cargo alias action: {action}");
            return ExitCode::FAILURE;
        }
    };
    let forwarded_arguments: Vec<OsString> = env::args_os().skip(2).collect();

    let status = Command::new("pwsh")
        .args(["-NoProfile", "-File"])
        .arg(scripts.join(script))
        .args(extra_arguments)
        .args(forwarded_arguments)
        .current_dir(root)
        .status();
    match status {
        Ok(status) if status.success() => ExitCode::SUCCESS,
        Ok(status) => {
            eprintln!("Moonmark {action} command failed with {status}.");
            ExitCode::FAILURE
        }
        Err(error) => {
            eprintln!("Could not launch PowerShell 7 for Moonmark {action}: {error}");
            ExitCode::FAILURE
        }
    }
}
