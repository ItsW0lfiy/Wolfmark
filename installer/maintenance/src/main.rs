use std::fs;
use std::path::PathBuf;

use clap::{Parser, Subcommand};
use wolfmark_maintenance::{
    BuildIdentity, Engine, EnginePaths, InstallOptions, Operation, PayloadManifest,
    build_payload_manifest,
};

#[derive(Parser)]
#[command(name = "WolfmarkMaintenance", version, about)]
struct Cli {
    #[arg(long)]
    state_root: Option<PathBuf>,
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    Manifest {
        #[arg(long)]
        payload: PathBuf,
        #[arg(long)]
        identity: PathBuf,
        #[arg(long)]
        output: PathBuf,
    },
    Detect {
        #[arg(long)]
        manifest: Option<PathBuf>,
    },
    Install {
        #[arg(long)]
        payload: PathBuf,
        #[arg(long)]
        manifest: PathBuf,
        #[arg(long)]
        install_dir: PathBuf,
        #[arg(long)]
        uninstaller: PathBuf,
        #[arg(long)]
        cancel_file: Option<PathBuf>,
        #[arg(long, default_value_t = true, action = clap::ArgAction::Set)]
        file_associations: bool,
        #[arg(long, default_value_t = false, action = clap::ArgAction::Set)]
        desktop_shortcut: bool,
        #[arg(long, default_value = "install")]
        operation: String,
    },
    Repair {
        #[arg(long)]
        uninstaller: PathBuf,
    },
    Modify {
        #[arg(long, action = clap::ArgAction::Set)]
        file_associations: bool,
        #[arg(long, action = clap::ArgAction::Set)]
        desktop_shortcut: bool,
    },
    Uninstall,
    Diagnose,
}

fn main() {
    if let Err(error) = run() {
        let log = write_failure_log(&error);
        if let Some(path) = log {
            eprintln!("{error}\nDiagnostic log: {}", path.display());
        } else {
            eprintln!("{error}");
        }
        let code = if error.contains("cancelled") {
            1223
        } else if error.contains("not installed") {
            1605
        } else {
            1603
        };
        std::process::exit(code);
    }
}

fn run() -> Result<(), String> {
    let cli = Cli::parse();
    let paths = if let Some(root) = cli.state_root {
        EnginePaths {
            state_root: root.clone(),
            cache_root: root.join("Cache"),
            log_root: root.join("Logs"),
        }
    } else {
        EnginePaths::system_default()?
    };
    let output = match cli.command {
        Command::Manifest {
            payload,
            identity,
            output,
        } => {
            let identity: BuildIdentity = read_json(identity)?;
            let manifest = build_payload_manifest(&payload, identity)?;
            let bytes = serde_json::to_vec_pretty(&manifest).map_err(|error| error.to_string())?;
            fs::write(&output, bytes)
                .map_err(|error| format!("writing {} failed: {error}", output.display()))?;
            serde_json::json!({ "manifest": output, "files": manifest.files.len() })
        }
        Command::Detect { manifest } => {
            let engine = Engine::new(paths.clone());
            let manifest = manifest.map(read_manifest).transpose()?;
            serde_json::json!({
                "status": engine.detect(manifest.as_ref())?,
                "state": engine.state()?,
            })
        }
        Command::Install {
            payload,
            manifest,
            install_dir,
            uninstaller,
            cancel_file,
            file_associations,
            desktop_shortcut,
            operation,
        } => {
            let engine = Engine::with_cancel_file(paths.clone(), cancel_file);
            let operation = match operation.as_str() {
                "install" => Operation::Install,
                "update" => Operation::Update,
                "reinstall" => Operation::Reinstall,
                _ => return Err(format!("unsupported deployment operation: {operation}")),
            };
            serde_json::to_value(engine.install(
                operation,
                &payload,
                &read_manifest(manifest)?,
                InstallOptions {
                    install_directory: install_dir,
                    file_associations,
                    desktop_shortcut,
                },
                &uninstaller,
            )?)
            .map_err(|error| error.to_string())?
        }
        Command::Repair { uninstaller } => {
            let engine = Engine::new(paths.clone());
            serde_json::to_value(engine.repair(&uninstaller)?).map_err(|error| error.to_string())?
        }
        Command::Modify {
            file_associations,
            desktop_shortcut,
        } => serde_json::to_value(
            Engine::new(paths.clone()).modify(file_associations, desktop_shortcut)?,
        )
        .map_err(|error| error.to_string())?,
        Command::Uninstall => serde_json::to_value(Engine::new(paths.clone()).uninstall()?)
            .map_err(|error| error.to_string())?,
        Command::Diagnose => serde_json::json!({
            "status": Engine::new(paths.clone()).detect(None)?,
            "state": Engine::new(paths).state()?,
        }),
    };
    println!(
        "{}",
        serde_json::to_string_pretty(&output).map_err(|error| error.to_string())?
    );
    Ok(())
}

fn write_failure_log(error: &str) -> Option<PathBuf> {
    use chrono::Utc;
    let paths = EnginePaths::system_default().ok()?;
    fs::create_dir_all(&paths.log_root).ok()?;
    let path = paths.log_root.join(format!(
        "{}-failure.log",
        Utc::now().format("%Y%m%d-%H%M%S")
    ));
    fs::write(&path, format!("error={error}\n")).ok()?;
    Some(path)
}

fn read_manifest(path: PathBuf) -> Result<PayloadManifest, String> {
    read_json(path)
}

fn read_json<T: serde::de::DeserializeOwned>(path: PathBuf) -> Result<T, String> {
    let bytes =
        fs::read(&path).map_err(|error| format!("reading {} failed: {error}", path.display()))?;
    serde_json::from_slice(&bytes)
        .map_err(|error| format!("parsing {} failed: {error}", path.display()))
}
