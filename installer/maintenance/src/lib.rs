mod engine;
mod integration;
mod manifest;
mod model;

pub use engine::{Engine, EnginePaths, OperationResult};
pub use manifest::{build_payload_manifest, hash_file, verify_payload};
pub use model::{
    BuildIdentity, InstallOptions, InstallState, InstallationStatus, Operation, PayloadFile,
    PayloadManifest, TransactionJournal, TransactionPhase,
};

pub const STATE_SCHEMA_VERSION: u32 = 1;
pub const MANIFEST_SCHEMA_VERSION: u32 = 1;
