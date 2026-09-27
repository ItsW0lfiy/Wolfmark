#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include "installer_host.h"

class SetupWindow;

class MaintenanceController final : public QObject, public InstallerHost {
public:
    struct Paths {
        QString maintenance;
        QString payload;
        QString manifest;
        QString uninstaller;
        QString installFolder;
    };

    MaintenanceController(Paths paths, QString requestedAction, bool silent, QObject* parent = nullptr);

    void attach(SetupWindow* window);
    void initialize();
    int runSilent();

    void begin(InstallerAction action, const InstallerOptions& options) override;
    void cancel() override;
    void quit(DWORD exitCode) override;

    int exitCode() const { return exitCode_; }

private:
    QStringList detectArguments() const;
    QStringList operationArguments(InstallerAction action, const InstallerOptions& options) const;
    void start(const QStringList& arguments, InstallerAction action, bool detection);
    bool parseDetection(const QByteArray& output, bool presentUi);
    InstallerAction requestedInstallerAction() const;
    InstallerAction defaultActionForStatus(const QString& kind) const;
    void showDetectedState(const QString& kind);
    static QString boolArgument(bool value);

    Paths paths_;
    QString requestedAction_;
    bool silent_ = false;
    bool cancelRequested_ = false;
    int exitCode_ = ERROR_SUCCESS;
    SetupWindow* window_ = nullptr;
    QProcess process_;
    InstallerState state_;
    QString statusKind_;
    QString cancelFile_;
};
