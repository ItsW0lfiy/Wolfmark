#include <windows.h>
#include <msi.h>
#include <msiquery.h>

#include <filesystem>

namespace {
using InitializeFunction = int(__stdcall*)(MSIHANDLE, LPCWSTR, LPDWORD);
using HandlerFunction = INT(__stdcall*)(UINT, MSIHANDLE);
using ShutdownFunction = DWORD(__stdcall*)();

HMODULE gHost = nullptr;
InitializeFunction gInitialize = nullptr;
HandlerFunction gHandler = nullptr;
ShutdownFunction gShutdown = nullptr;

bool loadHost(LPCWSTR resourcePath) {
    if (gHost) {
        return true;
    }
    const std::filesystem::path hostPath =
        std::filesystem::path(resourcePath ? resourcePath : L"") / L"WolfmarkMsiUi.dll";
    gHost = LoadLibraryExW(
        hostPath.c_str(),
        nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!gHost) {
        return false;
    }
    gInitialize = reinterpret_cast<InitializeFunction>(
        GetProcAddress(gHost, "WolfmarkInitializeEmbeddedUI"));
    gHandler = reinterpret_cast<HandlerFunction>(
        GetProcAddress(gHost, "WolfmarkEmbeddedUIHandler"));
    gShutdown = reinterpret_cast<ShutdownFunction>(
        GetProcAddress(gHost, "WolfmarkShutdownEmbeddedUI"));
    return gInitialize && gHandler && gShutdown;
}
}  // namespace

extern "C" __declspec(dllexport) int __stdcall InitializeEmbeddedUI(
    MSIHANDLE install,
    LPCWSTR resourcePath,
    LPDWORD internalUiLevel) {
    return loadHost(resourcePath)
        ? gInitialize(install, resourcePath, internalUiLevel)
        : ERROR_INSTALL_FAILURE;
}

extern "C" __declspec(dllexport) INT __stdcall EmbeddedUIHandler(
    UINT messageType,
    MSIHANDLE record) {
    return gHandler ? gHandler(messageType, record) : IDOK;
}

extern "C" __declspec(dllexport) DWORD __stdcall ShutdownEmbeddedUI() {
    const DWORD result = gShutdown ? gShutdown() : ERROR_SUCCESS;
    gInitialize = nullptr;
    gHandler = nullptr;
    gShutdown = nullptr;
    if (gHost) {
        FreeLibrary(gHost);
        gHost = nullptr;
    }
    return result;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
