#include "setup_window.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

#include "burn_controller.h"

namespace {
class WolfmarkSymbol final : public QWidget {
public:
    explicit WolfmarkSymbol(int size = 44, QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(size, size);
        setAccessibleName(QStringLiteral("Wolfmark paw"));
        source_.load(QApplication::applicationDirPath() +
                     QStringLiteral("/assets/branding/wolfmark-symbol.png"));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (!source_.isNull()) {
            painter.drawPixmap(rect(), source_);
        }
    }

private:
    QPixmap source_;
};

QPushButton* makeButton(const QString& text, bool primary = false) {
    auto* button = new QPushButton(text);
    button->setProperty("primary", primary);
    button->setMinimumHeight(38);
    button->setCursor(Qt::PointingHandCursor);
    button->setAccessibleName(text);
    return button;
}

QFrame* separator() {
    auto* line = new QFrame;
    line->setObjectName(QStringLiteral("separator"));
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    return line;
}
}  // namespace

SetupWindow::SetupWindow(BurnController* controller) : controller_(controller) {
    setObjectName(QStringLiteral("setupWindow"));
    setWindowTitle(QStringLiteral("Wolfmark Setup"));
    setFixedSize(820, 560);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setAccessibleName(QStringLiteral("Wolfmark Setup"));

    pages_ = new QStackedWidget(this);
    installPage_ = createInstallPage();
    maintenancePage_ = createMaintenancePage();
    confirmationPage_ = createConfirmationPage();
    progressPage_ = createProgressPage();
    completePage_ = createCompletePage();
    errorPage_ = createErrorPage();
    for (QWidget* page : {installPage_, maintenancePage_, confirmationPage_, progressPage_, completePage_, errorPage_}) {
        pages_->addWidget(page);
    }

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* brand_panel = new QWidget;
    brand_panel->setObjectName(QStringLiteral("brandPanel"));
    brand_panel->setAttribute(Qt::WA_StyledBackground);
    brand_panel->setFixedWidth(205);
    auto* brand_layout = new QVBoxLayout(brand_panel);
    brand_layout->setContentsMargins(26, 38, 26, 28);
    brand_layout->setSpacing(12);
    auto* brand_symbol = new WolfmarkSymbol(88);
    brand_layout->addWidget(brand_symbol, 0, Qt::AlignHCenter);
    auto* brand_name = new QLabel(QStringLiteral("Wolfmark"));
    brand_name->setObjectName(QStringLiteral("brandName"));
    brand_name->setAlignment(Qt::AlignHCenter);
    brand_layout->addWidget(brand_name);
    auto* brand_detail = new QLabel(QStringLiteral("Viewer-first.\nFree and open source."));
    brand_detail->setObjectName(QStringLiteral("brandDetail"));
    brand_detail->setAlignment(Qt::AlignHCenter);
    brand_layout->addWidget(brand_detail);
    brand_layout->addStretch();
    auto* brand_footer = new QLabel(QStringLiteral("WOLFMARK SETUP"));
    brand_footer->setObjectName(QStringLiteral("eyebrow"));
    brand_footer->setAlignment(Qt::AlignHCenter);
    brand_layout->addWidget(brand_footer);
    root->addWidget(brand_panel);
    pages_->setObjectName(QStringLiteral("setupPages"));
    root->addWidget(pages_, 1);
    applyStyle();
}

QWidget* SetupWindow::createIdentityHeader(const QString& eyebrow, const QString& title, const QString& detail) {
    auto* container = new QWidget;
    auto* row = new QHBoxLayout(container);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto* copy = new QVBoxLayout;
    copy->setSpacing(4);
    auto* eyebrowLabel = new QLabel(eyebrow);
    eyebrowLabel->setObjectName(QStringLiteral("eyebrow"));
    auto* titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("pageTitle"));
    auto* detailLabel = new QLabel(detail);
    detailLabel->setObjectName(QStringLiteral("pageDetail"));
    detailLabel->setWordWrap(true);
    copy->addWidget(eyebrowLabel);
    copy->addWidget(titleLabel);
    copy->addWidget(detailLabel);
    row->addLayout(copy, 1);
    return container;
}

QWidget* SetupWindow::createInstallPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 34, 42, 34);
    layout->setSpacing(18);
    auto* header = createIdentityHeader(
        QStringLiteral("WOLFMARK SETUP"),
        QStringLiteral("Install Wolfmark"),
        QStringLiteral("A native Markdown experience without the clutter."));
    const auto labels = header->findChildren<QLabel*>();
    installEyebrow_ = labels.at(0);
    installTitle_ = labels.at(1);
    installDetail_ = labels.at(2);
    layout->addWidget(header);
    layout->addWidget(separator());

    locationLabel_ = new QLabel(QStringLiteral("Install location"));
    locationLabel_->setObjectName(QStringLiteral("sectionLabel"));
    layout->addWidget(locationLabel_);
    auto* locationRow = new QHBoxLayout;
    locationRow->setSpacing(10);
    locationEdit_ = new QLineEdit;
    locationEdit_->setReadOnly(true);
    locationEdit_->setAccessibleName(QStringLiteral("Wolfmark install location"));
    locationButton_ = makeButton(QStringLiteral("Change"));
    connect(locationButton_, &QPushButton::clicked, this, [this] { chooseInstallFolder(); });
    locationRow->addWidget(locationEdit_, 1);
    locationRow->addWidget(locationButton_);
    layout->addLayout(locationRow);

    associationsCheck_ = new QCheckBox(QStringLiteral("Add Wolfmark to Open With"));
    desktopCheck_ = new QCheckBox(QStringLiteral("Create a desktop shortcut"));
    associationsCheck_->setAccessibleName(QStringLiteral("Add Wolfmark to Open With"));
    desktopCheck_->setAccessibleName(QStringLiteral("Create a desktop shortcut"));
    layout->addWidget(associationsCheck_);
    layout->addWidget(desktopCheck_);
    layout->addStretch(1);

    auto* actions = new QHBoxLayout;
    actions->addStretch(1);
    primaryButton_ = makeButton(QStringLiteral("Install Wolfmark"), true);
    connect(primaryButton_, &QPushButton::clicked, this, [this] {
        if (controller_) {
            controller_->begin(state_.activeAction, optionsFromControls());
        } else {
            showProgress(state_.activeAction == InstallerAction::Update ? InstallerAction::Update : InstallerAction::Install);
        }
    });
    actions->addWidget(primaryButton_);
    layout->addLayout(actions);
    return page;
}

QWidget* SetupWindow::createMaintenancePage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 34, 42, 34);
    layout->setSpacing(16);
    layout->addWidget(createIdentityHeader(
        QStringLiteral("WOLFMARK SETUP"),
        QStringLiteral("Wolfmark is installed"),
        QStringLiteral("Maintain the application without affecting your documents or settings.")));
    layout->addWidget(separator());
    maintenanceVersion_ = new QLabel;
    maintenanceVersion_->setObjectName(QStringLiteral("metadata"));
    layout->addWidget(maintenanceVersion_);
    auto* repair = makeButton(QStringLiteral("Repair installation"));
    auto* modify = makeButton(QStringLiteral("Change installation options"));
    auto* uninstall = makeButton(QStringLiteral("Uninstall Wolfmark"));
    uninstall->setProperty("danger", true);
    connect(repair, &QPushButton::clicked, this, [this] { showConfirmation(InstallerAction::Repair, state_); });
    connect(modify, &QPushButton::clicked, this, [this] {
        state_.activeAction = InstallerAction::Modify;
        showInstall(state_);
    });
    connect(uninstall, &QPushButton::clicked, this, [this] { showConfirmation(InstallerAction::Uninstall, state_); });
    layout->addWidget(repair);
    layout->addWidget(modify);
    layout->addWidget(uninstall);
    layout->addStretch(1);
    auto* close = makeButton(QStringLiteral("Close"));
    connect(close, &QPushButton::clicked, this, &QWidget::close);
    auto* actions = new QHBoxLayout;
    actions->addStretch(1);
    actions->addWidget(close);
    layout->addLayout(actions);
    return page;
}

QWidget* SetupWindow::createConfirmationPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 34, 42, 34);
    layout->setSpacing(16);
    auto* header = createIdentityHeader(QStringLiteral("WOLFMARK SETUP"), QString(), QString());
    const auto labels = header->findChildren<QLabel*>();
    confirmationTitle_ = labels.at(1);
    confirmationDetail_ = labels.at(2);
    layout->addWidget(header);
    layout->addWidget(separator());
    auto* assurance = new QLabel(QStringLiteral(
        "Your Markdown and text documents will not be changed. Per-user Wolfmark settings are preserved."));
    assurance->setObjectName(QStringLiteral("assurance"));
    assurance->setWordWrap(true);
    layout->addWidget(assurance);
    layout->addStretch(1);
    auto* cancel = makeButton(QStringLiteral("Cancel"));
    confirmationButton_ = makeButton(QStringLiteral("Continue"), true);
    connect(cancel, &QPushButton::clicked, this, [this] { showMaintenance(state_); });
    connect(confirmationButton_, &QPushButton::clicked, this, [this] {
        if (controller_) {
            controller_->begin(confirmationAction_, state_.options);
        } else {
            showProgress(confirmationAction_);
        }
    });
    auto* actions = new QHBoxLayout;
    actions->addStretch(1);
    actions->addWidget(cancel);
    actions->addWidget(confirmationButton_);
    layout->addLayout(actions);
    return page;
}

QWidget* SetupWindow::createProgressPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 48, 42, 48);
    layout->setSpacing(20);
    auto* header = createIdentityHeader(QStringLiteral("WOLFMARK SETUP"), QStringLiteral("Installing Wolfmark"), QString());
    const auto labels = header->findChildren<QLabel*>();
    progressTitle_ = labels.at(1);
    progressDetail_ = labels.at(2);
    layout->addWidget(header);
    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    progressBar_->setTextVisible(false);
    progressBar_->setAccessibleName(QStringLiteral("Installation progress"));
    layout->addSpacing(18);
    layout->addWidget(progressBar_);
    layout->addStretch(1);
    auto* cancel = makeButton(QStringLiteral("Cancel"));
    connect(cancel, &QPushButton::clicked, this, [this] {
        if (controller_) {
            controller_->cancel();
        }
    });
    auto* actions = new QHBoxLayout;
    actions->addStretch(1);
    actions->addWidget(cancel);
    layout->addLayout(actions);
    return page;
}

QWidget* SetupWindow::createCompletePage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 48, 42, 42);
    layout->setSpacing(18);
    auto* header = createIdentityHeader(
        QStringLiteral("WOLFMARK SETUP"),
        QStringLiteral("Wolfmark is ready"),
        QStringLiteral("Installation completed successfully."));
    const auto labels = header->findChildren<QLabel*>();
    completeTitle_ = labels.at(1);
    completeDetail_ = labels.at(2);
    layout->addWidget(header);
    layout->addWidget(separator());
    launchCheck_ = new QCheckBox(QStringLiteral("Launch Wolfmark"));
    launchCheck_->setChecked(true);
    layout->addWidget(launchCheck_);
    layout->addStretch(1);
    auto* finish = makeButton(QStringLiteral("Finish"), true);
    connect(finish, &QPushButton::clicked, this, [this] {
        if (launchCheck_->isVisible() && launchCheck_->isChecked()) {
            QProcess::startDetached(state_.options.installFolder + QStringLiteral("/Wolfmark.exe"));
        }
        if (controller_) {
            controller_->quit();
        }
        QApplication::quit();
    });
    auto* actions = new QHBoxLayout;
    actions->addStretch(1);
    actions->addWidget(finish);
    layout->addLayout(actions);
    return page;
}

QWidget* SetupWindow::createErrorPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(42, 42, 42, 42);
    layout->setSpacing(16);
    auto* header = createIdentityHeader(
        QStringLiteral("WOLFMARK SETUP"), QStringLiteral("Wolfmark could not be changed"), QString());
    errorSummary_ = header->findChildren<QLabel*>().at(2);
    layout->addWidget(header);
    layout->addWidget(separator());
    errorDetails_ = new QLabel;
    errorDetails_->setObjectName(QStringLiteral("errorDetails"));
    errorDetails_->setWordWrap(true);
    errorDetails_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    layout->addWidget(errorDetails_);
    layout->addStretch(1);
    auto* close = makeButton(QStringLiteral("Close"), true);
    connect(close, &QPushButton::clicked, this, [this] {
        if (controller_) {
            controller_->quit(ERROR_INSTALL_FAILURE);
        }
        QApplication::quit();
    });
    auto* actions = new QHBoxLayout;
    actions->addStretch(1);
    actions->addWidget(close);
    layout->addLayout(actions);
    return page;
}

void SetupWindow::showInstall(const InstallerState& state) {
    state_ = state;
    const bool modifying = state.activeAction == InstallerAction::Modify;
    installEyebrow_->setText(modifying ? QStringLiteral("WOLFMARK MAINTENANCE") : QStringLiteral("WOLFMARK SETUP"));
    installTitle_->setText(modifying ? QStringLiteral("Change Wolfmark") : QStringLiteral("Install Wolfmark"));
    installDetail_->setText(modifying
        ? QStringLiteral("Choose the Windows integration Wolfmark should maintain.")
        : QStringLiteral("A native Markdown experience without the clutter."));
    locationLabel_->setVisible(!modifying);
    locationEdit_->setVisible(!modifying);
    locationButton_->setVisible(!modifying);
    locationEdit_->setText(state.options.installFolder);
    associationsCheck_->setChecked(state.options.fileAssociations);
    desktopCheck_->setChecked(state.options.desktopShortcut);
    primaryButton_->setText(modifying ? QStringLiteral("Apply changes") : QStringLiteral("Install Wolfmark"));
    pages_->setCurrentWidget(installPage_);
    primaryButton_->setFocus();
}

void SetupWindow::showUpdate(const InstallerState& state) {
    state_ = state;
    state_.activeAction = InstallerAction::Update;
    installEyebrow_->setText(QStringLiteral("WOLFMARK UPDATE"));
    installTitle_->setText(QStringLiteral("Update Wolfmark"));
    installDetail_->setText(QStringLiteral(
        "Installed: %1\nNew version: %2\n\nYour settings and documents will be preserved.")
        .arg(state.installedVersion, state.targetVersion));
    locationLabel_->setVisible(false);
    locationEdit_->setVisible(false);
    locationButton_->setVisible(false);
    associationsCheck_->setChecked(state.options.fileAssociations);
    desktopCheck_->setChecked(state.options.desktopShortcut);
    primaryButton_->setText(QStringLiteral("Update Wolfmark"));
    pages_->setCurrentWidget(installPage_);
    primaryButton_->setFocus();
}

void SetupWindow::showMaintenance(const InstallerState& state) {
    state_ = state;
    maintenanceVersion_->setText(QStringLiteral("Installed version  %1").arg(state.installedVersion));
    pages_->setCurrentWidget(maintenancePage_);
}

void SetupWindow::showConfirmation(InstallerAction action, const InstallerState& state) {
    state_ = state;
    confirmationAction_ = action;
    const bool repair = action == InstallerAction::Repair;
    confirmationTitle_->setText(repair ? QStringLiteral("Repair Wolfmark") : QStringLiteral("Uninstall Wolfmark?"));
    confirmationDetail_->setText(repair
        ? QStringLiteral("Wolfmark will verify and restore its installed files.")
        : QStringLiteral("Wolfmark itself will be removed."));
    confirmationButton_->setText(repair ? QStringLiteral("Repair") : QStringLiteral("Uninstall"));
    confirmationButton_->setProperty("danger", !repair);
    confirmationButton_->style()->unpolish(confirmationButton_);
    confirmationButton_->style()->polish(confirmationButton_);
    pages_->setCurrentWidget(confirmationPage_);
    confirmationButton_->setFocus();
}

void SetupWindow::showProgress(InstallerAction action) {
    state_.activeAction = action;
    state_.applying = true;
    progressTitle_->setText(actionTitle(action));
    progressDetail_->setText(actionProgressDetail(action));
    progressBar_->setValue(0);
    pages_->setCurrentWidget(progressPage_);
}

void SetupWindow::setProgress(int percent, const QString& detail) {
    progressBar_->setValue(percent);
    if (!detail.isEmpty()) {
        progressDetail_->setText(detail);
    }
}

void SetupWindow::showComplete(InstallerAction action, const InstallerState& state) {
    state_ = state;
    state_.applying = false;
    switch (action) {
    case InstallerAction::Update:
        completeTitle_->setText(QStringLiteral("Wolfmark has been updated"));
        completeDetail_->setText(QStringLiteral("The latest Wolfmark development build is ready."));
        break;
    case InstallerAction::Repair:
        completeTitle_->setText(QStringLiteral("Wolfmark has been repaired"));
        completeDetail_->setText(QStringLiteral("Installed Wolfmark files were verified and restored."));
        break;
    case InstallerAction::Modify:
        completeTitle_->setText(QStringLiteral("Wolfmark options were updated"));
        completeDetail_->setText(QStringLiteral("Windows integration now matches your choices."));
        break;
    case InstallerAction::Uninstall:
        completeTitle_->setText(QStringLiteral("Wolfmark has been removed"));
        completeDetail_->setText(QStringLiteral("Your documents and per-user settings were preserved."));
        break;
    default:
        completeTitle_->setText(QStringLiteral("Wolfmark is ready"));
        completeDetail_->setText(QStringLiteral("Installation completed successfully."));
        break;
    }
    launchCheck_->setVisible(action == InstallerAction::Install || action == InstallerAction::Update);
    pages_->setCurrentWidget(completePage_);
}

void SetupWindow::showFailure(const QString& summary, const QString& details) {
    state_.applying = false;
    errorSummary_->setText(summary);
    errorDetails_->setText(details);
    pages_->setCurrentWidget(errorPage_);
}

void SetupWindow::showSmokeState(const QString& stateName) {
    InstallerState smoke;
    smoke.installed = true;
    smoke.installedVersion = QStringLiteral("0.1.0-dev.6");
    smoke.targetVersion = QStringLiteral("0.1.0-dev.7");
    smoke.options.installFolder = QStringLiteral("C:/Program Files/Wolfmark");
    smoke.options.fileAssociations = true;
    if (stateName == QStringLiteral("install")) {
        smoke.activeAction = InstallerAction::Install;
        showInstall(smoke);
    } else if (stateName == QStringLiteral("upgrade")) {
        showUpdate(smoke);
    } else if (stateName == QStringLiteral("maintenance")) {
        showMaintenance(smoke);
    } else if (stateName == QStringLiteral("repair")) {
        showConfirmation(InstallerAction::Repair, smoke);
    } else if (stateName == QStringLiteral("uninstall")) {
        showConfirmation(InstallerAction::Uninstall, smoke);
    } else if (stateName == QStringLiteral("progress")) {
        showProgress(InstallerAction::Install);
        setProgress(58, QStringLiteral("Installing application files..."));
    } else if (stateName == QStringLiteral("complete")) {
        showComplete(InstallerAction::Install, smoke);
    } else {
        showFailure(
            QStringLiteral("Wolfmark could not be installed"),
            QStringLiteral("Error 0x80070643\nThe installer log contains diagnostic details."));
    }
}

HWND SetupWindow::nativeHandle() {
    return reinterpret_cast<HWND>(winId());
}

void SetupWindow::closeEvent(QCloseEvent* event) {
    if (state_.applying) {
        const auto answer = QMessageBox::question(
            this,
            QStringLiteral("Cancel Wolfmark setup?"),
            QStringLiteral("Wolfmark will stop safely and roll back incomplete changes."));
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        if (controller_) {
            controller_->cancel();
        }
        event->ignore();
        return;
    }
    if (controller_) {
        controller_->quit(ERROR_INSTALL_USEREXIT);
    }
    event->accept();
}

void SetupWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && !state_.applying) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

InstallerOptions SetupWindow::optionsFromControls() const {
    InstallerOptions options;
    options.installFolder = locationEdit_->text();
    options.fileAssociations = associationsCheck_->isChecked();
    options.desktopShortcut = desktopCheck_->isChecked();
    return options;
}

void SetupWindow::chooseInstallFolder() {
    const QString folder = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose Wolfmark install location"), locationEdit_->text());
    if (!folder.isEmpty()) {
        locationEdit_->setText(folder);
    }
}

QString SetupWindow::actionTitle(InstallerAction action) const {
    switch (action) {
    case InstallerAction::Update: return QStringLiteral("Updating Wolfmark");
    case InstallerAction::Modify: return QStringLiteral("Changing Wolfmark");
    case InstallerAction::Repair: return QStringLiteral("Repairing Wolfmark");
    case InstallerAction::Uninstall: return QStringLiteral("Removing Wolfmark");
    default: return QStringLiteral("Installing Wolfmark");
    }
}

QString SetupWindow::actionProgressDetail(InstallerAction action) const {
    switch (action) {
    case InstallerAction::Update: return QStringLiteral("Preparing the Wolfmark update...");
    case InstallerAction::Modify: return QStringLiteral("Applying installation options...");
    case InstallerAction::Repair: return QStringLiteral("Verifying installed files...");
    case InstallerAction::Uninstall: return QStringLiteral("Removing Wolfmark-owned files...");
    default: return QStringLiteral("Preparing Wolfmark...");
    }
}

void SetupWindow::applyStyle() {
    qApp->setStyleSheet(QStringLiteral(R"(
        QWidget#setupWindow { background: #101010; color: #e9e7e6; font-family: "Segoe UI Variable Text", "Segoe UI"; font-size: 10pt; }
        QWidget#brandPanel { background: #171717; border-right: 1px solid #343434; }
        QStackedWidget#setupPages { background: #101010; }
        QLabel#brandName { color: #f1efed; font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 23pt; font-weight: 600; }
        QLabel#brandDetail { color: #9c9895; font-size: 9pt; }
        QLabel#eyebrow { color: #a09c99; font-size: 8pt; font-weight: 600; letter-spacing: 2px; }
        QLabel#pageTitle { color: #f2efed; font-family: "Georgia", "Cambria", "Segoe UI"; font-size: 24pt; font-weight: 600; }
        QLabel#pageDetail { color: #aaa6a3; font-size: 10pt; }
        QLabel#sectionLabel { color: #d0cdca; font-weight: 600; }
        QLabel#metadata { color: #9c9895; padding: 5px 0; }
        QLabel#assurance, QLabel#errorDetails { background: #1b1b1b; border: 1px solid #3b3b3b; border-radius: 9px; padding: 15px; color: #c5c1be; }
        QFrame#separator { background: #343434; border: none; }
        QLineEdit { background: #1b1b1b; border: 1px solid #414141; border-radius: 7px; padding: 9px 11px; color: #e9e7e6; selection-background-color: #69313c; selection-color: #ffffff; }
        QLineEdit:focus { border-color: #d62d4e; }
        QPushButton { background: #202020; border: 1px solid #414141; border-radius: 7px; color: #d8d4d1; padding: 8px 16px; }
        QPushButton:hover { background: #2b2b2b; border-color: #5c5c5c; }
        QPushButton:pressed { background: #342326; border-color: #b8213f; }
        QPushButton:focus { border: 2px solid #d62d4e; padding: 7px 15px; }
        QPushButton[primary="true"] { background: #d62d4e; color: #ffffff; border-color: #d62d4e; font-weight: 600; }
        QPushButton[primary="true"]:hover { background: #ea3d5f; border-color: #ea3d5f; }
        QPushButton[primary="true"]:pressed { background: #b8213f; border-color: #b8213f; }
        QPushButton[danger="true"] { color: #e8909c; }
        QPushButton[danger="true"]:hover { background: #422127; border-color: #d45a64; }
        QCheckBox { spacing: 9px; color: #d0cdca; }
        QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid #5b5b5b; border-radius: 5px; background: #1a1a1a; }
        QCheckBox::indicator:hover { border-color: #d62d4e; }
        QCheckBox::indicator:checked { background: #d62d4e; border-color: #ea3d5f; }
        QCheckBox:focus { outline: none; color: #ffffff; }
        QProgressBar { background: #1b1b1b; border: 1px solid #3c3c3c; border-radius: 5px; min-height: 11px; max-height: 11px; }
        QProgressBar::chunk { background: #d62d4e; border-radius: 4px; }
        QMessageBox { background: #101010; }
    )"));
}
