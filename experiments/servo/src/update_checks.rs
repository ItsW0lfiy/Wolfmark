// SPDX-License-Identifier: GPL-3.0-only
//! Explicitly permitted background GitHub release metadata checks.
//! This is NOT a general network API for documents or Servo.
use std::io::Read;
use std::time::Duration;

pub const CURRENT_VERSION: &str = "0.1.0-dev.7";
const GITHUB_RELEASES: &str =
    "https://api.github.com/repos/ItsW0lfiy/Moonmark/releases?per_page=30";
const MAX_RESPONSE: u64 = 3 * 1024 * 1024;

#[derive(Debug)]
pub struct Report {
    pub message: String,
    pub release_page: Option<String>,
}

pub fn check(include_prereleases: bool) -> Result<Report, String> {
    // The single preconfigured endpoint cannot be overridden by Markdown,
    // UI links, file metadata or a remote document. No redirects are followed.
    let agent = ureq::AgentBuilder::new()
        .timeout(Duration::from_secs(15))
        .redirects(0)
        .build();
    let response = agent.get(GITHUB_RELEASES)
        .set("Accept", "application/vnd.github+json")
        .set("X-GitHub-Api-Version", "2022-11-28")
        .set("User-Agent", "Wolfmark/0.1.0-dev.7")
        .call()
        .map_err(|error| match error {
            ureq::Error::Status(403 | 429, _) =>
                "GitHub temporarily limited update checks. Try again later.".to_owned(),
            ureq::Error::Status(code, _) =>
                format!("GitHub returned HTTP {code} while checking for updates."),
            _ => "Could not reach the GitHub Releases API.".to_owned(),
        })?;
    let mut bytes = Vec::new();
    response.into_reader().take(MAX_RESPONSE + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| "Could not read GitHub's update response.".to_owned())?;
    if bytes.len() as u64 > MAX_RESPONSE {
        return Err("GitHub release metadata exceeded the safe size limit.".into());
    }
    match crate::updates::select_release(&bytes, CURRENT_VERSION, include_prereleases)? {
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn endpoint_is_fixed_https_github_and_bounded() {
        let url = url::Url::parse(GITHUB_RELEASES).unwrap();
        assert_eq!(url.scheme(), "https");
        assert_eq!(url.host_str(), Some("api.github.com"));
        assert_eq!(MAX_RESPONSE, 3 * 1024 * 1024);
    }
}
