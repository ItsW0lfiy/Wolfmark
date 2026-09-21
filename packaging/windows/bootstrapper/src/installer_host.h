#pragma once

#include <windows.h>

#include "installer_model.h"

class InstallerHost {
public:
    virtual ~InstallerHost() = default;

    virtual void begin(InstallerAction action, const InstallerOptions& options) = 0;
    virtual void cancel() = 0;
    virtual void quit(DWORD exitCode) = 0;
};
