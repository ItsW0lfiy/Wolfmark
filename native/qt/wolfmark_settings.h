#pragma once

#include <QString>

#include <utility>

namespace wolfmark::qt {

struct UpdatePreferences {
    bool check_on_startup = true;
    bool include_prereleases = false;
};

class UserSettings final {
public:
    static constexpr int schema_version = 1;

    static UserSettings load(bool include_prereleases_default);

    [[nodiscard]] bool save(QString* error = nullptr) const;
    [[nodiscard]] const UpdatePreferences& updates() const { return updates_; }
    [[nodiscard]] const QString& lastOpenDirectory() const { return last_open_directory_; }
    [[nodiscard]] const QString& filePath() const { return file_path_; }
    [[nodiscard]] const QString& recoveryWarning() const { return recovery_warning_; }
    [[nodiscard]] bool portable() const { return portable_; }

    void setCheckOnStartup(bool value) { updates_.check_on_startup = value; }
    void setIncludePrereleases(bool value) { updates_.include_prereleases = value; }
    void setLastOpenDirectory(QString value) { last_open_directory_ = std::move(value); }

private:
    UserSettings(QString file_path, bool portable, bool include_prereleases_default);

    QString file_path_;
    bool portable_ = false;
    UpdatePreferences updates_;
    QString last_open_directory_;
    QString recovery_warning_;
};

} // namespace wolfmark::qt
