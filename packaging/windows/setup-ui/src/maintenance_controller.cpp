#include "maintenance_controller.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include "setup_window.h"
#include "wolfmark_setup_version.h"

namespace {
QString buildField(const QJsonObject& value, const char* name) {
    return value.value(QLatin1String(name)).toString();
}
}

MaintenanceController::MaintenanceController(
    Paths paths, QString requestedAction, bool silent, QObject* parent)
    : QObject(parent),
      paths_(std::move(paths)),
      requestedAction_(std::move(requestedAction)),
      silent_(silent) {
    state_.targetVersion = QStringLiteral(WOLFMARK_SETUP_VERSION_STRING);
    state_.options.installFolder = paths_.installFolder;
    state_.options.fileAssociations = true;
    state_.options.desktopShortcut = false;
}

void MaintenanceController::attach(SetupWindow* window) {
    window_ = window;
}

void MaintenanceController::initialize() {
    if (!window_) return;
    window_->showProgress(InstallerAction::Diagnose);
    start(detectArguments(), InstallerAction::Diagnose, true);
}

int MaintenanceController::runSilent() {
    QProcess detect;
    detect.start(paths_.maintenance, detectArguments());
    if (!detect.waitForStarted() || !detect.waitForFinished(-1) || detect.exitCode() != 0) {
        return ERROR_INSTALL_FAILURE;
    }
    if (!parseDetection(detect.readAllStandardOutput(), false)) {
        return ERROR_INSTALL_FAILURE;
    }
    InstallerAction action = requestedInstallerAction();
    if (action == InstallerAction::None) {
        action = defaultActionForStatus(statusKind_);
    }
    if (action == InstallerAction::None) return ERROR_PRODUCT_VERSION;
    if (action == InstallerAction::Modify && statusKind_ == QStringLiteral("absent")) {
        return ERROR_PRODUCT_UNINSTALLED;
    }
    if (action == InstallerAction::Update
        && statusKind_ != QStringLiteral("older")
        && statusKind_ != QStringLiteral("same-version-installed-ancestor")) {
        return ERROR_PRODUCT_VERSION;
    }
    if (action == InstallerAction::Diagnose) return ERROR_SUCCESS;
    QProcess operation;
    operation.start(paths_.maintenance, operationArguments(action, state_.options));
    if (!operation.waitForStarted() || !operation.waitForFinished(-1)) {
        return ERROR_INSTALL_FAILURE;
    }
    return operation.exitCode() == 0 ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE;
}

void MaintenanceController::begin(InstallerAction action, const InstallerOptions& options) {
    if (process_.state() != QProcess::NotRunning) return;
    cancelRequested_ = false;
    cancelFile_ = QDir::temp().filePath(
        QStringLiteral("wolfmark-setup-cancel-%1.flag").arg(QCoreApplication::applicationPid()));
    QFile::remove(cancelFile_);
    state_.options = options;
    window_->showProgress(action);
    window_->setProgress(12, QStringLiteral("Preparing a recoverable transaction..."));
    start(operationArguments(action, options), action, false);
}

void MaintenanceController::cancel() {
    cancelRequested_ = true;
    if (!cancelFile_.isEmpty()) {
        QFile file(cancelFile_);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.close();
    }
    if (window_) {
        window_->setProgress(70, QStringLiteral("Cancellation requested. Finishing the current safe transaction step..."));
    }
}

void MaintenanceController::quit(DWORD exitCode) {
    exitCode_ = static_cast<int>(exitCode);
    QApplication::exit(exitCode_);
}

QStringList MaintenanceController::detectArguments() const {
    return {QStringLiteral("detect"), QStringLiteral("--manifest"), paths_.manifest};
}

QStringList MaintenanceController::operationArguments(
    InstallerAction action, const InstallerOptions& options) const {
    switch (action) {
    case InstallerAction::Install:
    case InstallerAction::Update:
    case InstallerAction::Reinstall: {
        const QString operation = action == InstallerAction::Update
            ? QStringLiteral("update")
            : action == InstallerAction::Reinstall ? QStringLiteral("reinstall") : QStringLiteral("install");
        return {
            QStringLiteral("install"),
            QStringLiteral("--payload"), paths_.payload,
            QStringLiteral("--manifest"), paths_.manifest,
            QStringLiteral("--install-dir"), options.installFolder,
            QStringLiteral("--uninstaller"), paths_.uninstaller,
            QStringLiteral("--cancel-file"), cancelFile_,
            QStringLiteral("--file-associations"), boolArgument(options.fileAssociations),
            QStringLiteral("--desktop-shortcut"), boolArgument(options.desktopShortcut),
            QStringLiteral("--operation"), operation,
        };
    }
    case InstallerAction::Repair:
        return {QStringLiteral("repair"), QStringLiteral("--uninstaller"), paths_.uninstaller};
    case InstallerAction::Modify:
        return {
            QStringLiteral("modify"),
            QStringLiteral("--file-associations"), boolArgument(options.fileAssociations),
            QStringLiteral("--desktop-shortcut"), boolArgument(options.desktopShortcut),
        };
    case InstallerAction::Uninstall:
        return {QStringLiteral("uninstall")};
    case InstallerAction::Diagnose:
        return {QStringLiteral("diagnose")};
    default:
        return {};
    }
}

void MaintenanceController::start(
    const QStringList& arguments, InstallerAction action, bool detection) {
    process_.setProgram(paths_.maintenance);
    process_.setArguments(arguments);
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&process_, &QProcess::finished, this,
            [this, action, detection](int exitCode, QProcess::ExitStatus status) {
        const QByteArray output = process_.readAllStandardOutput();
        const QString errors = QString::fromUtf8(process_.readAllStandardError()).trimmed();
        process_.disconnect(this);
        if (status != QProcess::NormalExit || exitCode != 0) {
            exitCode_ = ERROR_INSTALL_FAILURE;
            if (window_) {
                window_->showFailure(
                    QStringLiteral("Wolfmark setup could not complete the requested operation."),
                    errors.isEmpty() ? QStringLiteral("The maintenance core exited unexpectedly.") : errors);
            }
            return;
        }
        if (detection) {
            if (!parseDetection(output, true) && window_) {
                window_->showFailure(QStringLiteral("Wolfmark setup state is unreadable."),
                                     QStringLiteral("The maintenance core returned invalid status data."));
            }
            return;
        }
        if (window_) {
            window_->setProgress(100, QStringLiteral("Transaction committed and verified."));
            window_->showComplete(action, state_);
        }
        if (cancelRequested_) quit(ERROR_CANCELLED);
    });
    process_.start();
}

bool MaintenanceController::parseDetection(const QByteArray& output, bool presentUi) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(output, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const QJsonObject root = document.object();
    const QJsonObject status = root.value(QStringLiteral("status")).toObject();
    statusKind_ = status.value(QStringLiteral("kind")).toString();
    QJsonObject installedBuild;
    if (status.contains(QStringLiteral("build"))) installedBuild = status.value(QStringLiteral("build")).toObject();
    if (status.contains(QStringLiteral("installed"))) installedBuild = status.value(QStringLiteral("installed")).toObject();
    state_.installedVersion = buildField(installedBuild, "version");
    state_.installedBuildId = buildField(installedBuild, "buildId");

    const QJsonObject state = root.value(QStringLiteral("state")).toObject();
    const QJsonObject options = state.value(QStringLiteral("options")).toObject();
    if (!options.isEmpty()) {
        state_.options.installFolder = options.value(QStringLiteral("installDirectory")).toString(paths_.installFolder);
        state_.options.fileAssociations = options.value(QStringLiteral("fileAssociations")).toBool(true);
        state_.options.desktopShortcut = options.value(QStringLiteral("desktopShortcut")).toBool(false);
    }
    state_.installed = statusKind_ != QStringLiteral("absent");
    state_.statusDetail = statusKind_;
    if (statusKind_ == QStringLiteral("damaged")) {
        state_.statusDetail = QStringLiteral("Installed files are damaged; Repair can restore the exact cached build.");
    } else if (statusKind_ == QStringLiteral("partial") || statusKind_ == QStringLiteral("interrupted") || statusKind_ == QStringLiteral("inconsistent")) {
        state_.statusDetail = QStringLiteral("Setup found incomplete state. Reinstall can recover this installation.");
    } else if (statusKind_ == QStringLiteral("diverged")) {
        state_.statusDetail = QStringLiteral("This setup contains a different Git lineage at the same version. Reinstall is explicit and will replace it.");
    }
    if (presentUi) showDetectedState(statusKind_);
    return !statusKind_.isEmpty();
}

InstallerAction MaintenanceController::requestedInstallerAction() const {
    const QString action = requestedAction_.toLower();
    if (action == QStringLiteral("install")) return InstallerAction::Install;
    if (action == QStringLiteral("update")) return InstallerAction::Update;
    if (action == QStringLiteral("repair")) return InstallerAction::Repair;
    if (action == QStringLiteral("reinstall")) return InstallerAction::Reinstall;
    if (action == QStringLiteral("uninstall")) return InstallerAction::Uninstall;
    if (action == QStringLiteral("diagnose")) return InstallerAction::Diagnose;
    if (action == QStringLiteral("maintenance")) return InstallerAction::Modify;
    return InstallerAction::None;
}

InstallerAction MaintenanceController::defaultActionForStatus(const QString& kind) const {
    if (kind == QStringLiteral("absent")) return InstallerAction::Install;
    if (kind == QStringLiteral("older") || kind == QStringLiteral("same-version-installed-ancestor")) return InstallerAction::Update;
    if (kind == QStringLiteral("damaged")) return InstallerAction::Repair;
    if (kind == QStringLiteral("newer")
        || kind == QStringLiteral("same-version-candidate-ancestor")
        || kind == QStringLiteral("diverged")
        || kind == QStringLiteral("dirty-build")) return InstallerAction::None;
    return InstallerAction::Reinstall;
}

void MaintenanceController::showDetectedState(const QString& kind) {
    const InstallerAction requested = requestedInstallerAction();
    if (requested == InstallerAction::Repair || requested == InstallerAction::Reinstall || requested == InstallerAction::Uninstall) {
        window_->showConfirmation(requested, state_);
        return;
    }
    if (kind == QStringLiteral("absent")) {
        state_.activeAction = InstallerAction::Install;
        window_->showInstall(state_);
    } else if (kind == QStringLiteral("older") || kind == QStringLiteral("same-version-installed-ancestor")) {
        window_->showUpdate(state_);
    } else if (kind == QStringLiteral("newer") || kind == QStringLiteral("same-version-candidate-ancestor")) {
        window_->showFailure(
            QStringLiteral("A newer Wolfmark build is installed."),
            QStringLiteral("This setup will not silently downgrade it. Use an explicitly approved reinstall if replacement is intended."));
    } else {
        window_->showMaintenance(state_);
    }
}

QString MaintenanceController::boolArgument(bool value) {
    return value ? QStringLiteral("true") : QStringLiteral("false");
}
