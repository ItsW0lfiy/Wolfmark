use std::fs::File;
use std::io::{BufReader, Read};
use std::path::{Component, Path};

use sha2::{Digest, Sha256};
use walkdir::WalkDir;

use crate::{BuildIdentity, MANIFEST_SCHEMA_VERSION, PayloadFile, PayloadManifest};

pub fn hash_file(path: &Path) -> std::io::Result<String> {
    let mut reader = BufReader::new(File::open(path)?);
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let read = reader.read(&mut buffer)?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
    }
    Ok(hex::encode(hasher.finalize()))
}

pub fn build_payload_manifest(
    payload_root: &Path,
    build: BuildIdentity,
) -> Result<PayloadManifest, String> {
    build.validate()?;
    let mut files = Vec::new();
    for entry in WalkDir::new(payload_root).follow_links(false) {
        let entry = entry.map_err(|error| error.to_string())?;
        if !entry.file_type().is_file() {
            continue;
        }
        let relative = entry
            .path()
            .strip_prefix(payload_root)
            .map_err(|error| error.to_string())?;
        if relative.components().any(|component| {
            matches!(
                component,
                Component::ParentDir | Component::RootDir | Component::Prefix(_)
            )
        }) {
            return Err(format!("unsafe payload path: {}", relative.display()));
        }
        let path = relative.to_string_lossy().replace('\\', "/");
        if path == "payload-manifest.json" || path == "portable.flag" {
            continue;
        }
        files.push(PayloadFile {
            path,
            size: entry.metadata().map_err(|error| error.to_string())?.len(),
            sha256: hash_file(entry.path()).map_err(|error| error.to_string())?,
        });
    }
    files.sort_by(|left, right| left.path.cmp(&right.path));
    Ok(PayloadManifest {
        schema_version: MANIFEST_SCHEMA_VERSION,
        build,
        files,
    })
}

pub fn verify_payload(root: &Path, manifest: &PayloadManifest) -> Result<Vec<String>, String> {
    if manifest.schema_version != MANIFEST_SCHEMA_VERSION {
        return Err(format!(
            "unsupported payload manifest schema {}",
            manifest.schema_version
        ));
    }
    manifest.build.validate()?;
    let mut damaged = Vec::new();
    for file in &manifest.files {
        let relative = Path::new(&file.path);
        if relative.components().any(|component| {
            matches!(
                component,
                Component::ParentDir | Component::RootDir | Component::Prefix(_)
            )
        }) {
            return Err(format!("unsafe manifest path: {}", file.path));
        }
        let target = root.join(relative);
        let metadata = match target.metadata() {
            Ok(metadata) if metadata.is_file() => metadata,
            _ => {
                damaged.push(file.path.clone());
                continue;
            }
        };
        if metadata.len() != file.size
            || hash_file(&target).map_err(|error| error.to_string())? != file.sha256
        {
            damaged.push(file.path.clone());
        }
    }
    Ok(damaged)
}

#[cfg(test)]
mod tests {
    use std::fs;

    use tempfile::tempdir;

    use super::*;

    fn identity() -> BuildIdentity {
        BuildIdentity {
            product: "Wolfmark".into(),
            version: "0.1.0-dev.7".into(),
            source_commit: "0123456789abcdef0123456789abcdef01234567".into(),
            build_id: "0123456789ab".into(),
            source_timestamp: 1_700_000_000,
            architecture: "x86_64".into(),
            channel: "development".into(),
            dirty: false,
            ancestors: Vec::new(),
        }
    }

    #[test]
    fn manifest_is_sorted_and_detects_damage() {
        let root = tempdir().unwrap();
        fs::write(root.path().join("z.txt"), b"last").unwrap();
        fs::create_dir(root.path().join("nested")).unwrap();
        fs::write(root.path().join("nested/a.txt"), b"first").unwrap();
        let manifest = build_payload_manifest(root.path(), identity()).unwrap();
        assert_eq!(manifest.files[0].path, "nested/a.txt");
        assert!(verify_payload(root.path(), &manifest).unwrap().is_empty());
        fs::write(root.path().join("nested/a.txt"), b"changed").unwrap();
        assert_eq!(
            verify_payload(root.path(), &manifest).unwrap(),
            ["nested/a.txt"]
        );
    }

    #[test]
    fn portable_marker_and_manifest_do_not_own_themselves() {
        let root = tempdir().unwrap();
        fs::write(root.path().join("Wolfmark.exe"), b"exe").unwrap();
        fs::write(root.path().join("portable.flag"), b"").unwrap();
        fs::write(root.path().join("payload-manifest.json"), b"{}").unwrap();
        let manifest = build_payload_manifest(root.path(), identity()).unwrap();
        assert_eq!(manifest.files.len(), 1);
        assert_eq!(manifest.files[0].path, "Wolfmark.exe");
    }
}
