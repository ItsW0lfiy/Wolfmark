use std::path::{Component, Path, PathBuf};

use percent_encoding::percent_decode_str;
use url::Url;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AssetState {
    Allowed,
    Missing,
    Blocked,
    Remote,
    Unsupported,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct AssetResolution {
    pub state: AssetState,
    pub canonical_path: Option<PathBuf>,
    pub display_path: String,
    pub message: String,
}

pub fn resolve_local_image(document_path: &Path, reference: &str) -> AssetResolution {
    let root = document_path.parent().unwrap_or_else(|| Path::new("."));
    let root = std::fs::canonicalize(root).unwrap_or_else(|_| normalize(root));
    let decoded = percent_decode_str(reference)
        .decode_utf8_lossy()
        .replace('\\', "/");
    let decoded_path = PathBuf::from(decoded.as_str());

    // Windows drive and UNC paths contain colon/slash forms that URL parsers can
    // mistake for custom schemes, so recognize native absolute paths first.
    if decoded_path.is_absolute() {
        return resolve_path(decoded_path, reference);
    }

    if let Ok(url) = Url::parse(&decoded) {
        if matches!(url.scheme(), "http" | "https") {
            return resolution(
                AssetState::Remote,
                None,
                reference,
                "Remote images are disabled",
            );
        }
        if url.scheme() == "file" {
            if let Ok(path) = url.to_file_path() {
                return resolve_path(path, reference);
            }
            return resolution(
                AssetState::Blocked,
                None,
                reference,
                "Invalid local file URI",
            );
        }
        if !url.scheme().is_empty() {
            return resolution(
                AssetState::Blocked,
                None,
                reference,
                "Unsupported image scheme",
            );
        }
    }

    let candidate = root.join(decoded_path);
    resolve_path(candidate, reference)
}

fn resolve_path(candidate: PathBuf, original: &str) -> AssetResolution {
    let normalized = normalize(&candidate);
    let canonical = std::fs::canonicalize(&normalized);
    let checked = canonical.as_deref().unwrap_or(&normalized);
    if canonical.is_err() || !checked.is_file() {
        return resolution(AssetState::Missing, None, original, "Image not found");
    }
    let extension = checked
        .extension()
        .and_then(|value| value.to_str())
        .unwrap_or_default()
        .to_ascii_lowercase();
    if !matches!(
        extension.as_str(),
        "png" | "jpg" | "jpeg" | "gif" | "webp" | "svg"
    ) {
        return resolution(
            AssetState::Unsupported,
            None,
            original,
            "Unsupported image format",
        );
    }
    AssetResolution {
        state: AssetState::Allowed,
        canonical_path: canonical.ok(),
        display_path: original.replace('\\', "/"),
        message: String::new(),
    }
}

fn resolution(
    state: AssetState,
    path: Option<PathBuf>,
    display: &str,
    message: &str,
) -> AssetResolution {
    AssetResolution {
        state,
        canonical_path: path,
        display_path: display.replace('\\', "/"),
        message: message.to_owned(),
    }
}

fn normalize(path: &Path) -> PathBuf {
    let mut normalized = PathBuf::new();
    for component in path.components() {
        match component {
            Component::CurDir => {}
            Component::ParentDir => {
                normalized.pop();
            }
            other => normalized.push(other.as_os_str()),
        }
    }
    normalized
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    #[test]
    fn resolves_spaces_unicode_encoding_and_windows_separators() {
        let root = std::env::temp_dir().join(format!("wolfmark-paths-{}", std::process::id()));
        let images = root.join("images");
        fs::create_dir_all(&images).unwrap();
        fs::write(images.join("moon ü.png"), b"x").unwrap();
        let document = root.join("document.md");
        fs::write(&document, b"").unwrap();
        for reference in [
            "images/moon ü.png",
            "images/moon%20%C3%BC.png",
            "images\\moon ü.png",
        ] {
            assert_eq!(
                resolve_local_image(&document, reference).state,
                AssetState::Allowed
            );
        }
        fs::remove_dir_all(root).unwrap();
    }

    #[test]
    fn allows_explicit_parent_absolute_and_file_uri_paths() {
        let base = std::env::temp_dir().join(format!("wolfmark-policy-{}", std::process::id()));
        let root = base.join("document");
        let shared = base.join("shared");
        fs::create_dir_all(&root).unwrap();
        fs::create_dir_all(&shared).unwrap();
        let document = root.join("document.md");
        let image = shared.join("outside.png");
        fs::write(&document, b"").unwrap();
        fs::write(&image, b"fixture").unwrap();

        let file_uri = Url::from_file_path(&image).unwrap();
        for reference in [
            "../shared/outside.png".to_owned(),
            image.to_string_lossy().into_owned(),
            file_uri.to_string(),
        ] {
            assert_eq!(
                resolve_local_image(&document, &reference).state,
                AssetState::Allowed
            );
        }
        assert_eq!(
            resolve_local_image(&document, "https://example.invalid/a.png").state,
            AssetState::Remote
        );
        fs::remove_dir_all(base).unwrap();
    }
}
