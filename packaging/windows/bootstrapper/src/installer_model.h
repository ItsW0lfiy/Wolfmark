#pragma once

#include <QString>

enum class InstallerAction {
    None,
    Install,
    Update,
    Modify,
    Repair,
    Uninstall,
};

struct InstallerOptions {
    QString installFolder;
    bool fileAssociations = true;
    bool desktopShortcut = false;
};

struct InstallerState {
    QString installedVersion;
    QString targetVersion;
    InstallerOptions options;
    InstallerAction activeAction = InstallerAction::None;
    bool installed = false;
    bool applying = false;
};
