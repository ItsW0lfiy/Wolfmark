#pragma once

#include <windows.h>

#include <QWidget>
#include <QString>
#include <QStringList>

#include "installer_host.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;

class SetupWindow final : public QWidget {
public:
    explicit SetupWindow(InstallerHost* host, const QString& resourceRoot = QString());

    void showInstall(const InstallerState& state);
    void showUpdate(const InstallerState& state);
    void showMaintenance(const InstallerState& state);
    void showConfirmation(InstallerAction action, const InstallerState& state);
    void showProgress(InstallerAction action);
    void setProgress(int percent, const QString& detail);
    void showComplete(InstallerAction action, const InstallerState& state);
    void showFailure(const QString& summary, const QString& details);
    int promptFilesInUse(const QStringList& files, bool restartManager);
    void showSmokeState(const QString& stateName);
    HWND nativeHandle();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QWidget* createIdentityHeader(const QString& eyebrow, const QString& title, const QString& detail);
    QWidget* createInstallPage();
    QWidget* createMaintenancePage();
    QWidget* createConfirmationPage();
    QWidget* createProgressPage();
    QWidget* createCompletePage();
    QWidget* createErrorPage();
    InstallerOptions optionsFromControls() const;
    void chooseInstallFolder();
    void applyStyle();
    QString actionTitle(InstallerAction action) const;
    QString actionProgressDetail(InstallerAction action) const;

    InstallerHost* host_ = nullptr;
    InstallerState state_;
    InstallerAction confirmationAction_ = InstallerAction::None;
    QStackedWidget* pages_ = nullptr;
    QWidget* installPage_ = nullptr;
    QLabel* installEyebrow_ = nullptr;
    QLabel* installTitle_ = nullptr;
    QLabel* installDetail_ = nullptr;
    QLabel* locationLabel_ = nullptr;
    QLineEdit* locationEdit_ = nullptr;
    QPushButton* locationButton_ = nullptr;
    QCheckBox* associationsCheck_ = nullptr;
    QCheckBox* desktopCheck_ = nullptr;
    QPushButton* primaryButton_ = nullptr;
    QWidget* maintenancePage_ = nullptr;
    QLabel* maintenanceVersion_ = nullptr;
    QWidget* confirmationPage_ = nullptr;
    QLabel* confirmationTitle_ = nullptr;
    QLabel* confirmationDetail_ = nullptr;
    QPushButton* confirmationButton_ = nullptr;
    QWidget* progressPage_ = nullptr;
    QLabel* progressTitle_ = nullptr;
    QLabel* progressDetail_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QWidget* completePage_ = nullptr;
    QLabel* completeTitle_ = nullptr;
    QLabel* completeDetail_ = nullptr;
    QCheckBox* launchCheck_ = nullptr;
    QWidget* errorPage_ = nullptr;
    QLabel* errorSummary_ = nullptr;
    QLabel* errorDetails_ = nullptr;
};
