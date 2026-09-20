#include "burn_controller.h"

#include <QApplication>
#include <QDir>
#include <QMetaObject>
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

void BurnController::setWindow(SetupWindow* window) {
    window_ = window;
}

STDMETHODIMP BurnController::OnCreate(IBootstrapperEngine* engine, BOOTSTRAPPER_COMMAND* command) {
    const HRESULT status = CBootstrapperApplicationBase::OnCreate(engine, command);
    if (SUCCEEDED(status)) {
        commandAction_ = command->action;
        commandDisplay_ = command->display;
        commandScope_ = command->commandLineScope == BOOTSTRAPPER_SCOPE_DEFAULT
            ? BOOTSTRAPPER_SCOPE_PER_MACHINE
            : command->commandLineScope;
        targetBundleVersion_ = engineString(L"WixBundleVersion");
        state_.targetVersion = engineString(L"WolfmarkDisplayVersion");
        if (state_.targetVersion.isEmpty()) {
            state_.targetVersion = targetBundleVersion_;
        }
        loadInstalledOptions();
    }
    return status;
}

STDMETHODIMP BurnController::OnStartup() {
    return m_pEngine->Detect(window_ ? window_->nativeHandle() : nullptr);
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
        state_.installed = true;
        detectedVersion_ = QString::fromWCharArray(version);
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
    state_.installed = true;
    if (detectedVersion_.isEmpty()) {
        detectedVersion_ = QString::fromWCharArray(version);
    }
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
        state_.installed = true;
    }
    return S_OK;
}

STDMETHODIMP BurnController::OnDetectComplete(HRESULT status, BOOL) {
    if (FAILED(status)) {
        postError(QStringLiteral("Wolfmark setup could not inspect this computer."), status);
        return S_OK;
    }
    QMetaObject::invokeMethod(qApp, [this] { presentDetectedState(); }, Qt::QueuedConnection);
    return S_OK;
}

void BurnController::presentDetectedState() {
    if (!window_) {
        quit(ERROR_INSTALL_FAILURE);
        return;
    }
    const bool updateAvailable = !detectedVersion_.isEmpty() && !targetBundleVersion_.isEmpty() &&
        QVersionNumber::compare(
            QVersionNumber::fromString(detectedVersion_),
            QVersionNumber::fromString(targetBundleVersion_)) < 0;
    state_.installedVersion = readRegistryString(L"Version");
    if (state_.installedVersion.isEmpty()) {
        state_.installedVersion = detectedVersion_;
    }
    if (commandDisplay_ != BOOTSTRAPPER_DISPLAY_FULL) {
        InstallerAction action = InstallerAction::Install;
        if (commandAction_ == BOOTSTRAPPER_ACTION_UNINSTALL) {
            action = InstallerAction::Uninstall;
        } else if (commandAction_ == BOOTSTRAPPER_ACTION_REPAIR) {
            action = InstallerAction::Repair;
        } else if (state_.installed) {
            action = InstallerAction::Update;
        }
        begin(action, state_.options);
        if (commandDisplay_ == BOOTSTRAPPER_DISPLAY_PASSIVE) {
            window_->show();
        }
        return;
    }
    if (commandAction_ == BOOTSTRAPPER_ACTION_UNINSTALL) {
        window_->showMaintenance(state_);
    } else if (state_.installed && updateAvailable) {
        window_->showUpdate(state_);
    } else if (state_.installed) {
        window_->showMaintenance(state_);
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
    if (FAILED(status)) {
        state_.applying = false;
        postError(QStringLiteral("Wolfmark setup could not prepare the requested change."), status);
        return S_OK;
    }
    QMetaObject::invokeMethod(qApp, [this] {
        const HRESULT applyStatus = m_pEngine->Apply(window_ ? window_->nativeHandle() : nullptr);
        if (FAILED(applyStatus)) {
            state_.applying = false;
            postError(QStringLiteral("Wolfmark setup could not start the requested change."), applyStatus);
        }
    }, Qt::QueuedConnection);
    return S_OK;
}

STDMETHODIMP BurnController::OnProgress(DWORD, DWORD overallProgress, BOOL* cancelFlag) {
    *cancelFlag |= CheckCanceled();
    if (window_) {
        QMetaObject::invokeMethod(qApp, [this, overallProgress] {
            window_->setProgress(static_cast<int>(overallProgress), QString());
        }, Qt::QueuedConnection);
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
    if (window_) {
        QString detail;
        switch (state_.activeAction) {
        case InstallerAction::Uninstall: detail = QStringLiteral("Removing Wolfmark-owned files..."); break;
        case InstallerAction::Repair: detail = QStringLiteral("Restoring Wolfmark application files..."); break;
        case InstallerAction::Modify: detail = QStringLiteral("Applying Windows integration options..."); break;
        default: detail = QStringLiteral("Installing application files..."); break;
        }
        QMetaObject::invokeMethod(qApp, [this, detail] { window_->setProgress(10, detail); }, Qt::QueuedConnection);
    }
    return S_OK;
}

STDMETHODIMP BurnController::OnApplyComplete(
    HRESULT status,
    BOOTSTRAPPER_APPLY_RESTART restart,
    BOOTSTRAPPER_APPLYCOMPLETE_ACTION recommendation,
    BOOTSTRAPPER_APPLYCOMPLETE_ACTION* action) {
    CBootstrapperApplicationBase::OnApplyComplete(status, restart, recommendation, action);
    state_.applying = false;
    if (commandDisplay_ != BOOTSTRAPPER_DISPLAY_FULL) {
        quit(SUCCEEDED(status) ? ERROR_SUCCESS : static_cast<DWORD>(status));
        return S_OK;
    }
    if (SUCCEEDED(status)) {
        if (window_) {
            QMetaObject::invokeMethod(qApp, [this] {
                window_->showComplete(state_.activeAction, state_);
            }, Qt::QueuedConnection);
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

void BurnController::loadInstalledOptions() {
    state_.options.installFolder = readRegistryString(L"InstallDir");
    if (state_.options.installFolder.isEmpty()) {
        state_.options.installFolder = QString::fromLocal8Bit(qgetenv("ProgramFiles")) + QStringLiteral("/Wolfmark");
    }
    state_.options.fileAssociations = readRegistryFlag(L"FileAssociations", true);
    state_.options.desktopShortcut = readRegistryFlag(L"DesktopShortcut", false);
}

void BurnController::postError(const QString& summary, HRESULT status) {
    if (!window_) {
        quit(static_cast<DWORD>(status));
        return;
    }
    const QString details = hresultText(status) + QStringLiteral("\nReview the Burn and MSI logs for diagnostic details.");
    QMetaObject::invokeMethod(qApp, [this, summary, details] {
        window_->showFailure(summary, details);
        window_->show();
    }, Qt::QueuedConnection);
}

BOOTSTRAPPER_ACTION BurnController::burnAction(InstallerAction action) {
    switch (action) {
    case InstallerAction::Modify: return BOOTSTRAPPER_ACTION_MODIFY;
    case InstallerAction::Repair: return BOOTSTRAPPER_ACTION_REPAIR;
    case InstallerAction::Uninstall: return BOOTSTRAPPER_ACTION_UNINSTALL;
    default: return BOOTSTRAPPER_ACTION_INSTALL;
    }
}
