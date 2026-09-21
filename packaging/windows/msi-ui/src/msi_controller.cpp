#include "msi_controller.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QMetaObject>
#include <QVersionNumber>

#include <array>
#include <vector>

#include "setup_window.h"

namespace {
constexpr DWORD kUiLevelMask = 0x7;

UINT setProperty(MSIHANDLE install, const wchar_t* name, const QString& value) {
    const std::wstring native = value.toStdWString();
    return MsiSetPropertyW(install, name, native.c_str());
}
}  // namespace

UINT MsiController::initialize(
    MSIHANDLE install,
    const QString& resourceRoot,
    DWORD* internalUiLevel) {
    if (!internalUiLevel || ((*internalUiLevel) & kUiLevelMask) == INSTALLUILEVEL_NONE) {
        return ERROR_SUCCESS;
    }

    QString relatedVersion;
    state_.presence = detectPresence(install, &relatedVersion);
    state_.installed = state_.presence == InstallerPresence::Current;
    state_.targetVersion = property(install, L"WOLFMARK_DISPLAY_VERSION");
    state_.installedVersion = registryString(L"Version");
    if (state_.installedVersion.isEmpty()) {
        state_.installedVersion = relatedVersion;
    }
    state_.options.installFolder = property(install, L"INSTALLFOLDER");
    if (state_.options.installFolder.isEmpty()) {
        state_.options.installFolder = registryString(L"InstallDir");
    }
    if (state_.options.installFolder.isEmpty()) {
        state_.options.installFolder = QString::fromLocal8Bit(qgetenv("ProgramFiles")) +
            QStringLiteral("/Wolfmark");
    }
    state_.options.fileAssociations = registryFlag(L"FileAssociations", true);
    state_.options.desktopShortcut = registryFlag(L"DesktopShortcut", false);
    state_.activeAction = state_.presence == InstallerPresence::None
        ? InstallerAction::Install
        : state_.presence == InstallerPresence::Current
            ? InstallerAction::None
            : InstallerAction::Update;

    startUiThread(resourceRoot);
    std::unique_lock lock(mutex_);
    stateChanged_.wait(lock, [this] { return uiReady_; });
    stateChanged_.wait(lock, [this] { return selectionReady_ || uiFinished_; });
    if (!selectionReady_ || exitCode_ != ERROR_SUCCESS) {
        lock.unlock();
        stopUiThread();
        return exitCode_ == ERROR_SUCCESS ? ERROR_INSTALL_USEREXIT : exitCode_;
    }
    lock.unlock();

    const UINT result = applySelection(install);
    if (result != ERROR_SUCCESS) {
        stopUiThread();
        return result;
    }
    *internalUiLevel = INSTALLUILEVEL_NONE;
    return ERROR_SUCCESS;
}

void MsiController::startUiThread(const QString& resourceRoot) {
    uiThread_ = std::thread([this, resourceRoot] {
        const QByteArray platformPath = resourceRoot.toUtf8();
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", platformPath);
        int argumentCount = 1;
        char applicationName[] = "WolfmarkMsiUi";
        char* arguments[] = {applicationName, nullptr};
        QApplication application(argumentCount, arguments);
        QApplication::setApplicationName(QStringLiteral("Wolfmark Setup"));
        QApplication::setOrganizationName(QStringLiteral("Wolfmark"));
        QApplication::setWindowIcon(QIcon(
            QDir(resourceRoot).filePath(QStringLiteral("wolfmark-symbol.png"))));

        SetupWindow window(this, resourceRoot);
        if (state_.presence == InstallerPresence::Current) {
            window.showMaintenance(state_);
        } else if (state_.presence == InstallerPresence::RelatedOlder ||
                   state_.presence == InstallerPresence::RelatedSame) {
            window.showUpdate(state_);
        } else if (state_.presence == InstallerPresence::RelatedNewer) {
            window.showFailure(
                QStringLiteral("A newer version of Wolfmark is already installed."),
                QStringLiteral("Uninstall the newer version before installing this development build."));
        } else {
            window.showInstall(state_);
        }
        window.show();
        window.raise();
        window.activateWindow();
        {
            std::lock_guard lock(mutex_);
            application_ = &application;
            window_ = &window;
            uiReady_ = true;
        }
        stateChanged_.notify_all();
        application.exec();
        {
            std::lock_guard lock(mutex_);
            window_ = nullptr;
            application_ = nullptr;
            uiFinished_ = true;
        }
        stateChanged_.notify_all();
    });
}

UINT MsiController::applySelection(MSIHANDLE install) {
    UINT result = setProperty(install, L"INSTALLFOLDER", selectedOptions_.installFolder);
    if (result == ERROR_SUCCESS) {
        result = MsiSetPropertyW(
            install,
            L"WOLFMARK_FILE_ASSOC",
            selectedOptions_.fileAssociations ? L"1" : L"0");
    }
    if (result == ERROR_SUCCESS) {
        result = MsiSetPropertyW(
            install,
            L"WOLFMARK_DESKTOP_SHORTCUT",
            selectedOptions_.desktopShortcut ? L"1" : L"0");
    }
    if (result != ERROR_SUCCESS) {
        return result;
    }

    switch (selectedAction_) {
    case InstallerAction::Uninstall:
        return MsiSetPropertyW(install, L"REMOVE", L"ALL");
    case InstallerAction::Repair:
    case InstallerAction::Modify:
        result = MsiSetPropertyW(install, L"REINSTALL", L"ALL");
        return result == ERROR_SUCCESS
            ? MsiSetPropertyW(install, L"REINSTALLMODE", L"amus")
            : result;
    default:
        return ERROR_SUCCESS;
    }
}

void MsiController::begin(InstallerAction action, const InstallerOptions& options) {
    {
        std::lock_guard lock(mutex_);
        selectedAction_ = action;
        selectedOptions_ = options;
        state_.activeAction = action;
        state_.options = options;
        state_.applying = true;
        selectionReady_ = true;
        exitCode_ = ERROR_SUCCESS;
    }
    if (window_) {
        window_->showProgress(action);
    }
    stateChanged_.notify_all();
}

void MsiController::cancel() {
    cancelRequested_.store(true);
}

void MsiController::quit(DWORD exitCode) {
    QCoreApplication* application = nullptr;
    {
        std::lock_guard lock(mutex_);
        exitCode_ = exitCode;
        if (!selectionReady_) {
            selectionReady_ = true;
        }
        application = application_;
    }
    stateChanged_.notify_all();
    if (application) {
        QMetaObject::invokeMethod(application, &QCoreApplication::quit, Qt::QueuedConnection);
    }
}

INT MsiController::handleMessage(UINT messageType, MSIHANDLE record) {
    if (cancelRequested_.load()) {
        return IDCANCEL;
    }
    const auto type = static_cast<INSTALLMESSAGE>(messageType & 0xFF000000);
    switch (type) {
    case INSTALLMESSAGE_ACTIONSTART:
        postAction(record);
        return IDOK;
    case INSTALLMESSAGE_PROGRESS:
        postProgress(record);
        return IDOK;
    case INSTALLMESSAGE_ERROR:
    case INSTALLMESSAGE_FATALEXIT: {
        const int code = MsiRecordGetInteger(record, 1);
        postFailure(
            QStringLiteral("Windows Installer reported an error."),
            QStringLiteral("MSI error %1. Review the Windows Installer log for full details.").arg(code));
        return IDOK;
    }
    case INSTALLMESSAGE_WARNING:
        postToUi([this] {
            if (window_) {
                window_->setProgress(-1, QStringLiteral("Windows Installer reported a warning; continuing..."));
            }
        });
        return IDOK;
    case INSTALLMESSAGE_FILESINUSE:
    case INSTALLMESSAGE_RMFILESINUSE:
        postToUi([this] {
            if (window_) {
                window_->setProgress(-1, QStringLiteral("Waiting for files currently in use..."));
            }
        });
        return IDIGNORE;
    case INSTALLMESSAGE_RESOLVESOURCE:
        postToUi([this] {
            if (window_) {
                window_->setProgress(-1, QStringLiteral("Locating the Wolfmark installation source..."));
            }
        });
        return IDOK;
    case INSTALLMESSAGE_INSTALLEND: {
        const int value = MsiRecordGetInteger(record, 3);
        installResult_ = value == MSI_NULL_INTEGER ? ERROR_INSTALL_FAILURE : static_cast<UINT>(value);
        installEnded_ = true;
        postCompletion(installResult_);
        return IDOK;
    }
    default:
        return IDOK;
    }
}

void MsiController::postProgress(MSIHANDLE record) {
    const int subtype = MsiRecordGetInteger(record, 1);
    const int value = MsiRecordGetInteger(record, 2);
    if (subtype == 0) {
        totalTicks_ = value;
        completedTicks_ = MsiRecordGetInteger(record, 3) == 0 ? 0 : totalTicks_;
        progressForward_ = MsiRecordGetInteger(record, 3) == 0;
    } else if (subtype == 2 && totalTicks_ > 0) {
        completedTicks_ += progressForward_ ? value : -value;
    } else if (subtype == 3) {
        totalTicks_ += value;
    }
    const int percent = totalTicks_ > 0
        ? qBound(0, (completedTicks_ * 100) / totalTicks_, 100)
        : 0;
    postToUi([this, percent] {
        if (window_) {
            window_->setProgress(percent, QString());
        }
    });
}

void MsiController::postAction(MSIHANDLE record) {
    QString detail = recordString(record, 2);
    if (detail.isEmpty()) {
        detail = recordString(record, 1);
    }
    postToUi([this, detail] {
        if (window_ && !detail.isEmpty()) {
            window_->setProgress(-1, detail);
        }
    });
}

void MsiController::postFailure(const QString& summary, const QString& details) {
    postToUi([this, summary, details] {
        if (window_) {
            window_->showFailure(summary, details);
        }
    });
}

void MsiController::postCompletion(UINT result) {
    if (result == ERROR_SUCCESS) {
        postToUi([this] {
            if (window_) {
                window_->showComplete(selectedAction_, state_);
            }
        });
    } else if (result == ERROR_INSTALL_USEREXIT) {
        postFailure(
            QStringLiteral("Wolfmark setup was cancelled safely."),
            QStringLiteral("No incomplete installation changes were kept."));
    } else {
        postFailure(
            QStringLiteral("Wolfmark setup could not complete."),
            QStringLiteral("Windows Installer exited with code %1.").arg(result));
    }
}

DWORD MsiController::shutdown() {
    if (!installEnded_) {
        postFailure(
            QStringLiteral("Wolfmark setup ended unexpectedly."),
            QStringLiteral("Windows Installer did not report a final result."));
    }
    std::unique_lock lock(mutex_);
    stateChanged_.wait(lock, [this] { return uiFinished_; });
    lock.unlock();
    stopUiThread();
    return ERROR_SUCCESS;
}

void MsiController::stopUiThread() {
    QCoreApplication* application = nullptr;
    {
        std::lock_guard lock(mutex_);
        application = application_;
    }
    if (application) {
        QMetaObject::invokeMethod(application, &QCoreApplication::quit, Qt::QueuedConnection);
    }
    if (uiThread_.joinable() && uiThread_.get_id() != std::this_thread::get_id()) {
        uiThread_.join();
    }
}

bool MsiController::postToUi(std::function<void()> callback) {
    QCoreApplication* application = nullptr;
    {
        std::lock_guard lock(mutex_);
        application = application_;
    }
    return application && QMetaObject::invokeMethod(
        application, std::move(callback), Qt::QueuedConnection);
}

QString MsiController::property(MSIHANDLE install, const wchar_t* name) {
    DWORD length = 0;
    UINT result = MsiGetPropertyW(install, name, nullptr, &length);
    if (result != ERROR_MORE_DATA || length == 0) {
        return {};
    }
    std::vector<wchar_t> value(static_cast<std::size_t>(length) + 1);
    ++length;
    result = MsiGetPropertyW(install, name, value.data(), &length);
    return result == ERROR_SUCCESS ? QString::fromWCharArray(value.data()) : QString();
}

QString MsiController::productInfo(const QString& productCode, const wchar_t* name) {
    const std::wstring nativeProductCode = productCode.toStdWString();
    DWORD length = 0;
    UINT result = MsiGetProductInfoW(nativeProductCode.c_str(), name, nullptr, &length);
    if (result != ERROR_MORE_DATA || length == 0) {
        return {};
    }
    std::vector<wchar_t> value(static_cast<std::size_t>(length) + 1);
    ++length;
    result = MsiGetProductInfoW(nativeProductCode.c_str(), name, value.data(), &length);
    return result == ERROR_SUCCESS ? QString::fromWCharArray(value.data()) : QString();
}

InstallerPresence MsiController::detectPresence(MSIHANDLE install, QString* relatedVersion) const {
    const QString currentProductCode = property(install, L"ProductCode");
    if (!property(install, L"Installed").isEmpty() ||
        (!currentProductCode.isEmpty() &&
         MsiQueryProductStateW(currentProductCode.toStdWString().c_str()) == INSTALLSTATE_DEFAULT)) {
        if (relatedVersion) {
            *relatedVersion = property(install, L"ProductVersion");
        }
        return InstallerPresence::Current;
    }

    const QString upgradeCode = property(install, L"UpgradeCode");
    QString newestRelatedVersion;
    if (!upgradeCode.isEmpty()) {
        const std::wstring nativeUpgradeCode = upgradeCode.toStdWString();
        for (DWORD index = 0;; ++index) {
            std::array<wchar_t, 39> productCode{};
            const UINT result = MsiEnumRelatedProductsW(
                nativeUpgradeCode.c_str(), 0, index, productCode.data());
            if (result == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (result != ERROR_SUCCESS) {
                break;
            }
            const QString candidateCode = QString::fromWCharArray(productCode.data());
            if (candidateCode.compare(currentProductCode, Qt::CaseInsensitive) == 0 ||
                MsiQueryProductStateW(productCode.data()) != INSTALLSTATE_DEFAULT) {
                continue;
            }
            const QString candidateVersion = productInfo(candidateCode, INSTALLPROPERTY_VERSIONSTRING);
            if (newestRelatedVersion.isEmpty() ||
                QVersionNumber::compare(
                    QVersionNumber::fromString(candidateVersion),
                    QVersionNumber::fromString(newestRelatedVersion)) > 0) {
                newestRelatedVersion = candidateVersion;
            }
        }
    }
    if (newestRelatedVersion.isEmpty()) {
        return InstallerPresence::None;
    }
    if (relatedVersion) {
        *relatedVersion = newestRelatedVersion;
    }
    const int comparison = QVersionNumber::compare(
        QVersionNumber::fromString(newestRelatedVersion),
        QVersionNumber::fromString(property(install, L"ProductVersion")));
    return comparison < 0
        ? InstallerPresence::RelatedOlder
        : comparison > 0 ? InstallerPresence::RelatedNewer : InstallerPresence::RelatedSame;
}

QString MsiController::registryString(const wchar_t* name) {
    std::array<wchar_t, 1024> value{};
    DWORD size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    const LSTATUS status = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        L"Software\\ItsW0lfiy\\Wolfmark\\Installer",
        name,
        RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
        nullptr,
        value.data(),
        &size);
    return status == ERROR_SUCCESS ? QString::fromWCharArray(value.data()) : QString();
}

bool MsiController::registryFlag(const wchar_t* name, bool fallback) {
    DWORD value = fallback ? 1U : 0U;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        L"Software\\ItsW0lfiy\\Wolfmark\\Installer",
        name,
        RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY,
        nullptr,
        &value,
        &size);
    return status == ERROR_SUCCESS ? value != 0 : fallback;
}

QString MsiController::recordString(MSIHANDLE record, UINT field) {
    DWORD length = 0;
    UINT result = MsiRecordGetStringW(record, field, nullptr, &length);
    if (result != ERROR_MORE_DATA || length == 0) {
        return {};
    }
    std::vector<wchar_t> value(static_cast<std::size_t>(length) + 1);
    ++length;
    result = MsiRecordGetStringW(record, field, value.data(), &length);
    return result == ERROR_SUCCESS ? QString::fromWCharArray(value.data()) : QString();
}
