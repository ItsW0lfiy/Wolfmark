#include "burn_controller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QMetaObject>
#include <QStringList>
#include <QThread>
#include <QVersionNumber>

#include <array>
#include <vector>

#include "setup_window.h"

namespace {
QString readRegistryString(const wchar_t* name) {
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

bool readRegistryFlag(const wchar_t* name, bool fallback) {
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

QString hresultText(HRESULT status) {
    return QStringLiteral("Error 0x%1")
        .arg(static_cast<qulonglong>(static_cast<ULONG>(status)), 8, 16, QLatin1Char('0'))
        .toUpper();
}
}  // namespace

BurnController::BurnController() = default;

STDMETHODIMP BurnController::OnCreate(IBootstrapperEngine* engine, BOOTSTRAPPER_COMMAND* command) {
    const HRESULT status = CBootstrapperApplicationBase::OnCreate(engine, command);
    if (SUCCEEDED(status)) {
        commandAction_ = command->action;
        commandDisplay_ = command->display;
        commandScope_ = command->commandLineScope == BOOTSTRAPPER_SCOPE_DEFAULT
            ? BOOTSTRAPPER_SCOPE_PER_MACHINE
            : command->commandLineScope;
        targetBundleVersion_ = engineString(L"WolfmarkBundleVersion");
        state_.targetVersion = engineString(L"WolfmarkDisplayVersion");
        if (state_.targetVersion.isEmpty()) {
            state_.targetVersion = targetBundleVersion_;
        }
        loadInstalledOptions();
        logLifecycle(QStringLiteral("BA create: action=%1 display=%2 scope=%3")
            .arg(static_cast<int>(commandAction_))
            .arg(static_cast<int>(commandDisplay_))
            .arg(static_cast<int>(commandScope_)));
    }
    return status;
}

STDMETHODIMP BurnController::OnStartup() {
    logLifecycle(QStringLiteral("BA startup"));
    if (commandDisplay_ == BOOTSTRAPPER_DISPLAY_FULL ||
        commandDisplay_ == BOOTSTRAPPER_DISPLAY_PASSIVE) {
        const HRESULT status = startUiThread();
        if (FAILED(status)) {
            return status;
        }
    }
    return m_pEngine->Detect(uiWindowHandle_);
}

STDMETHODIMP BurnController::OnShutdown(BOOTSTRAPPER_SHUTDOWN_ACTION* action) {
    logLifecycle(QStringLiteral("BA shutdown"));
    stopApplyThread();
    stopUiThread();
    return CBootstrapperApplicationBase::OnShutdown(action);
}

STDMETHODIMP BurnController::OnDestroy(BOOL reload) {
    logLifecycle(QStringLiteral("BA destroy: reload=%1").arg(reload));
    stopApplyThread();
    stopUiThread();
    return CBootstrapperApplicationBase::OnDestroy(reload);
}

STDMETHODIMP BurnController::OnDetectBegin(
    BOOL cached,
    BOOTSTRAPPER_REGISTRATION_TYPE registrationType,
    DWORD packageCount,
    BOOL* cancelFlag) {
    logLifecycle(QStringLiteral("Detect begin: cached=%1 registration=%2 packages=%3")
        .arg(cached)
        .arg(static_cast<int>(registrationType))
        .arg(packageCount));
    return CBootstrapperApplicationBase::OnDetectBegin(
        cached, registrationType, packageCount, cancelFlag);
}

STDMETHODIMP BurnController::OnDetectRelatedBundle(
    LPCWSTR,
    BOOTSTRAPPER_RELATION_TYPE relationType,
    LPCWSTR,
    BOOL,
    LPCWSTR version,
    BOOL,
    BOOL* cancelFlag) {
    if (relationType == BOOTSTRAPPER_RELATION_UPGRADE || relationType == BOOTSTRAPPER_RELATION_DETECT) {
        relatedBundlePresent_ = true;
        noteRelatedVersion(version);
    }
    *cancelFlag |= CheckCanceled();
    return S_OK;
}

STDMETHODIMP BurnController::OnDetectRelatedMsiPackage(
    LPCWSTR,
    LPCWSTR,
    LPCWSTR,
    BOOL,
    LPCWSTR version,
    BOOTSTRAPPER_RELATED_OPERATION,
    BOOL* cancelFlag) {
    relatedMsiPresent_ = true;
    noteRelatedVersion(version);
    *cancelFlag |= CheckCanceled();
    return S_OK;
}

STDMETHODIMP BurnController::OnDetectPackageComplete(
    LPCWSTR packageId,
    HRESULT,
    BOOTSTRAPPER_PACKAGE_STATE packageState,
    BOOL) {
    if (QString::fromWCharArray(packageId) == QStringLiteral("WolfmarkMsi") &&
        packageState != BOOTSTRAPPER_PACKAGE_STATE_ABSENT &&
        packageState != BOOTSTRAPPER_PACKAGE_STATE_UNKNOWN) {
        currentPackagePresent_ = true;
    }
    return S_OK;
}

STDMETHODIMP BurnController::OnDetectComplete(HRESULT status, BOOL) {
    logLifecycle(QStringLiteral("Detect complete: %1").arg(hresultText(status)));
    if (FAILED(status)) {
        postError(QStringLiteral("Wolfmark setup could not inspect this computer."), status);
        return S_OK;
    }
    currentBundleInstalled_ = engineNumeric(L"WixBundleInstalled") != 0;
    state_.installed = currentBundleInstalled_;
    if (currentBundleInstalled_) {
        state_.presence = InstallerPresence::Current;
    } else if (currentPackagePresent_ || relatedBundlePresent_ || relatedMsiPresent_) {
        if (detectedVersion_.isEmpty() && currentPackagePresent_) {
            detectedVersion_ = targetBundleVersion_;
        }
        const int comparison = QVersionNumber::compare(
            QVersionNumber::fromString(detectedVersion_),
            QVersionNumber::fromString(targetBundleVersion_));
        state_.presence = comparison < 0
            ? InstallerPresence::RelatedOlder
            : comparison > 0 ? InstallerPresence::RelatedNewer : InstallerPresence::RelatedSame;
    } else {
        state_.presence = InstallerPresence::None;
    }
    if (commandDisplay_ == BOOTSTRAPPER_DISPLAY_FULL ||
        commandDisplay_ == BOOTSTRAPPER_DISPLAY_PASSIVE) {
        postToUi([this] { presentDetectedState(); });
    } else {
        presentDetectedState();
    }
    return S_OK;
}

STDMETHODIMP BurnController::OnPlanBegin(DWORD packageCount, BOOL* cancelFlag) {
    logLifecycle(QStringLiteral("Plan begin: packages=%1 action=%2")
        .arg(packageCount).arg(static_cast<int>(state_.activeAction)));
    return CBootstrapperApplicationBase::OnPlanBegin(packageCount, cancelFlag);
}

HRESULT BurnController::startUiThread() {
    uiThread_ = std::thread([this] {
        int argumentCount = 1;
        char applicationName[] = "WolfmarkSetup";
        char* arguments[] = {applicationName, nullptr};
        QApplication application(argumentCount, arguments);
        QApplication::setApplicationName(QStringLiteral("Wolfmark Setup"));
        QApplication::setOrganizationName(QStringLiteral("Wolfmark"));
        QApplication::setWindowIcon(QIcon(
            QCoreApplication::applicationDirPath() +
            QStringLiteral("/assets/branding/wolfmark-symbol.png")));

        SetupWindow window(this);
        {
            std::lock_guard lock(uiMutex_);
            uiApplication_ = &application;
            window_ = &window;
            uiWindowHandle_ = window.nativeHandle();
            uiInitialized_ = true;
        }
        uiReady_.notify_one();
        application.exec();
        {
            std::lock_guard lock(uiMutex_);
            window_ = nullptr;
            uiApplication_ = nullptr;
            uiWindowHandle_ = nullptr;
        }
    });

    std::unique_lock lock(uiMutex_);
    uiReady_.wait(lock, [this] { return uiInitialized_; });
    return uiApplication_ && uiWindowHandle_ ? S_OK : E_FAIL;
}

void BurnController::stopUiThread() {
    QCoreApplication* application = nullptr;
    {
        std::lock_guard lock(uiMutex_);
        application = uiApplication_;
    }
    if (application) {
        QMetaObject::invokeMethod(application, &QCoreApplication::quit, Qt::QueuedConnection);
    }
    if (uiThread_.joinable() && uiThread_.get_id() != std::this_thread::get_id()) {
        uiThread_.join();
    }
}

void BurnController::stopApplyThread() {
    if (applyThread_.joinable() && applyThread_.get_id() != std::this_thread::get_id()) {
        applyThread_.join();
    }
}

bool BurnController::postToUi(std::function<void()> callback) {
    QCoreApplication* application = nullptr;
    {
        std::lock_guard lock(uiMutex_);
        application = uiApplication_;
    }
    return application && QMetaObject::invokeMethod(
        application, std::move(callback), Qt::QueuedConnection);
}

void BurnController::presentDetectedState() {
    state_.installedVersion = readRegistryString(L"Version");
    if (state_.installedVersion.isEmpty()) {
        state_.installedVersion = detectedVersion_;
    }
    if (commandDisplay_ != BOOTSTRAPPER_DISPLAY_FULL) {
        if (commandAction_ == BOOTSTRAPPER_ACTION_LAYOUT) {
            state_.applying = true;
            const HRESULT status = m_pEngine->Plan(BOOTSTRAPPER_ACTION_LAYOUT, commandScope_);
            if (FAILED(status)) {
                state_.applying = false;
                postError(QStringLiteral("Wolfmark setup could not prepare the requested layout."), status);
            }
            return;
        }
        if (state_.presence == InstallerPresence::RelatedNewer) {
            postError(QStringLiteral("A newer version of Wolfmark is already installed."), HRESULT_FROM_WIN32(ERROR_PRODUCT_VERSION));
            return;
        }
        if ((commandAction_ == BOOTSTRAPPER_ACTION_UNINSTALL ||
             commandAction_ == BOOTSTRAPPER_ACTION_REPAIR) &&
            !currentBundleInstalled_) {
            postError(QStringLiteral("This Wolfmark setup is not the registered installer."), HRESULT_FROM_WIN32(ERROR_UNKNOWN_PRODUCT));
            return;
        }
        InstallerAction action = InstallerAction::Install;
        if (commandAction_ == BOOTSTRAPPER_ACTION_UNINSTALL) {
            action = InstallerAction::Uninstall;
        } else if (commandAction_ == BOOTSTRAPPER_ACTION_REPAIR) {
            action = InstallerAction::Repair;
        } else if (currentBundleInstalled_ || state_.presence == InstallerPresence::RelatedOlder ||
                   state_.presence == InstallerPresence::RelatedSame) {
            action = InstallerAction::Update;
        }
        begin(action, state_.options);
        if (commandDisplay_ == BOOTSTRAPPER_DISPLAY_PASSIVE) {
            window_->show();
        }
        return;
    }
    if (!window_) {
        quit(ERROR_INSTALL_FAILURE);
        return;
    }
    if (state_.presence == InstallerPresence::RelatedNewer) {
        window_->showFailure(
            QStringLiteral("A newer version of Wolfmark is already installed."),
            QStringLiteral("Uninstall the newer version before installing this development build."));
    } else if (commandAction_ == BOOTSTRAPPER_ACTION_UNINSTALL && currentBundleInstalled_) {
        window_->showMaintenance(state_);
    } else if (currentBundleInstalled_) {
        window_->showMaintenance(state_);
    } else if (state_.presence == InstallerPresence::RelatedOlder ||
               state_.presence == InstallerPresence::RelatedSame) {
        window_->showUpdate(state_);
    } else {
        state_.activeAction = InstallerAction::Install;
        window_->showInstall(state_);
    }
    window_->show();
    window_->raise();
    window_->activateWindow();
}

void BurnController::begin(InstallerAction action, const InstallerOptions& options) {
    if (!m_pEngine || state_.applying) {
        return;
    }
    state_.activeAction = action;
    state_.options = options;
    state_.applying = true;
    m_pEngine->SetVariableNumeric(L"WolfmarkFileAssociations", options.fileAssociations ? 1 : 0);
    m_pEngine->SetVariableNumeric(L"WolfmarkDesktopShortcut", options.desktopShortcut ? 1 : 0);
    const std::wstring folder = QDir::toNativeSeparators(options.installFolder).toStdWString();
    m_pEngine->SetVariableString(L"WolfmarkInstallFolder", folder.c_str(), FALSE);
    if (window_) {
        window_->showProgress(action);
    }
    const HRESULT status = m_pEngine->Plan(burnAction(action), commandScope_);
    if (FAILED(status)) {
        state_.applying = false;
        postError(QStringLiteral("Wolfmark setup could not prepare the requested change."), status);
    }
}

STDMETHODIMP BurnController::OnPlanComplete(HRESULT status) {
    logLifecycle(QStringLiteral("Plan complete: %1").arg(hresultText(status)));
    if (FAILED(status)) {
        state_.applying = false;
        postError(QStringLiteral("Wolfmark setup could not prepare the requested change."), status);
        return S_OK;
    }
    if (commandDisplay_ == BOOTSTRAPPER_DISPLAY_FULL ||
        commandDisplay_ == BOOTSTRAPPER_DISPLAY_PASSIVE) {
        if (!postToUi([this] { applyPlannedAction(); })) {
            state_.applying = false;
            quit(ERROR_INSTALL_FAILURE);
        }
    } else {
        applyThread_ = std::thread([this] { applyPlannedAction(); });
    }
    return S_OK;
}

STDMETHODIMP BurnController::OnApplyBegin(DWORD phaseCount, BOOL* cancelFlag) {
    logLifecycle(QStringLiteral("Apply begin: phases=%1").arg(phaseCount));
    return CBootstrapperApplicationBase::OnApplyBegin(phaseCount, cancelFlag);
}

void BurnController::applyPlannedAction() {
    HWND parent = GetDesktopWindow();
    {
        std::lock_guard lock(uiMutex_);
        if (uiWindowHandle_) {
            parent = uiWindowHandle_;
        }
    }
    const HRESULT status = m_pEngine->Apply(parent);
    if (FAILED(status)) {
        state_.applying = false;
        postError(QStringLiteral("Wolfmark setup could not start the requested change."), status);
    }
}

STDMETHODIMP BurnController::OnProgress(DWORD, DWORD overallProgress, BOOL* cancelFlag) {
    *cancelFlag |= CheckCanceled();
    if (window_) {
        postToUi([this, overallProgress] {
            window_->setProgress(static_cast<int>(overallProgress), QString());
        });
    }
    return S_OK;
}

STDMETHODIMP BurnController::OnExecutePackageBegin(
    LPCWSTR packageId,
    BOOL execute,
    BOOTSTRAPPER_ACTION_STATE action,
    INSTALLUILEVEL uiLevel,
    BOOL disableExternalUiHandler,
    BOOL* cancelFlag) {
    CBootstrapperApplicationBase::OnExecutePackageBegin(
        packageId, execute, action, uiLevel, disableExternalUiHandler, cancelFlag);
    logLifecycle(QStringLiteral("Execute package begin: %1 action=%2 ui=%3 external-ui-disabled=%4")
        .arg(QString::fromWCharArray(packageId))
        .arg(static_cast<int>(action))
        .arg(static_cast<int>(uiLevel))
        .arg(disableExternalUiHandler));
    if (window_) {
        QString detail;
        switch (state_.activeAction) {
        case InstallerAction::Uninstall: detail = QStringLiteral("Removing Wolfmark-owned files..."); break;
        case InstallerAction::Repair: detail = QStringLiteral("Restoring Wolfmark application files..."); break;
        case InstallerAction::Modify: detail = QStringLiteral("Applying Windows integration options..."); break;
        default: detail = QStringLiteral("Installing application files..."); break;
        }
        postToUi([this, detail] { window_->setProgress(10, detail); });
    }
    return S_OK;
}

STDMETHODIMP BurnController::OnExecuteFilesInUse(
    LPCWSTR packageId,
    DWORD fileCount,
    LPCWSTR* files,
    int recommendation,
    BOOTSTRAPPER_FILES_IN_USE_TYPE source,
    int* result) {
    QStringList fileList;
    for (DWORD index = 0; index < fileCount; ++index) {
        if (files[index] && *files[index]) {
            fileList.append(QString::fromWCharArray(files[index]));
        }
    }
    logLifecycle(QStringLiteral("Files in use: package=%1 source=%2 count=%3 recommendation=%4")
        .arg(QString::fromWCharArray(packageId))
        .arg(static_cast<int>(source))
        .arg(fileCount)
        .arg(recommendation));

    if (commandDisplay_ != BOOTSTRAPPER_DISPLAY_FULL) {
        *result = IDIGNORE;
        logLifecycle(QStringLiteral("Files in use: noninteractive operation will continue and may require restart"));
        return S_OK;
    }

    SetupWindow* window = nullptr;
    {
        std::lock_guard lock(uiMutex_);
        window = window_;
    }
    if (!window) {
        *result = IDCANCEL;
        return S_OK;
    }

    int selection = IDCANCEL;
    const bool restartManager = source == BOOTSTRAPPER_FILES_IN_USE_TYPE_MSI_RM;
    const auto prompt = [window, fileList, restartManager, &selection] {
        selection = window->promptFilesInUse(fileList, restartManager);
    };
    if (QThread::currentThread() == window->thread()) {
        prompt();
    } else if (!QMetaObject::invokeMethod(window, prompt, Qt::BlockingQueuedConnection)) {
        selection = IDCANCEL;
    }
    *result = selection;
    logLifecycle(QStringLiteral("Files in use response: %1").arg(selection));
    return S_OK;
}

STDMETHODIMP BurnController::OnExecutePackageComplete(
    LPCWSTR packageId,
    HRESULT status,
    BOOTSTRAPPER_APPLY_RESTART restart,
    BOOTSTRAPPER_EXECUTEPACKAGECOMPLETE_ACTION recommendation,
    BOOTSTRAPPER_EXECUTEPACKAGECOMPLETE_ACTION* action) {
    logLifecycle(QStringLiteral("Execute package complete: %1 result=%2 restart=%3")
        .arg(QString::fromWCharArray(packageId))
        .arg(hresultText(status))
        .arg(static_cast<int>(restart)));
    return CBootstrapperApplicationBase::OnExecutePackageComplete(
        packageId, status, restart, recommendation, action);
}

STDMETHODIMP BurnController::OnApplyComplete(
    HRESULT status,
    BOOTSTRAPPER_APPLY_RESTART restart,
    BOOTSTRAPPER_APPLYCOMPLETE_ACTION recommendation,
    BOOTSTRAPPER_APPLYCOMPLETE_ACTION* action) {
    CBootstrapperApplicationBase::OnApplyComplete(status, restart, recommendation, action);
    logLifecycle(QStringLiteral("Apply complete: %1 restart=%2")
        .arg(hresultText(status)).arg(static_cast<int>(restart)));
    state_.applying = false;
    if (commandDisplay_ != BOOTSTRAPPER_DISPLAY_FULL) {
        quit(SUCCEEDED(status) ? ERROR_SUCCESS : static_cast<DWORD>(status));
        return S_OK;
    }
    if (SUCCEEDED(status)) {
        if (window_) {
            postToUi([this] {
                window_->showComplete(state_.activeAction, state_);
            });
        }
    } else if (status == HRESULT_FROM_WIN32(ERROR_INSTALL_USEREXIT)) {
        postError(QStringLiteral("Wolfmark setup was cancelled safely."), status);
    } else {
        postError(QStringLiteral("Wolfmark setup could not complete the requested change."), status);
    }
    return S_OK;
}

void BurnController::cancel() {
    EnterCriticalSection(&m_csCanceled);
    m_fCanceled = TRUE;
    LeaveCriticalSection(&m_csCanceled);
}

void BurnController::quit(DWORD exitCode) {
    if (m_pEngine) {
        m_pEngine->Quit(exitCode);
    }
}

QString BurnController::engineString(const wchar_t* name) const {
    if (!m_pEngine) {
        return {};
    }
    SIZE_T length = 0;
    HRESULT status = m_pEngine->GetVariableString(name, nullptr, &length);
    if (status != HRESULT_FROM_WIN32(ERROR_MORE_DATA) || length == 0) {
        return {};
    }
    std::vector<wchar_t> value(length);
    status = m_pEngine->GetVariableString(name, value.data(), &length);
    return SUCCEEDED(status) ? QString::fromWCharArray(value.data()) : QString();
}

LONGLONG BurnController::engineNumeric(const wchar_t* name, LONGLONG fallback) const {
    if (!m_pEngine) {
        return fallback;
    }
    LONGLONG value = fallback;
    return SUCCEEDED(m_pEngine->GetVariableNumeric(name, &value)) ? value : fallback;
}

void BurnController::noteRelatedVersion(LPCWSTR version) {
    const QString candidate = QString::fromWCharArray(version);
    if (detectedVersion_.isEmpty() ||
        QVersionNumber::compare(
            QVersionNumber::fromString(candidate),
            QVersionNumber::fromString(detectedVersion_)) > 0) {
        detectedVersion_ = candidate;
    }
}

void BurnController::loadInstalledOptions() {
    state_.options.installFolder = readRegistryString(L"InstallDir");
    if (state_.options.installFolder.isEmpty()) {
        QString programFiles = QString::fromLocal8Bit(qgetenv("ProgramW6432"));
        if (programFiles.isEmpty()) {
            programFiles = QString::fromLocal8Bit(qgetenv("ProgramFiles"));
        }
        state_.options.installFolder = QDir::toNativeSeparators(
            QDir(programFiles).filePath(QStringLiteral("Wolfmark")));
    }
    state_.options.fileAssociations = readRegistryFlag(L"FileAssociations", true);
    state_.options.desktopShortcut = readRegistryFlag(L"DesktopShortcut", false);
}

void BurnController::postError(const QString& summary, HRESULT status) {
    if (!window_) {
        quit(static_cast<DWORD>(status));
        return;
    }
    QString details = hresultText(status);
    const QString bundleLog = engineString(L"WixBundleLog");
    const QString packageLog = engineString(L"WixBundleLog_WolfmarkMsi");
    if (!bundleLog.isEmpty()) {
        details += QStringLiteral("\n\nBurn log:\n") + bundleLog;
    }
    if (!packageLog.isEmpty()) {
        details += QStringLiteral("\n\nWindows Installer log:\n") + packageLog;
    }
    postToUi([this, summary, details] {
        window_->showFailure(summary, details);
        window_->show();
    });
}

void BurnController::logLifecycle(const QString& message) const {
    if (!m_pEngine) {
        return;
    }
    const std::wstring native = (QStringLiteral("Wolfmark BA: ") + message).toStdWString();
    m_pEngine->Log(BOOTSTRAPPER_LOG_LEVEL_STANDARD, native.c_str());
}

BOOTSTRAPPER_ACTION BurnController::burnAction(InstallerAction action) {
    switch (action) {
    case InstallerAction::Modify: return BOOTSTRAPPER_ACTION_MODIFY;
    case InstallerAction::Repair: return BOOTSTRAPPER_ACTION_REPAIR;
    case InstallerAction::Uninstall: return BOOTSTRAPPER_ACTION_UNINSTALL;
    default: return BOOTSTRAPPER_ACTION_INSTALL;
    }
}
