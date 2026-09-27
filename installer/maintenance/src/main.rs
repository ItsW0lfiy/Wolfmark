use std::fs;
use std::path::PathBuf;

use clap::{Parser, Subcommand};
use wolfmark_maintenance::{Engine, EnginePaths, InstallOptions, Operation, PayloadManifest};

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
        eprintln!("{error}");
        std::process::exit(1);
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
    let engine = Engine::new(paths);
    let output = match cli.command {
        Command::Detect { manifest } => {
            let manifest = manifest.map(read_manifest).transpose()?;
            serde_json::to_value(engine.detect(manifest.as_ref())?)
                .map_err(|error| error.to_string())?
        }
        Command::Install {
            payload,
            manifest,
            install_dir,
            uninstaller,
            file_associations,
            desktop_shortcut,
            operation,
        } => {
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
            serde_json::to_value(engine.repair(&uninstaller)?).map_err(|error| error.to_string())?
        }
        Command::Modify {
            file_associations,
            desktop_shortcut,
        } => serde_json::to_value(engine.modify(file_associations, desktop_shortcut)?)
            .map_err(|error| error.to_string())?,
        Command::Uninstall => {
            serde_json::to_value(engine.uninstall()?).map_err(|error| error.to_string())?
        }
        Command::Diagnose => serde_json::json!({
            "status": engine.detect(None)?,
            "state": engine.state()?,
        }),
    };
    println!(
        "{}",
        serde_json::to_string_pretty(&output).map_err(|error| error.to_string())?
    );
    Ok(())
}

fn read_manifest(path: PathBuf) -> Result<PayloadManifest, String> {
    let bytes =
        fs::read(&path).map_err(|error| format!("reading {} failed: {error}", path.display()))?;
    serde_json::from_slice(&bytes)
        .map_err(|error| format!("parsing {} failed: {error}", path.display()))
}
