#include "moonmark_settings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <utility>

namespace moonmark::qt {
namespace {

struct SettingsLocation {
    QString file_path;
    bool portable = false;
};

SettingsLocation settingsLocation() {
    const auto override_root = qEnvironmentVariable("MOONMARK_SETTINGS_ROOT");
    if (!override_root.isEmpty()) {
        return {QDir(override_root).filePath(QStringLiteral("settings.json")), false};
    }

    const auto application_dir = QCoreApplication::applicationDirPath();
    if (QFileInfo::exists(QDir(application_dir).filePath(QStringLiteral("portable.flag")))) {
        return {QDir(application_dir).filePath(QStringLiteral("data/settings.json")), true};
    }

    return {QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                .filePath(QStringLiteral("settings.json")),
            false};
}

QString preserveMalformedSettings(const QString& file_path) {
    const QFileInfo source(file_path);
    const auto backup_name = QStringLiteral("settings.invalid-%1.json")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    const auto backup_path = source.dir().filePath(backup_name);
    if (QFile::rename(file_path, backup_path)) return backup_path;
    if (QFile::copy(file_path, backup_path)) return backup_path;
    return {};
}

} // namespace

UserSettings::UserSettings(QString file_path, bool portable, bool include_prereleases_default)
    : file_path_(std::move(file_path)), portable_(portable) {
    updates_.include_prereleases = include_prereleases_default;
}

UserSettings UserSettings::load(bool include_prereleases_default) {
    const auto location = settingsLocation();
    UserSettings settings(location.file_path, location.portable, include_prereleases_default);
    QFile file(location.file_path);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            settings.recovery_warning_ = QStringLiteral("Moonmark could not read settings.json; defaults are active.");
            return settings;
        }
        QJsonParseError parse_error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
        file.close();
        if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
            const auto backup = preserveMalformedSettings(location.file_path);
            settings.recovery_warning_ = backup.isEmpty()
                ? QStringLiteral("Moonmark found malformed settings.json and used defaults.")
                : QStringLiteral("Moonmark preserved malformed settings as %1 and used defaults.")
                    .arg(QFileInfo(backup).fileName());
            return settings;
        }

        const auto root = document.object();
        const auto updates = root.value(QStringLiteral("updates")).toObject();
        const auto files = root.value(QStringLiteral("files")).toObject();
        if (updates.value(QStringLiteral("checkOnStartup")).isBool())
            settings.updates_.check_on_startup = updates.value(QStringLiteral("checkOnStartup")).toBool();
        if (updates.value(QStringLiteral("includePrereleases")).isBool())
            settings.updates_.include_prereleases = updates.value(QStringLiteral("includePrereleases")).toBool();
        if (files.value(QStringLiteral("lastOpenDirectory")).isString())
            settings.last_open_directory_ = files.value(QStringLiteral("lastOpenDirectory")).toString();
        return settings;
    }

    QSettings legacy;
    const auto legacy_directory = legacy.value(QStringLiteral("lastOpenDirectory")).toString();
    if (!legacy_directory.isEmpty()) {
        settings.last_open_directory_ = legacy_directory;
        QString error;
        if (settings.save(&error)) {
            legacy.remove(QStringLiteral("lastOpenDirectory"));
            legacy.sync();
        } else {
            settings.recovery_warning_ = error;
        }
    }
    return settings;
}

bool UserSettings::save(QString* error) const {
    const QFileInfo target(file_path_);
    if (!QDir().mkpath(target.absolutePath())) {
        if (error != nullptr) *error = QStringLiteral("Moonmark could not create the settings directory.");
        return false;
    }

    QJsonObject updates;
    updates.insert(QStringLiteral("checkOnStartup"), updates_.check_on_startup);
    updates.insert(QStringLiteral("includePrereleases"), updates_.include_prereleases);
    QJsonObject files;
    files.insert(QStringLiteral("lastOpenDirectory"), last_open_directory_);
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), schema_version);
    root.insert(QStringLiteral("updates"), updates);
    root.insert(QStringLiteral("files"), files);

    QSaveFile file(file_path_);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) *error = QStringLiteral("Moonmark could not open settings.json for writing.");
        return false;
    }
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        if (error != nullptr) *error = QStringLiteral("Moonmark could not save settings.json atomically.");
        return false;
    }
    return true;
}

} // namespace moonmark::qt
