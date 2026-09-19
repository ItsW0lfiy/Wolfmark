#pragma once

#include "moonmark_api.h"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

namespace moonmark::qt {

struct ReleaseInfo {
    QString version;
    QString tag_name;
    QString name;
    QString body;
    QUrl html_url;
    QUrl installer_url;
    QUrl portable_url;
    QUrl checksums_url;
};

struct UpdateCheckResult {
    enum class Status { Current, Available, Error };
    Status status = Status::Error;
    ReleaseInfo release;
    QString message;
};

struct UpdateDownloadResult {
    bool valid = false;
    QString file_path;
    QString message;
};

class UpdateManager final : public QObject {
public:
    using CheckCallback = std::function<void(UpdateCheckResult)>;
    using DownloadCallback = std::function<void(UpdateDownloadResult)>;

    explicit UpdateManager(const MoonmarkApiTable* api, QObject* parent = nullptr);

    void check(bool include_prereleases, bool manual, CheckCallback callback);
    void download(const ReleaseInfo& release, bool portable, DownloadCallback callback);

private:
    struct CacheState {
        QByteArray etag;
        QDateTime last_success;
        QByteArray releases_json;
    };

    [[nodiscard]] QString stateFilePath() const;
    [[nodiscard]] QString downloadRoot() const;
    [[nodiscard]] CacheState loadCache() const;
    void saveCache(const CacheState& cache) const;
    [[nodiscard]] UpdateCheckResult select(const QByteArray& releases_json,
                                           bool include_prereleases) const;
    void getBytes(const QUrl& url, std::function<void(bool, QByteArray, QString)> callback);
    [[nodiscard]] bool trustedDownloadUrl(const QUrl& url) const;

    const MoonmarkApiTable* api_ = nullptr;
    QNetworkAccessManager* network_ = nullptr;
};

} // namespace moonmark::qt
