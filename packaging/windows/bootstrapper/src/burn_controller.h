#pragma once

#include <windows.h>
#include <msiquery.h>
#include <dutil.h>
#include <dictutil.h>
#include <BootstrapperApplicationBase.h>

#include <QString>

#include "installer_model.h"

class SetupWindow;

class BurnController final : public CBootstrapperApplicationBase {
public:
    BurnController();

    void setWindow(SetupWindow* window);
    void begin(InstallerAction action, const InstallerOptions& options);
    void cancel();
    void quit(DWORD exitCode = ERROR_SUCCESS);

    STDMETHODIMP OnCreate(IBootstrapperEngine* engine, BOOTSTRAPPER_COMMAND* command) override;
    STDMETHODIMP OnStartup() override;
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
    void loadInstalledOptions();
    void presentDetectedState();
    void postError(const QString& summary, HRESULT status);
    static BOOTSTRAPPER_ACTION burnAction(InstallerAction action);

    SetupWindow* window_ = nullptr;
    InstallerState state_;
    QString detectedVersion_;
    QString targetBundleVersion_;
    BOOTSTRAPPER_ACTION commandAction_ = BOOTSTRAPPER_ACTION_UNKNOWN;
    BOOTSTRAPPER_SCOPE commandScope_ = BOOTSTRAPPER_SCOPE_PER_MACHINE;
    BOOTSTRAPPER_DISPLAY commandDisplay_ = BOOTSTRAPPER_DISPLAY_FULL;
};
