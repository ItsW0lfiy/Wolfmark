#include "moonmark_updates.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>

#include <utility>

namespace moonmark::qt {
namespace {

constexpr auto releases_endpoint = "https://api.github.com/repos/ItsW0lfiy/Moonmark/releases?per_page=30";
constexpr qint64 automatic_check_interval_seconds = 6 * 60 * 60;

QJsonObject jsonFromBuffer(const MoonmarkApiTable* api, MoonmarkBuffer buffer) {
    const QByteArray bytes(reinterpret_cast<const char*>(buffer.data),
                           static_cast<qsizetype>(buffer.len));
    api->buffer_free(buffer);
    return QJsonDocument::fromJson(bytes).object();
}

QString networkMessage(QNetworkReply* reply) {
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 403 || status == 429)
        return QStringLiteral("GitHub temporarily limited update checks. Try again later.");
    if (reply->error() == QNetworkReply::TimeoutError)
        return QStringLiteral("The update check timed out.");
    return QStringLiteral("Unable to reach GitHub: %1").arg(reply->errorString());
}

} // namespace

UpdateManager::UpdateManager(const MoonmarkApiTable* api, QObject* parent)
    : QObject(parent), api_(api), network_(new QNetworkAccessManager(this)) {}

void UpdateManager::check(bool include_prereleases, bool manual, CheckCallback callback) {
    auto cache = loadCache();
    if (!manual && cache.last_success.isValid() && !cache.releases_json.isEmpty() &&
        cache.last_success.secsTo(QDateTime::currentDateTimeUtc()) < automatic_check_interval_seconds) {
        callback(select(cache.releases_json, include_prereleases));
        return;
    }

    const auto test_endpoint = qEnvironmentVariable("MOONMARK_UPDATE_TEST_ENDPOINT");
    QNetworkRequest request(test_endpoint.isEmpty()
                                ? QUrl(QString::fromLatin1(releases_endpoint))
                                : QUrl(test_endpoint));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent",
                         QStringLiteral("Moonmark/%1").arg(QCoreApplication::applicationVersion()).toUtf8());
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!cache.etag.isEmpty()) request.setRawHeader("If-None-Match", cache.etag);

    auto* reply = network_->get(request);
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, cache = std::move(cache), include_prereleases,
                      callback = std::move(callback)]() mutable {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 304 && !cache.releases_json.isEmpty()) {
            cache.last_success = QDateTime::currentDateTimeUtc();
            saveCache(cache);
            const auto result = select(cache.releases_json, include_prereleases);
            reply->deleteLater();
            callback(result);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            const auto message = networkMessage(reply);
            reply->deleteLater();
            callback({UpdateCheckResult::Status::Error, {}, message});
            return;
        }

        cache.releases_json = reply->readAll();
        cache.etag = reply->rawHeader("ETag");
        cache.last_success = QDateTime::currentDateTimeUtc();
        saveCache(cache);
        const auto result = select(cache.releases_json, include_prereleases);
        reply->deleteLater();
        callback(result);
    });
}

void UpdateManager::download(const ReleaseInfo& release, bool portable,
                             DownloadCallback callback) {
    const auto asset_name = portable ? QStringLiteral("Moonmark-portable-win-x64.zip")
                                     : QStringLiteral("Moonmark-Setup-win-x64.exe");
    const auto asset_url = portable ? release.portable_url : release.installer_url;
    if (!trustedDownloadUrl(asset_url) || !trustedDownloadUrl(release.checksums_url)) {
        callback({false, {}, QStringLiteral("The release is missing a trusted Moonmark update asset.")});
        return;
    }

    getBytes(release.checksums_url,
             [this, release, asset_url, asset_name, callback = std::move(callback)](
                 bool checksum_ok, QByteArray manifest, QString checksum_error) mutable {
        if (!checksum_ok) {
            callback({false, {}, std::move(checksum_error)});
            return;
        }
        getBytes(asset_url,
                 [this, release, asset_name, manifest = std::move(manifest),
                  callback = std::move(callback)](bool asset_ok, QByteArray bytes,
                                                  QString asset_error) mutable {
            if (!asset_ok) {
                callback({false, {}, std::move(asset_error)});
                return;
            }
            const auto directory = QDir(downloadRoot()).filePath(release.version);
            if (!QDir().mkpath(directory)) {
                callback({false, {}, QStringLiteral("Moonmark could not create the update cache directory.")});
                return;
            }
            const auto path = QDir(directory).filePath(asset_name);
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
                callback({false, {}, QStringLiteral("Moonmark could not save the downloaded update.")});
                return;
            }

            const auto path_utf8 = path.toUtf8();
            const auto name_utf8 = asset_name.toUtf8();
            const auto result = jsonFromBuffer(api_, api_->verify_update(
                reinterpret_cast<const std::uint8_t*>(path_utf8.constData()),
                static_cast<std::size_t>(path_utf8.size()),
                reinterpret_cast<const std::uint8_t*>(manifest.constData()),
                static_cast<std::size_t>(manifest.size()),
                reinterpret_cast<const std::uint8_t*>(name_utf8.constData()),
                static_cast<std::size_t>(name_utf8.size())));
            if (!result.value(QStringLiteral("valid")).toBool()) {
                QFile::remove(path);
                callback({false, {}, result.value(QStringLiteral("message")).toString(
                    QStringLiteral("The downloaded update failed verification."))});
                return;
            }
            callback({true, path, {}});
        });
    });
}

QString UpdateManager::stateFilePath() const {
    const auto override_root = qEnvironmentVariable("MOONMARK_UPDATE_STATE_ROOT");
    const auto root = override_root.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        : override_root;
    return QDir(root).filePath(QStringLiteral("update-state.json"));
}

QString UpdateManager::downloadRoot() const {
    const auto override_root = qEnvironmentVariable("MOONMARK_UPDATE_STATE_ROOT");
    if (!override_root.isEmpty()) return QDir(override_root).filePath(QStringLiteral("updates"));
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("updates"));
}

UpdateManager::CacheState UpdateManager::loadCache() const {
    QFile file(stateFilePath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    CacheState cache;
    cache.etag = root.value(QStringLiteral("etag")).toString().toUtf8();
    cache.last_success = QDateTime::fromString(
        root.value(QStringLiteral("lastSuccessfulCheck")).toString(), Qt::ISODateWithMs);
    const auto releases = root.value(QStringLiteral("releases"));
    if (releases.isArray()) cache.releases_json = QJsonDocument(releases.toArray()).toJson(QJsonDocument::Compact);
    return cache;
}

void UpdateManager::saveCache(const CacheState& cache) const {
    const QFileInfo target(stateFilePath());
    if (!QDir().mkpath(target.absolutePath())) return;
    QJsonObject root;
    root.insert(QStringLiteral("etag"), QString::fromUtf8(cache.etag));
    root.insert(QStringLiteral("lastSuccessfulCheck"), cache.last_success.toString(Qt::ISODateWithMs));
    const auto releases = QJsonDocument::fromJson(cache.releases_json);
    if (releases.isArray()) root.insert(QStringLiteral("releases"), releases.array());
    QSaveFile file(target.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) return;
    file.commit();
}

UpdateCheckResult UpdateManager::select(const QByteArray& releases_json,
                                        bool include_prereleases) const {
    const auto result = jsonFromBuffer(api_, api_->select_update(
        reinterpret_cast<const std::uint8_t*>(releases_json.constData()),
        static_cast<std::size_t>(releases_json.size()), include_prereleases));
    const auto status = result.value(QStringLiteral("status")).toString();
    if (status == QStringLiteral("current"))
        return {UpdateCheckResult::Status::Current, {}, {}};
    if (status != QStringLiteral("available"))
        return {UpdateCheckResult::Status::Error, {},
                result.value(QStringLiteral("message")).toString(
                    QStringLiteral("GitHub returned unusable release metadata."))};

    const auto value = result.value(QStringLiteral("release")).toObject();
    ReleaseInfo release;
    release.version = value.value(QStringLiteral("version")).toString();
    release.tag_name = value.value(QStringLiteral("tagName")).toString();
    release.name = value.value(QStringLiteral("name")).toString();
    release.body = value.value(QStringLiteral("body")).toString();
    release.html_url = QUrl(value.value(QStringLiteral("htmlUrl")).toString());
    release.installer_url = QUrl(value.value(QStringLiteral("installerUrl")).toString());
    release.portable_url = QUrl(value.value(QStringLiteral("portableUrl")).toString());
    release.checksums_url = QUrl(value.value(QStringLiteral("checksumsUrl")).toString());
    return {UpdateCheckResult::Status::Available, std::move(release), {}};
}

void UpdateManager::getBytes(const QUrl& url,
                             std::function<void(bool, QByteArray, QString)> callback) {
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/octet-stream");
    request.setRawHeader("User-Agent",
                         QStringLiteral("Moonmark/%1").arg(QCoreApplication::applicationVersion()).toUtf8());
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply = network_->get(request);
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [reply, callback = std::move(callback)]() mutable {
        if (reply->error() != QNetworkReply::NoError) {
            const auto message = networkMessage(reply);
            reply->deleteLater();
            callback(false, {}, message);
            return;
        }
        auto bytes = reply->readAll();
        reply->deleteLater();
        callback(true, std::move(bytes), {});
    });
}

bool UpdateManager::trustedDownloadUrl(const QUrl& url) const {
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
        url.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) == 0;
}

} // namespace moonmark::qt
