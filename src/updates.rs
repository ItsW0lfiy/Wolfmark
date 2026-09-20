use std::fs::File;
use std::io::{BufReader, Read};
use std::path::Path;

use semver::Version;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

pub const INSTALLER_ASSET: &str = "Wolfmark-Setup-win-x64.exe";
pub const PORTABLE_ASSET: &str = "Wolfmark-portable-win-x64.zip";
pub const CHECKSUM_ASSET: &str = "SHA256SUMS.txt";

#[derive(Debug, Deserialize)]
struct GitHubRelease {
    draft: bool,
    prerelease: bool,
    tag_name: String,
    name: Option<String>,
    body: Option<String>,
    html_url: String,
    #[serde(default)]
    assets: Vec<GitHubAsset>,
}

#[derive(Debug, Deserialize)]
struct GitHubAsset {
    name: String,
    browser_download_url: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SelectedRelease {
    pub version: String,
    pub tag_name: String,
    pub name: String,
    pub body: String,
    pub html_url: String,
    pub installer_url: Option<String>,
    pub portable_url: Option<String>,
    pub checksums_url: Option<String>,
}

pub fn select_release(
    releases_json: &[u8],
    current_version: &str,
    include_prereleases: bool,
) -> Result<Option<SelectedRelease>, String> {
    let current = Version::parse(current_version)
        .map_err(|error| format!("Wolfmark's current version is invalid: {error}"))?;
    let releases: Vec<GitHubRelease> = serde_json::from_slice(releases_json)
        .map_err(|error| format!("GitHub returned malformed release metadata: {error}"))?;

    let mut candidates = releases
        .into_iter()
        .filter(|release| !release.draft && (include_prereleases || !release.prerelease))
        .filter_map(|release| {
            Version::parse(&release.tag_name)
                .ok()
                .map(|version| (version, release))
        })
        .filter(|(version, _)| version > &current)
        .collect::<Vec<_>>();
    candidates.sort_by(|left, right| right.0.cmp(&left.0));
    let Some((version, release)) = candidates.into_iter().next() else {
        return Ok(None);
    };

    let asset_url = |name: &str| {
        release
            .assets
            .iter()
            .find(|asset| asset.name == name)
            .map(|asset| asset.browser_download_url.clone())
    };
    Ok(Some(SelectedRelease {
        version: version.to_string(),
        tag_name: release.tag_name,
        name: release.name.unwrap_or_default(),
        body: release.body.unwrap_or_default(),
        html_url: release.html_url,
        installer_url: asset_url(INSTALLER_ASSET),
        portable_url: asset_url(PORTABLE_ASSET),
        checksums_url: asset_url(CHECKSUM_ASSET),
    }))
}

pub fn verify_file_checksum(
    file_path: &Path,
    manifest: &[u8],
    asset_name: &str,
) -> Result<(), String> {
    let expected = checksum_for_asset(manifest, asset_name)?;
    let file = File::open(file_path)
        .map_err(|error| format!("Could not open the downloaded update: {error}"))?;
    let mut reader = BufReader::new(file);
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let count = reader
            .read(&mut buffer)
            .map_err(|error| format!("Could not read the downloaded update: {error}"))?;
        if count == 0 {
            break;
        }
        hasher.update(&buffer[..count]);
    }
    let actual = format!("{:x}", hasher.finalize());
    if actual != expected {
        return Err("The downloaded update failed SHA-256 verification.".into());
    }
    Ok(())
}

fn checksum_for_asset(manifest: &[u8], asset_name: &str) -> Result<String, String> {
    let text = std::str::from_utf8(manifest)
        .map_err(|_| "SHA256SUMS.txt is not valid UTF-8.".to_owned())?;
    for line in text.lines() {
        let Some((hash, file_name)) = line.split_once(char::is_whitespace) else {
            continue;
        };
        let file_name = file_name.trim_start().trim_start_matches('*');
        if file_name == asset_name {
            let hash = hash.to_ascii_lowercase();
            if hash.len() == 64 && hash.bytes().all(|byte| byte.is_ascii_hexdigit()) {
                return Ok(hash);
            }
            return Err(format!(
                "SHA256SUMS.txt contains an invalid hash for {asset_name}."
            ));
        }
    }
    Err(format!("SHA256SUMS.txt does not contain {asset_name}."))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn releases() -> Vec<u8> {
        serde_json::to_vec(&serde_json::json!([
            {
                "draft": true,
                "prerelease": false,
                "tag_name": "9.0.0",
                "name": "Draft",
                "body": "",
                "html_url": "https://github.com/ItsW0lfiy/Moonmark/releases/tag/9.0.0",
                "assets": []
            },
            {
                "draft": false,
                "prerelease": true,
                "tag_name": "0.1.0-dev.10",
                "name": "Development 10",
                "body": "Notes",
                "html_url": "https://github.com/ItsW0lfiy/Moonmark/releases/tag/0.1.0-dev.10",
                "assets": [
                    {"name": INSTALLER_ASSET, "browser_download_url": "https://github.com/setup"},
                    {"name": PORTABLE_ASSET, "browser_download_url": "https://github.com/portable"},
                    {"name": CHECKSUM_ASSET, "browser_download_url": "https://github.com/checksums"}
                ]
            },
            {
                "draft": false,
                "prerelease": true,
                "tag_name": "0.1.0-dev.9",
                "name": null,
                "body": null,
                "html_url": "https://github.com/ItsW0lfiy/Moonmark/releases/tag/0.1.0-dev.9",
                "assets": []
            },
            {
                "draft": false,
                "prerelease": false,
                "tag_name": "0.1.0",
                "name": "Stable",
                "body": "",
                "html_url": "https://github.com/ItsW0lfiy/Moonmark/releases/tag/0.1.0",
                "assets": []
            },
            {
                "draft": false,
                "prerelease": false,
                "tag_name": "not-a-version",
                "name": "Malformed",
                "body": "",
                "html_url": "https://example.invalid",
                "assets": []
            }
        ]))
        .unwrap()
    }

    #[test]
    fn filters_drafts_and_prereleases_and_uses_semver_ordering() {
        let stable = select_release(&releases(), "0.1.0-dev.7", false)
            .unwrap()
            .unwrap();
        assert_eq!(stable.version, "0.1.0");

        let prerelease = select_release(&releases(), "0.1.0-dev.7", true)
            .unwrap()
            .unwrap();
        assert_eq!(prerelease.version, "0.1.0");

        let development_only = select_release(&releases(), "0.1.0-dev.8", true)
            .unwrap()
            .unwrap();
        assert_eq!(development_only.version, "0.1.0");
        assert!(Version::parse("0.1.0-dev.10").unwrap() > Version::parse("0.1.0-dev.9").unwrap());
        assert!(Version::parse("0.1.0").unwrap() > Version::parse("0.1.0-dev.10").unwrap());
    }

    #[test]
    fn exposes_only_exact_expected_assets() {
        let development_releases = serde_json::to_vec(&serde_json::json!([{
            "draft": false,
            "prerelease": true,
            "tag_name": "0.1.0-dev.10",
            "name": "Development 10",
            "body": "Notes",
            "html_url": "https://github.com/ItsW0lfiy/Moonmark/releases/tag/0.1.0-dev.10",
            "assets": [
                {"name": INSTALLER_ASSET, "browser_download_url": "https://github.com/setup"},
                {"name": PORTABLE_ASSET, "browser_download_url": "https://github.com/portable"},
                {"name": CHECKSUM_ASSET, "browser_download_url": "https://github.com/checksums"}
            ]
        }]))
        .unwrap();
        let selected = select_release(&development_releases, "0.1.0-dev.9", true)
            .unwrap()
            .unwrap();
        assert_eq!(selected.version, "0.1.0-dev.10");
        assert_eq!(
            selected.installer_url.as_deref(),
            Some("https://github.com/setup")
        );
        assert_eq!(
            selected.portable_url.as_deref(),
            Some("https://github.com/portable")
        );
        assert_eq!(
            selected.checksums_url.as_deref(),
            Some("https://github.com/checksums")
        );

        let missing = select_release(&releases(), "0.0.1", false)
            .unwrap()
            .unwrap();
        assert!(missing.installer_url.is_none());
        assert!(missing.checksums_url.is_none());
    }

    #[test]
    fn malformed_metadata_is_safe() {
        assert!(select_release(b"not json", "0.1.0-dev.7", true).is_err());
        assert!(
            select_release(b"[]", "0.1.0-dev.7", true)
                .unwrap()
                .is_none()
        );
    }

    #[test]
    fn checksum_manifest_requires_exact_asset_and_hash() {
        let root =
            std::env::temp_dir().join(format!("wolfmark-update-test-{}", std::process::id()));
        std::fs::create_dir_all(&root).unwrap();
        let file = root.join(INSTALLER_ASSET);
        std::fs::write(&file, b"wolfmark").unwrap();
        let good = b"a0e36022b25c053fc72dbf43ebf04c188a42a15b57d24f53fe8f84ab9f7835a4 *Wolfmark-Setup-win-x64.exe\n";
        verify_file_checksum(&file, good, INSTALLER_ASSET).unwrap();
        assert!(verify_file_checksum(&file, good, PORTABLE_ASSET).is_err());
        let bad = b"0000000000000000000000000000000000000000000000000000000000000000 *Wolfmark-Setup-win-x64.exe\n";
        assert!(verify_file_checksum(&file, bad, INSTALLER_ASSET).is_err());
        std::fs::remove_dir_all(root).unwrap();
    }
}
