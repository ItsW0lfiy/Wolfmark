// SPDX-License-Identifier: GPL-3.0-only
//! Shared Qt-compatible user preferences for the Servo frontend.
//! Format and keys intentionally match native/qt/wolfmark_settings.cpp.
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use serde_json::{Value, json};

#[derive(Clone, Debug)]
pub struct Preferences {
    pub editor_word_wrap: bool,
    pub check_on_startup: bool,
    pub include_prereleases: bool,
    pub last_open_directory: String,
    path: PathBuf,
    pub portable: bool,
    pub recovery_warning: Option<String>,
}

impl Default for Preferences {
    fn default() -> Self {
        Self::at_path(default_settings_path(), false)
    }
}

impl Preferences {
    pub fn at_path(path: PathBuf, portable: bool) -> Self {
        Self {
            editor_word_wrap: true,
            check_on_startup: true,
            // Match Qt's prerelease default for the development channel.
            include_prereleases: true,
            last_open_directory: String::new(),
            path,
            portable,
            recovery_warning: None,
        }
    }

    pub fn load() -> Self {
        let (path, portable) = settings_path();
        Self::load_from(path, portable)
    }

    pub fn load_from(path: PathBuf, portable: bool) -> Self {
        let mut settings = Self::at_path(path, portable);
        let bytes = match std::fs::read(&settings.path) {
            Ok(bytes) => bytes,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => return settings,
            Err(error) => {
                settings.recovery_warning = Some(format!("Could not read settings.json: {error}"));
                return settings;
            }
        };
        let root: Value = match serde_json::from_slice::<Value>(&bytes) {
            Ok(value) if value.is_object() => value,
            _ => {
                let timestamp = SystemTime::now()
                    .duration_since(UNIX_EPOCH)
                    .map_or(0, |time| time.as_millis());
                let invalid = settings.path.with_file_name(format!("settings.invalid-{timestamp}.json"));
                match std::fs::rename(&settings.path, &invalid) {
                    Ok(()) => settings.recovery_warning = Some(format!(
                        "Invalid settings were preserved as {}. Defaults are active.",
                        invalid.file_name().unwrap_or_default().to_string_lossy()
                    )),
                    Err(_) => {
                        if std::fs::copy(&settings.path, &invalid).is_ok() {
                            settings.recovery_warning = Some(format!(
                                "Invalid settings were preserved as {}. Defaults are active.",
                                invalid.file_name().unwrap_or_default().to_string_lossy()
                            ));
                        } else {
                            settings.recovery_warning = Some(
                                "Invalid settings.json could not be backed up. Defaults are active.".into()
                            );
                        }
                    }
                }
                return settings;
            }
        };
        if let Some(value) = root.pointer("/editor/wordWrap").and_then(Value::as_bool) {
            settings.editor_word_wrap = value;
        }
        if let Some(value) = root.pointer("/updates/checkOnStartup").and_then(Value::as_bool) {
            settings.check_on_startup = value;
        }
        if let Some(value) = root.pointer("/updates/includePrereleases").and_then(Value::as_bool) {
            settings.include_prereleases = value;
        }
        if let Some(value) = root.pointer("/files/lastOpenDirectory").and_then(Value::as_str) {
            settings.last_open_directory = value.to_owned();
        }
        settings
    }

    pub fn path(&self) -> &Path { &self.path }

    pub fn save(&self) -> Result<(), String> {
        let parent = self.path.parent().ok_or("Settings path has no parent")?;
        std::fs::create_dir_all(parent).map_err(|err| format!("Unable to create settings directory: {err}"))?;
        let mut data = match std::fs::read(&self.path) {
            Ok(bytes) => serde_json::from_slice::<Value>(&bytes)
                .unwrap_or_else(|_| json!({})),
            Err(_) => json!({}),
        };
        if !data.is_object() { data = json!({}); }
        let object = data.as_object_mut().expect("object checked");
        object.insert("schemaVersion".into(), json!(2));
        let updates = object.entry("updates").or_insert_with(|| json!({}));
        if !updates.is_object() { *updates = json!({}); }
        updates["checkOnStartup"] = json!(self.check_on_startup);
        updates["includePrereleases"] = json!(self.include_prereleases);
        let editor = object.entry("editor").or_insert_with(|| json!({}));
        if !editor.is_object() { *editor = json!({}); }
        editor["wordWrap"] = json!(self.editor_word_wrap);
        let files = object.entry("files").or_insert_with(|| json!({}));
        if !files.is_object() { *files = json!({}); }
        files["lastOpenDirectory"] = json!(self.last_open_directory);

        let bytes = serde_json::to_vec_pretty(&data).map_err(|err| err.to_string())?;
        let mut temp = tempfile::NamedTempFile::new_in(parent)
            .map_err(|err| format!("Unable to prepare settings save: {err}"))?;
        use std::io::Write;
        temp.write_all(&bytes).map_err(|err| err.to_string())?;
        temp.as_file().sync_all().map_err(|err| err.to_string())?;
        temp.persist(&self.path)
            .map_err(|err| format!("Unable to replace settings.json: {}", err.error))?;
        Ok(())
    }
}

fn default_settings_path() -> PathBuf { settings_path().0 }

fn settings_path() -> (PathBuf, bool) {
    if let Some(root) = std::env::var_os("WOLFMARK_SETTINGS_ROOT").filter(|v| !v.is_empty()) {
        return (PathBuf::from(root).join("settings.json"), false);
    }
    if let Ok(exe) = std::env::current_exe()
        && let Some(dir) = exe.parent()
        && dir.join("portable.flag").exists()
    {
        return (dir.join("data").join("settings.json"), true);
    }
    #[cfg(target_os = "windows")]
    {
        if let Some(appdata) = std::env::var_os("APPDATA") {
            return (PathBuf::from(appdata).join("Wolfmark").join("settings.json"), false);
        }
    }
    #[cfg(target_os = "linux")]
    {
        if let Some(xdg) = std::env::var_os("XDG_CONFIG_HOME").filter(|v| !v.is_empty()) {
            return (PathBuf::from(xdg).join("Wolfmark").join("settings.json"), false);
        }
        if let Some(home) = std::env::var_os("HOME") {
            return (PathBuf::from(home).join(".config").join("Wolfmark").join("settings.json"), false);
        }
    }
    (std::env::temp_dir().join("Wolfmark").join("settings.json"), false)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn qt_preferences_round_trip_and_keep_other_keys() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("settings.json");
        std::fs::write(&path, r#"{"schemaVersion":2,"updates":{"checkOnStartup":false,"extra":7},"editor":{"wordWrap":false},"files":{"lastOpenDirectory":"C:\\docs"},"custom":{"a":true}}"#).unwrap();
        let mut settings = Preferences::load_from(path.clone(), false);
        assert!(!settings.check_on_startup);
        assert!(!settings.editor_word_wrap);
        assert_eq!(settings.last_open_directory, r"C:\docs");
        settings.include_prereleases = false;
        settings.save().unwrap();
        let saved: Value = serde_json::from_slice(&std::fs::read(&path).unwrap()).unwrap();
        assert_eq!(saved.pointer("/custom/a").and_then(Value::as_bool), Some(true));
        assert_eq!(saved.pointer("/updates/extra").and_then(Value::as_u64), Some(7));
        let loaded = Preferences::load_from(path, false);
        assert!(!loaded.include_prereleases);
    }

    #[test]
    fn malformed_json_is_preserved_before_defaults() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("settings.json");
        std::fs::write(&path, "{broken").unwrap();
        let settings = Preferences::load_from(path.clone(), false);
        assert!(settings.recovery_warning.is_some());
        assert!(settings.editor_word_wrap);
        assert!(!path.exists());
        assert!(std::fs::read_dir(dir.path()).unwrap().filter_map(Result::ok).any(|entry|
            entry.file_name().to_string_lossy().starts_with("settings.invalid-")));
    }
}
