use std::path::PathBuf;

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BuildIdentity {
    pub product: String,
    pub version: String,
    pub source_commit: String,
    pub build_id: String,
    pub source_timestamp: i64,
    pub architecture: String,
    pub channel: String,
    pub dirty: bool,
    #[serde(default)]
    pub ancestors: Vec<String>,
}

impl BuildIdentity {
    pub fn validate(&self) -> Result<(), String> {
        if self.product != "Wolfmark" {
            return Err(format!("unexpected product identity: {}", self.product));
        }
        if self.source_commit.len() != 40
            || !self
                .source_commit
                .bytes()
                .all(|byte| byte.is_ascii_hexdigit())
        {
            return Err("sourceCommit must be a full 40-character Git object id".into());
        }
        let expected = if self.dirty {
            format!("{}-dirty", &self.source_commit[..12])
        } else {
            self.source_commit[..12].to_owned()
        };
        if self.build_id != expected {
            return Err(format!("buildId must be {expected}"));
        }
        if self.architecture != "x86_64" {
            return Err(format!("unsupported architecture: {}", self.architecture));
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PayloadFile {
    pub path: String,
    pub size: u64,
    pub sha256: String,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PayloadManifest {
    pub schema_version: u32,
    pub build: BuildIdentity,
    pub files: Vec<PayloadFile>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct InstallOptions {
    pub install_directory: PathBuf,
    pub file_associations: bool,
    pub desktop_shortcut: bool,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct InstallState {
    pub schema_version: u32,
    pub manifest: PayloadManifest,
    pub options: InstallOptions,
    pub installed_at: i64,
    pub updated_at: i64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum Operation {
    Install,
    Update,
    Repair,
    Reinstall,
    Modify,
    Uninstall,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum TransactionPhase {
    Prepared,
    PayloadStaged,
    ExistingBackedUp,
    PayloadApplied,
    IntegrationApplied,
    Validated,
    Committed,
    RollingBack,
    RolledBack,
    Failed,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TransactionJournal {
    pub schema_version: u32,
    pub transaction_id: String,
    pub operation: Operation,
    pub phase: TransactionPhase,
    pub started_at: i64,
    pub source_payload: Option<PathBuf>,
    pub target_directory: PathBuf,
    pub previous_state: Option<InstallState>,
    pub error: Option<String>,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "kebab-case")]
pub enum InstallationStatus {
    Absent,
    Exact {
        build: BuildIdentity,
        damaged_files: Vec<String>,
    },
    Older {
        installed: BuildIdentity,
        candidate: BuildIdentity,
    },
    Newer {
        installed: BuildIdentity,
        candidate: BuildIdentity,
    },
    SameVersionInstalledAncestor {
        installed: BuildIdentity,
        candidate: BuildIdentity,
    },
    SameVersionCandidateAncestor {
        installed: BuildIdentity,
        candidate: BuildIdentity,
    },
    Diverged {
        installed: BuildIdentity,
        candidate: BuildIdentity,
    },
    DirtyBuild {
        build: BuildIdentity,
    },
    Damaged {
        build: BuildIdentity,
        damaged_files: Vec<String>,
    },
    Partial {
        details: String,
    },
    Inconsistent {
        details: String,
    },
    Interrupted {
        journal: TransactionJournal,
    },
}
