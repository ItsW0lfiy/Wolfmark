#pragma once

#include <QString>

enum class InstallerAction {
    None,
    Install,
    Update,
    Modify,
    Repair,
    Reinstall,
    Uninstall,
    Diagnose,
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
    QString installedBuildId;
    QString targetBuildId;
    QString statusDetail;
    InstallerOptions options;
    InstallerAction activeAction = InstallerAction::None;
    InstallerPresence presence = InstallerPresence::None;
    bool installed = false;
    bool applying = false;
};
