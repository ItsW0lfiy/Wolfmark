#pragma once
#include "document_output.h"

#include <QString>
#include <QStringList>

#include <utility>

namespace wolfmark::qt {

struct UpdatePreferences {
    bool check_on_startup = true;
    bool include_prereleases = false;
};

class UserSettings final {
public:
    static constexpr int schema_version = 2;

    static UserSettings load(bool include_prereleases_default);

    [[nodiscard]] bool save(QString* error = nullptr) const;
    [[nodiscard]] const UpdatePreferences& updates() const { return updates_; }
    [[nodiscard]] const QString& lastOpenDirectory() const { return last_open_directory_; }
    [[nodiscard]] const QString& filePath() const { return file_path_; }
    [[nodiscard]] const QString& recoveryWarning() const { return recovery_warning_; }
    [[nodiscard]] bool portable() const { return portable_; }
    [[nodiscard]] bool editorWordWrap() const { return editor_word_wrap_; }
    void setEditorWordWrap(bool value) { editor_word_wrap_ = value; }

    void setCheckOnStartup(bool value) { updates_.check_on_startup = value; }
    void setIncludePrereleases(bool value) { updates_.include_prereleases = value; }
    void setLastOpenDirectory(QString value) { last_open_directory_ = std::move(value); }
    [[nodiscard]] const QStringList& recentDocuments() const { return recent_documents_; }
    void rememberDocument(const QString& path);
    [[nodiscard]] const PageSettings& pageSettings() const { return page_settings_; }
    void setPageSettings(const PageSettings& settings) { page_settings_ = settings; }

private:
    UserSettings(QString file_path, bool portable, bool include_prereleases_default);

    QString file_path_;
    bool portable_ = false;
    UpdatePreferences updates_;
    QString last_open_directory_;
    QString recovery_warning_;
    bool editor_word_wrap_ = true;
    QStringList recent_documents_;
    PageSettings page_settings_;
};

} // namespace wolfmark::qt
