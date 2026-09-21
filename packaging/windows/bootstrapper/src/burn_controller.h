#pragma once

#include <windows.h>
#include <msiquery.h>
#include <dutil.h>
#include <dictutil.h>
#include <BootstrapperApplicationBase.h>

#include <QString>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "installer_host.h"

class SetupWindow;
class QCoreApplication;

class BurnController final : public CBootstrapperApplicationBase, public InstallerHost {
public:
    BurnController();

    void begin(InstallerAction action, const InstallerOptions& options) override;
    void cancel() override;
    void quit(DWORD exitCode) override;

    STDMETHODIMP OnCreate(IBootstrapperEngine* engine, BOOTSTRAPPER_COMMAND* command) override;
    STDMETHODIMP OnDestroy(BOOL reload) override;
    STDMETHODIMP OnStartup() override;
    STDMETHODIMP OnShutdown(BOOTSTRAPPER_SHUTDOWN_ACTION* action) override;
    STDMETHODIMP OnDetectRelatedBundle(LPCWSTR, BOOTSTRAPPER_RELATION_TYPE, LPCWSTR, BOOL, LPCWSTR, BOOL, BOOL*) override;
    STDMETHODIMP OnDetectRelatedMsiPackage(LPCWSTR, LPCWSTR, LPCWSTR, BOOL, LPCWSTR, BOOTSTRAPPER_RELATED_OPERATION, BOOL*) override;
    STDMETHODIMP OnDetectPackageComplete(LPCWSTR, HRESULT, BOOTSTRAPPER_PACKAGE_STATE, BOOL) override;
    STDMETHODIMP OnDetectComplete(HRESULT, BOOL) override;
    STDMETHODIMP OnPlanComplete(HRESULT) override;
    STDMETHODIMP OnProgress(DWORD, DWORD, BOOL*) override;
    STDMETHODIMP OnExecutePackageBegin(LPCWSTR, BOOL, BOOTSTRAPPER_ACTION_STATE, INSTALLUILEVEL, BOOL, BOOL*) override;
    STDMETHODIMP OnApplyComplete(HRESULT, BOOTSTRAPPER_APPLY_RESTART, BOOTSTRAPPER_APPLYCOMPLETE_ACTION, BOOTSTRAPPER_APPLYCOMPLETE_ACTION*) override;

private:
    QString engineString(const wchar_t* name) const;
    LONGLONG engineNumeric(const wchar_t* name, LONGLONG fallback = 0) const;
    void noteRelatedVersion(LPCWSTR version);
    void loadInstalledOptions();
    HRESULT startUiThread();
    void stopUiThread();
    void stopApplyThread();
    void applyPlannedAction();
    bool postToUi(std::function<void()> callback);
    void presentDetectedState();
    void postError(const QString& summary, HRESULT status);
    static BOOTSTRAPPER_ACTION burnAction(InstallerAction action);

    SetupWindow* window_ = nullptr;
    InstallerState state_;
    QString detectedVersion_;
    QString targetBundleVersion_;
    bool currentBundleInstalled_ = false;
    bool currentPackagePresent_ = false;
    bool relatedBundlePresent_ = false;
    bool relatedMsiPresent_ = false;
    BOOTSTRAPPER_ACTION commandAction_ = BOOTSTRAPPER_ACTION_UNKNOWN;
    BOOTSTRAPPER_SCOPE commandScope_ = BOOTSTRAPPER_SCOPE_PER_MACHINE;
    BOOTSTRAPPER_DISPLAY commandDisplay_ = BOOTSTRAPPER_DISPLAY_FULL;
    std::thread uiThread_;
    std::thread applyThread_;
    std::mutex uiMutex_;
    std::condition_variable uiReady_;
    QCoreApplication* uiApplication_ = nullptr;
    HWND uiWindowHandle_ = nullptr;
    bool uiInitialized_ = false;
};
