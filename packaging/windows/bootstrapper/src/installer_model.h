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

enum class InstallerPresence {
    None,
    Current,
    RelatedOlder,
    RelatedSame,
    RelatedNewer,
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
    InstallerPresence presence = InstallerPresence::None;
    bool installed = false;
    bool applying = false;
};
