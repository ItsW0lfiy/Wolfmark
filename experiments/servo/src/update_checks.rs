// SPDX-License-Identifier: GPL-3.0-only
//! The only permitted Wolfmark UI network path: public GitHub Releases API.
//! No general web requests, localhost, redirects, document URLs or telemetry.
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};

pub const CURRENT_VERSION: &str = "0.1.0-dev.7";
const GITHUB_RELEASES: &str =
    "https://api.github.com/repos/ItsW0lfiy/Wolfmark/releases?per_page=30";
const MAX_RESPONSE: u64 = 3 * 1024 * 1024;
const CACHE_WINDOW: u64 = 6 * 60 * 60;

#[derive(Debug)]
pub struct Report {
    pub message: String,
    pub release_page: Option<String>,
}

#[derive(Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
struct Cache {
    etag: String,
    last_success: u64,
    releases: String,
}

impl Cache {
    fn load(path: &Path) -> Self {
        let Ok(metadata) = std::fs::metadata(path) else { return Self::default() };
        if metadata.len() > MAX_RESPONSE { return Self::default(); }
        let Ok(bytes) = std::fs::read(path) else { return Self::default() };
        serde_json::from_slice(&bytes).unwrap_or_default()
    }

    fn save(&self, path: &Path) {
        // Cache failure must never affect actual update detection or documents.
        let Some(parent) = path.parent() else { return };
        if std::fs::create_dir_all(parent).is_err() { return }
        let Ok(bytes) = serde_json::to_vec(self) else { return };
        let Ok(mut temporary) = tempfile::NamedTempFile::new_in(parent) else { return };
        if temporary.write_all(&bytes).is_ok() {
            let _ = temporary.persist(path);
        }
    }

    fn fresh(&self, now: u64) -> bool {
        !self.releases.is_empty() && now >= self.last_success
            && now - self.last_success < CACHE_WINDOW
    }
}

fn now_unix() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH)
        .map_or(0, |duration| duration.as_secs())
}

fn report_from(releases: &[u8], include_prereleases: bool) -> Result<Report, String> {
    match crate::updates::select_release(releases, CURRENT_VERSION, include_prereleases)? {
        Some(release) => Ok(Report {
            message: format!("Wolfmark {} is available.", release.version),
            release_page: Some(release.html_url),
        }),
        None => Ok(Report {
            message: "Wolfmark is up to date.".into(),
            release_page: None,
        }),
    }
}

fn canonical_cache_path(path: &Path) -> PathBuf {
    if let Some(directory) =
        std::env::var_os("WOLFMARK_UPDATE_STATE_ROOT").filter(|path| !path.is_empty())
    {
        PathBuf::from(directory).join("update-state.json")
    } else {
        path.to_path_buf()
    }
}

pub fn check(
    include_prereleases: bool,
    manual: bool,
    settings_cache_path: &Path,
) -> Result<Report, String> {
    let cache_path = canonical_cache_path(settings_cache_path);
    let mut cache = Cache::load(&cache_path);
    let now = now_unix();
    if !manual && cache.fresh(now) {
        if let Ok(result) = report_from(cache.releases.as_bytes(), include_prereleases) {
            return Ok(result);
        }
    }

    // The single preconfigured endpoint cannot be overridden by document
    // Markdown, UI links, environment, or untrusted metadata.
    let agent = ureq::AgentBuilder::new()
        .timeout(Duration::from_secs(15))
        .redirects(0)
        .build();
    let mut request = agent.get(GITHUB_RELEASES)
        .set("Accept", "application/vnd.github+json")
        .set("X-GitHub-Api-Version", "2022-11-28")
        .set("User-Agent", "Wolfmark/0.1.0-dev.7");
    if !cache.etag.is_empty() {
        request = request.set("If-None-Match", &cache.etag);
    }
    let response = match request.call() {
        Ok(response) => response,
        Err(ureq::Error::Status(304, _)) if !cache.releases.is_empty() => {
            cache.last_success = now;
            cache.save(&cache_path);
            return report_from(cache.releases.as_bytes(), include_prereleases);
        }
        Err(ureq::Error::Status(403 | 429, _)) =>
            return Err("GitHub temporarily limited update checks. Try again later.".into()),
        Err(ureq::Error::Status(code, _)) =>
            return Err(format!("GitHub returned HTTP {code} while checking for updates.")),
        Err(_) => return Err("Could not reach the GitHub Releases API.".into()),
    };
    let etag = response.header("ETag").unwrap_or_default().to_string();
    let mut bytes = Vec::new();
    response.into_reader().take(MAX_RESPONSE + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| "Could not read GitHub's update response.".to_owned())?;
    if bytes.len() as u64 > MAX_RESPONSE {
        return Err("GitHub release metadata exceeded the safe size limit.".into());
    }
    let result = report_from(&bytes, include_prereleases)?;
    if let Ok(releases) = String::from_utf8(bytes) {
        cache.releases = releases;
        cache.etag = etag;
        cache.last_success = now;
        cache.save(&cache_path);
    }
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn endpoint_is_canonical_fixed_https_github() {
        let url = url::Url::parse(GITHUB_RELEASES).unwrap();
        assert_eq!(url.scheme(), "https");
        assert_eq!(url.host_str(), Some("api.github.com"));
        assert!(url.path().contains("/ItsW0lfiy/Wolfmark/releases"));
    }

    #[test]
    fn cache_roundtrip_and_window() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("update-state.json");
        let now = now_unix();
        let cache = Cache {
            etag: "W/\"123\"".into(),
            last_success: now,
            releases: "[]".into(),
        };
        cache.save(&path);
        let loaded = Cache::load(&path);
        assert_eq!(loaded.etag, "W/\"123\"");
        assert!(loaded.fresh(now + 12));
        assert!(!loaded.fresh(now + CACHE_WINDOW + 1));
        assert_eq!(report_from(loaded.releases.as_bytes(), true).unwrap().message,
                   "Wolfmark is up to date.");
    }

    #[test]
    fn invalid_cache_is_safe() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("update-state.json");
        std::fs::write(&path, "{not json").unwrap();
        assert!(Cache::load(&path).releases.is_empty());
    }
}
