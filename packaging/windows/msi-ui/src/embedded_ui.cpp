#include <windows.h>
#include <msi.h>
#include <msiquery.h>

#include <memory>
#include <mutex>

#include "msi_controller.h"

namespace {
std::mutex gMutex;
std::unique_ptr<MsiController> gController;
}  // namespace

extern "C" __declspec(dllexport) int __stdcall WolfmarkInitializeEmbeddedUI(
    MSIHANDLE install,
    LPCWSTR resourcePath,
    LPDWORD internalUiLevel) {
    constexpr DWORD uiLevelMask = 0x7;
    if (!internalUiLevel || ((*internalUiLevel) & uiLevelMask) == INSTALLUILEVEL_NONE) {
        return ERROR_SUCCESS;
    }
    std::lock_guard lock(gMutex);
    if (gController) {
        return ERROR_INSTALL_ALREADY_RUNNING;
    }
    auto controller = std::make_unique<MsiController>();
    const UINT result = controller->initialize(
        install,
        QString::fromWCharArray(resourcePath ? resourcePath : L""),
        internalUiLevel);
    if (result == ERROR_SUCCESS) {
        gController = std::move(controller);
    }
    return static_cast<int>(result);
}

extern "C" __declspec(dllexport) INT __stdcall WolfmarkEmbeddedUIHandler(
    UINT messageType,
    MSIHANDLE record) {
    std::lock_guard lock(gMutex);
    return gController ? gController->handleMessage(messageType, record) : IDOK;
}

extern "C" __declspec(dllexport) DWORD __stdcall WolfmarkShutdownEmbeddedUI() {
    std::unique_ptr<MsiController> controller;
    {
        std::lock_guard lock(gMutex);
        controller = std::move(gController);
    }
    return controller ? controller->shutdown() : ERROR_SUCCESS;
}
