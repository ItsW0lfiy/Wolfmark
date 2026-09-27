use std::fs::{self, File};
use std::io::Write;
use std::path::{Path, PathBuf};

use chrono::Utc;
use serde::Serialize;
use walkdir::WalkDir;

use crate::integration;
use crate::{
    InstallOptions, InstallState, InstallationStatus, Operation, PayloadManifest,
    STATE_SCHEMA_VERSION, TransactionJournal, TransactionPhase, verify_payload,
};

const STATE_FILE: &str = "install-state.json";
const JOURNAL_FILE: &str = "transaction.json";

#[derive(Clone, Debug)]
pub struct EnginePaths {
    pub state_root: PathBuf,
    pub cache_root: PathBuf,
    pub log_root: PathBuf,
}

impl EnginePaths {
    pub fn system_default() -> Result<Self, String> {
        let program_data = std::env::var_os("ProgramData")
            .ok_or_else(|| "ProgramData is unavailable".to_owned())?;
        let root = PathBuf::from(program_data).join(r"Wolfmark\Installer");
        Ok(Self {
            state_root: root.clone(),
            cache_root: root.join("Cache"),
            log_root: root.join("Logs"),
        })
    }
}

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct OperationResult {
    pub operation: Operation,
    pub changed: bool,
    pub message: String,
    pub log_path: PathBuf,
}

pub struct Engine {
    paths: EnginePaths,
}

impl Engine {
    pub fn new(paths: EnginePaths) -> Self {
        Self { paths }
    }

    pub fn state(&self) -> Result<Option<InstallState>, String> {
        read_json_optional(&self.paths.state_root.join(STATE_FILE))
    }

    pub fn detect(
        &self,
        candidate: Option<&PayloadManifest>,
    ) -> Result<InstallationStatus, String> {
        if let Some(journal) =
            read_json_optional::<TransactionJournal>(&self.paths.state_root.join(JOURNAL_FILE))?
            && !matches!(
                journal.phase,
                TransactionPhase::Committed | TransactionPhase::RolledBack
            )
        {
            return Ok(InstallationStatus::Interrupted { journal });
        }
        let Some(state) = self.state()? else {
            if integration::registration_exists() {
                return Ok(InstallationStatus::Partial {
                    details: "Windows registration exists but install-state.json is missing".into(),
                });
            }
            return Ok(InstallationStatus::Absent);
        };
        if !state.options.install_directory.exists() {
            return Ok(InstallationStatus::Partial {
                details: format!(
                    "state exists but {} is missing",
                    state.options.install_directory.display()
                ),
            });
        }
        let damaged = verify_payload(&state.options.install_directory, &state.manifest)?;
        let Some(candidate) = candidate else {
            return if damaged.is_empty() {
                Ok(InstallationStatus::Exact {
                    build: state.manifest.build,
                    damaged_files: damaged,
                })
            } else {
                Ok(InstallationStatus::Damaged {
                    build: state.manifest.build,
                    damaged_files: damaged,
                })
            };
        };
        let installed = state.manifest.build;
        let next = candidate.build.clone();
        if next.dirty {
            return Ok(InstallationStatus::DirtyBuild { build: next });
        }
        if installed.source_commit == next.source_commit {
            return if damaged.is_empty() {
                Ok(InstallationStatus::Exact {
                    build: installed,
                    damaged_files: damaged,
                })
            } else {
                Ok(InstallationStatus::Damaged {
                    build: installed,
                    damaged_files: damaged,
                })
            };
        }
        if installed.version == next.version {
            if next.ancestors.contains(&installed.source_commit) {
                return Ok(InstallationStatus::SameVersionInstalledAncestor {
                    installed,
                    candidate: next,
                });
            }
            if installed.ancestors.contains(&next.source_commit) {
                return Ok(InstallationStatus::SameVersionCandidateAncestor {
                    installed,
                    candidate: next,
                });
            }
            return Ok(InstallationStatus::Diverged {
                installed,
                candidate: next,
            });
        }
        match compare_versions(&installed.version, &next.version) {
            std::cmp::Ordering::Less => Ok(InstallationStatus::Older {
                installed,
                candidate: next,
            }),
            std::cmp::Ordering::Greater => Ok(InstallationStatus::Newer {
                installed,
                candidate: next,
            }),
            std::cmp::Ordering::Equal => Ok(InstallationStatus::Diverged {
                installed,
                candidate: next,
            }),
        }
    }

    pub fn install(
        &self,
        operation: Operation,
        payload_root: &Path,
        manifest: &PayloadManifest,
        options: InstallOptions,
        uninstaller_source: &Path,
    ) -> Result<OperationResult, String> {
        if !matches!(
            operation,
            Operation::Install | Operation::Update | Operation::Reinstall
        ) {
            return Err(format!("invalid deployment operation: {operation:?}"));
        }
        let damaged_source = verify_payload(payload_root, manifest)?;
        if !damaged_source.is_empty() {
            return Err(format!(
                "setup payload failed validation: {}",
                damaged_source.join(", ")
            ));
        }
        let previous = self.state()?;
        let mut journal = self.begin_journal(
            operation,
            Some(payload_root.to_path_buf()),
            options.install_directory.clone(),
            previous.clone(),
        )?;
        let result = self.deploy_transaction(
            &mut journal,
            payload_root,
            manifest,
            options,
            uninstaller_source,
            previous,
        );
        match result {
            Ok(result) => Ok(result),
            Err(error) => {
                self.rollback(&mut journal)?;
                Err(error)
            }
        }
    }

    pub fn repair(&self, uninstaller_source: &Path) -> Result<OperationResult, String> {
        let state = self
            .state()?
            .ok_or_else(|| "Wolfmark is not installed".to_owned())?;
        let current_cache = self.paths.cache_root.join("current");
        let damaged_cache = verify_payload(&current_cache, &state.manifest)?;
        if !damaged_cache.is_empty() {
            return Err(format!(
                "exact-build repair cache is incomplete: {}. Use Reinstall with the matching setup build.",
                damaged_cache.join(", ")
            ));
        }
        let damaged = verify_payload(&state.options.install_directory, &state.manifest)?;
        for relative in &damaged {
            copy_one(
                &current_cache.join(relative),
                &state.options.install_directory.join(relative),
            )?;
        }
        let uninstaller = self.paths.state_root.join("Wolfmark-Uninstall.exe");
        copy_one(uninstaller_source, &uninstaller)?;
        integration::apply(
            &state.options,
            &state.manifest.build.version,
            &state.manifest.build.build_id,
            &uninstaller,
        )?;
        let remaining = verify_payload(&state.options.install_directory, &state.manifest)?;
        if !remaining.is_empty() {
            return Err(format!(
                "repair validation failed: {}",
                remaining.join(", ")
            ));
        }
        self.result(
            Operation::Repair,
            !damaged.is_empty(),
            format!(
                "verified {} files; restored {}",
                state.manifest.files.len(),
                damaged.len()
            ),
        )
    }

    pub fn modify(
        &self,
        file_associations: bool,
        desktop_shortcut: bool,
    ) -> Result<OperationResult, String> {
        let mut state = self
            .state()?
            .ok_or_else(|| "Wolfmark is not installed".to_owned())?;
        state.options.file_associations = file_associations;
        state.options.desktop_shortcut = desktop_shortcut;
        state.updated_at = Utc::now().timestamp();
        let uninstaller = self.paths.state_root.join("Wolfmark-Uninstall.exe");
        integration::apply(
            &state.options,
            &state.manifest.build.version,
            &state.manifest.build.build_id,
            &uninstaller,
        )?;
        write_json_atomic(&self.paths.state_root.join(STATE_FILE), &state)?;
        self.result(
            Operation::Modify,
            true,
            "installation options updated".into(),
        )
    }

    pub fn uninstall(&self) -> Result<OperationResult, String> {
        let state = self.state()?;
        if let Some(state) = &state {
            for file in &state.manifest.files {
                let path = state.options.install_directory.join(&file.path);
                if path.exists() {
                    fs::remove_file(&path)
                        .map_err(|error| format!("removing {} failed: {error}", path.display()))?;
                }
            }
            remove_empty_directories(&state.options.install_directory)?;
        }
        integration::remove()?;
        if self.paths.state_root.exists() {
            fs::remove_dir_all(&self.paths.state_root).map_err(|error| {
                format!(
                    "removing {} failed: {error}",
                    self.paths.state_root.display()
                )
            })?;
        }
        self.result(
            Operation::Uninstall,
            state.is_some(),
            "Wolfmark-owned files and registration removed".into(),
        )
    }

    fn deploy_transaction(
        &self,
        journal: &mut TransactionJournal,
        payload_root: &Path,
        manifest: &PayloadManifest,
        options: InstallOptions,
        uninstaller_source: &Path,
        previous: Option<InstallState>,
    ) -> Result<OperationResult, String> {
        fs::create_dir_all(&self.paths.state_root).map_err(|error| error.to_string())?;
        let staged = self
            .paths
            .state_root
            .join("Staging")
            .join(&journal.transaction_id);
        copy_manifest_files(payload_root, &staged, manifest)?;
        self.advance(journal, TransactionPhase::PayloadStaged)?;

        let backup = self
            .paths
            .state_root
            .join("Backup")
            .join(&journal.transaction_id);
        if let Some(previous) = &previous {
            copy_existing_manifest_files(
                &previous.options.install_directory,
                &backup,
                &previous.manifest,
            )?;
        }
        self.advance(journal, TransactionPhase::ExistingBackedUp)?;

        if let Some(previous) = &previous {
            remove_owned_files(&previous.options.install_directory, &previous.manifest)?;
        }
        copy_manifest_files(&staged, &options.install_directory, manifest)?;
        self.advance(journal, TransactionPhase::PayloadApplied)?;

        let uninstaller = self.paths.state_root.join("Wolfmark-Uninstall.exe");
        copy_one(uninstaller_source, &uninstaller)?;
        integration::apply(
            &options,
            &manifest.build.version,
            &manifest.build.build_id,
            &uninstaller,
        )?;
        self.advance(journal, TransactionPhase::IntegrationApplied)?;
        let damaged = verify_payload(&options.install_directory, manifest)?;
        if !damaged.is_empty() {
            return Err(format!(
                "installed payload validation failed: {}",
                damaged.join(", ")
            ));
        }
        self.advance(journal, TransactionPhase::Validated)?;

        let now = Utc::now().timestamp();
        let state = InstallState {
            schema_version: STATE_SCHEMA_VERSION,
            manifest: manifest.clone(),
            options,
            installed_at: previous.as_ref().map_or(now, |value| value.installed_at),
            updated_at: now,
        };
        let current_cache = self.paths.cache_root.join("current");
        let previous_cache = self.paths.cache_root.join("previous");
        if previous_cache.exists() {
            fs::remove_dir_all(&previous_cache).map_err(|error| error.to_string())?;
        }
        if current_cache.exists() {
            fs::rename(&current_cache, &previous_cache).map_err(|error| error.to_string())?;
        }
        copy_manifest_files(payload_root, &current_cache, manifest)?;
        write_json_atomic(&current_cache.join("payload-manifest.json"), manifest)?;
        write_json_atomic(&self.paths.state_root.join(STATE_FILE), &state)?;
        self.advance(journal, TransactionPhase::Committed)?;
        let _ = fs::remove_dir_all(
            self.paths
                .state_root
                .join("Staging")
                .join(&journal.transaction_id),
        );
        let _ = fs::remove_dir_all(
            self.paths
                .state_root
                .join("Backup")
                .join(&journal.transaction_id),
        );
        self.result(
            journal.operation,
            true,
            format!(
                "Wolfmark {} ({}) installed",
                manifest.build.version, manifest.build.build_id
            ),
        )
    }

    fn begin_journal(
        &self,
        operation: Operation,
        source_payload: Option<PathBuf>,
        target_directory: PathBuf,
        previous_state: Option<InstallState>,
    ) -> Result<TransactionJournal, String> {
        fs::create_dir_all(&self.paths.state_root).map_err(|error| error.to_string())?;
        let now = Utc::now();
        let journal = TransactionJournal {
            schema_version: STATE_SCHEMA_VERSION,
            transaction_id: now.format("%Y%m%dT%H%M%S%.9fZ").to_string(),
            operation,
            phase: TransactionPhase::Prepared,
            started_at: now.timestamp(),
            source_payload,
            target_directory,
            previous_state,
            error: None,
        };
        write_json_atomic(&self.paths.state_root.join(JOURNAL_FILE), &journal)?;
        Ok(journal)
    }

    fn advance(
        &self,
        journal: &mut TransactionJournal,
        phase: TransactionPhase,
    ) -> Result<(), String> {
        journal.phase = phase;
        write_json_atomic(&self.paths.state_root.join(JOURNAL_FILE), journal)
    }

    fn rollback(&self, journal: &mut TransactionJournal) -> Result<(), String> {
        self.advance(journal, TransactionPhase::RollingBack)?;
        if let Some(previous) = &journal.previous_state {
            let backup = self
                .paths
                .state_root
                .join("Backup")
                .join(&journal.transaction_id);
            if backup.exists() {
                remove_owned_files(&journal.target_directory, &previous.manifest)?;
                copy_manifest_files(
                    &backup,
                    &previous.options.install_directory,
                    &previous.manifest,
                )?;
                let uninstaller = self.paths.state_root.join("Wolfmark-Uninstall.exe");
                integration::apply(
                    &previous.options,
                    &previous.manifest.build.version,
                    &previous.manifest.build.build_id,
                    &uninstaller,
                )?;
                write_json_atomic(&self.paths.state_root.join(STATE_FILE), previous)?;
            }
        } else {
            let _ = integration::remove();
        }
        self.advance(journal, TransactionPhase::RolledBack)
    }

    fn result(
        &self,
        operation: Operation,
        changed: bool,
        message: String,
    ) -> Result<OperationResult, String> {
        fs::create_dir_all(&self.paths.log_root).map_err(|error| error.to_string())?;
        let log_path = self.paths.log_root.join(format!(
            "{}-{}.log",
            Utc::now().format("%Y%m%d-%H%M%S"),
            format!("{operation:?}").to_lowercase()
        ));
        let mut log = File::create(&log_path).map_err(|error| error.to_string())?;
        writeln!(
            log,
            "operation={operation:?}\nchanged={changed}\nmessage={message}"
        )
        .map_err(|error| error.to_string())?;
        Ok(OperationResult {
            operation,
            changed,
            message,
            log_path,
        })
    }
}

fn compare_versions(left: &str, right: &str) -> std::cmp::Ordering {
    match (semver_like(left), semver_like(right)) {
        (Some(left), Some(right)) => left.cmp(&right),
        _ => left.cmp(right),
    }
}

fn semver_like(value: &str) -> Option<(u64, u64, u64, u64)> {
    let (base, dev) = value.split_once("-dev.").unwrap_or((value, "0"));
    let mut parts = base.split('.').map(str::parse::<u64>);
    Some((
        parts.next()?.ok()?,
        parts.next()?.ok()?,
        parts.next()?.ok()?,
        dev.parse().ok()?,
    ))
}

fn copy_one(source: &Path, target: &Path) -> Result<(), String> {
    if let Some(parent) = target.parent() {
        fs::create_dir_all(parent).map_err(|error| error.to_string())?;
    }
    let temporary = target.with_extension("wolfmark-new");
    fs::copy(source, &temporary)
        .map_err(|error| format!("copying {} failed: {error}", source.display()))?;
    if target.exists() {
        fs::remove_file(target)
            .map_err(|error| format!("replacing {} failed: {error}", target.display()))?;
    }
    fs::rename(&temporary, target)
        .map_err(|error| format!("committing {} failed: {error}", target.display()))?;
    Ok(())
}

fn copy_manifest_files(
    source: &Path,
    target: &Path,
    manifest: &PayloadManifest,
) -> Result<(), String> {
    for file in &manifest.files {
        copy_one(&source.join(&file.path), &target.join(&file.path))?;
    }
    Ok(())
}

fn copy_existing_manifest_files(
    source: &Path,
    target: &Path,
    manifest: &PayloadManifest,
) -> Result<(), String> {
    for file in &manifest.files {
        let source_file = source.join(&file.path);
        if source_file.exists() {
            copy_one(&source_file, &target.join(&file.path))?;
        }
    }
    Ok(())
}

fn remove_owned_files(root: &Path, manifest: &PayloadManifest) -> Result<(), String> {
    for file in &manifest.files {
        let path = root.join(&file.path);
        if path.exists() {
            fs::remove_file(&path)
                .map_err(|error| format!("removing {} failed: {error}", path.display()))?;
        }
    }
    Ok(())
}

fn remove_empty_directories(root: &Path) -> Result<(), String> {
    if !root.exists() {
        return Ok(());
    }
    let mut directories = WalkDir::new(root)
        .min_depth(1)
        .into_iter()
        .filter_map(Result::ok)
        .filter(|entry| entry.file_type().is_dir())
        .map(|entry| entry.into_path())
        .collect::<Vec<_>>();
    directories.sort_by_key(|path| std::cmp::Reverse(path.components().count()));
    for directory in directories {
        let _ = fs::remove_dir(directory);
    }
    let _ = fs::remove_dir(root);
    Ok(())
}

fn read_json_optional<T: serde::de::DeserializeOwned>(path: &Path) -> Result<Option<T>, String> {
    if !path.exists() {
        return Ok(None);
    }
    let bytes =
        fs::read(path).map_err(|error| format!("reading {} failed: {error}", path.display()))?;
    serde_json::from_slice(&bytes)
        .map(Some)
        .map_err(|error| format!("parsing {} failed: {error}", path.display()))
}

fn write_json_atomic(path: &Path, value: &impl Serialize) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent).map_err(|error| error.to_string())?;
    }
    let temporary = path.with_extension("json.new");
    let bytes = serde_json::to_vec_pretty(value).map_err(|error| error.to_string())?;
    fs::write(&temporary, bytes).map_err(|error| error.to_string())?;
    if path.exists() {
        fs::remove_file(path).map_err(|error| error.to_string())?;
    }
    fs::rename(&temporary, path).map_err(|error| error.to_string())
}

#[cfg(test)]
mod tests {
    use std::fs;

    use tempfile::tempdir;

    use super::*;
    use crate::{BuildIdentity, build_payload_manifest};

    fn identity(commit: &str, ancestors: &[&str]) -> BuildIdentity {
        BuildIdentity {
            product: "Wolfmark".into(),
            version: "0.1.0-dev.7".into(),
            source_commit: commit.into(),
            build_id: commit[..12].into(),
            source_timestamp: 1_700_000_000,
            architecture: "x86_64".into(),
            channel: "development".into(),
            dirty: false,
            ancestors: ancestors.iter().map(|value| (*value).into()).collect(),
        }
    }

    #[test]
    fn same_version_lineage_is_not_guessed_from_sha_order() {
        let a = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
        let b = "1111111111111111111111111111111111111111";
        let installed = identity(a, &[]);
        let candidate = identity(b, &[a]);
        assert!(candidate.ancestors.contains(&installed.source_commit));
    }

    #[test]
    fn exact_cache_tracks_same_manifest_as_install_payload() {
        let root = tempdir().unwrap();
        let payload = root.path().join("payload");
        fs::create_dir_all(&payload).unwrap();
        fs::write(payload.join("Wolfmark.exe"), b"binary").unwrap();
        let manifest = build_payload_manifest(
            &payload,
            identity("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", &[]),
        )
        .unwrap();
        let cache = root.path().join("cache");
        copy_manifest_files(&payload, &cache, &manifest).unwrap();
        assert!(verify_payload(&cache, &manifest).unwrap().is_empty());
    }
}
