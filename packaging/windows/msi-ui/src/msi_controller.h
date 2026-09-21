#pragma once

#include <windows.h>
#include <msi.h>
#include <msiquery.h>

#include <QCoreApplication>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "installer_host.h"

class SetupWindow;

class MsiController final : public InstallerHost {
public:
    UINT initialize(MSIHANDLE install, const QString& resourceRoot, DWORD* internalUiLevel);
    INT handleMessage(UINT messageType, MSIHANDLE record);
    DWORD shutdown();

    void begin(InstallerAction action, const InstallerOptions& options) override;
    void cancel() override;
    void quit(DWORD exitCode) override;

private:
    static QString property(MSIHANDLE install, const wchar_t* name);
    static QString registryString(const wchar_t* name);
    static bool registryFlag(const wchar_t* name, bool fallback);
    static QString recordString(MSIHANDLE record, UINT field);
    UINT applySelection(MSIHANDLE install);
    bool postToUi(std::function<void()> callback);
    void startUiThread(const QString& resourceRoot);
    void stopUiThread();
    void postProgress(MSIHANDLE record);
    void postAction(MSIHANDLE record);
    void postFailure(const QString& summary, const QString& details);
    void postCompletion(UINT result);

    InstallerState state_;
    InstallerAction selectedAction_ = InstallerAction::None;
    InstallerOptions selectedOptions_;
    std::thread uiThread_;
    std::mutex mutex_;
    std::condition_variable stateChanged_;
    QCoreApplication* application_ = nullptr;
    SetupWindow* window_ = nullptr;
    bool uiReady_ = false;
    bool selectionReady_ = false;
    bool uiFinished_ = false;
    DWORD exitCode_ = ERROR_SUCCESS;
    std::atomic_bool cancelRequested_ = false;
    int totalTicks_ = 0;
    int completedTicks_ = 0;
    bool progressForward_ = true;
    bool installEnded_ = false;
    UINT installResult_ = ERROR_INSTALL_FAILURE;
};
