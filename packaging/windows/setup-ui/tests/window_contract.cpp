#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QThread>

#include <iostream>
#include <stdexcept>

#include "../src/setup_window.h"
#include "../src/maintenance_controller.h"

namespace {
class RecordingHost final : public InstallerHost {
public:
    void begin(InstallerAction action, const InstallerOptions& options) override {
        action_ = action;
        options_ = options;
        ++calls_;
    }
    void cancel() override {}
    void quit(DWORD) override {}

    InstallerAction action_ = InstallerAction::None;
    InstallerOptions options_;
    int calls_ = 0;
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void click(SetupWindow& window, const QString& text) {
    QApplication::processEvents();
    QPushButton* target = nullptr;
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->isVisible() && button->text() == text) {
            require(target == nullptr, "Ambiguous setup action");
            target = button;
        }
    }
    require(target != nullptr, "Setup action is not visible");
    target->click();
}

InstallerState installedState() {
    InstallerState state;
    state.installed = true;
    state.installedVersion = QStringLiteral("0.1.0-dev.7");
    state.targetVersion = QStringLiteral("0.1.0-dev.7");
    state.options.installFolder = QString::fromUtf16(u"D:/Installed apps/Wolfmark \u03a9");
    state.options.fileAssociations = false;
    state.options.desktopShortcut = true;
    return state;
}

void checkOptions(const RecordingHost& host, InstallerAction action, const InstallerOptions& expected) {
    require(host.calls_ == 1, "Setup must dispatch exactly one operation");
    require(host.action_ == action, "Setup dispatched the wrong operation");
    require(host.options_.installFolder == expected.installFolder,
            "Setup lost or replaced the installation directory");
    require(host.options_.fileAssociations == expected.fileAssociations,
            "Setup lost the file-association choice");
    require(host.options_.desktopShortcut == expected.desktopShortcut,
            "Setup lost the desktop-shortcut choice");
}

void updateFromFreshWindow() {
    RecordingHost host;
    SetupWindow window(&host);
    const InstallerState state = installedState();
    window.showUpdate(state);
    window.show();
    click(window, QStringLiteral("Update Wolfmark"));
    checkOptions(host, InstallerAction::Update, state.options);
}

void updateAfterAnotherInstallPage() {
    RecordingHost host;
    SetupWindow window(&host);
    InstallerState previous;
    previous.activeAction = InstallerAction::Install;
    previous.options.installFolder = QStringLiteral("C:/Different install target");
    window.showInstall(previous);
    const InstallerState state = installedState();
    window.showUpdate(state);
    window.show();
    click(window, QStringLiteral("Update Wolfmark"));
    checkOptions(host, InstallerAction::Update, state.options);
}

void installUsesSelectedDirectory() {
    RecordingHost host;
    SetupWindow window(&host);
    InstallerState state;
    state.activeAction = InstallerAction::Install;
    state.options.installFolder = QStringLiteral("C:/Program Files/Wolfmark");
    state.options.fileAssociations = true;
    window.showInstall(state);
    window.show();
    QLineEdit* location = window.findChild<QLineEdit*>();
    require(location != nullptr, "Install directory control missing");
    InstallerOptions selected = state.options;
    selected.installFolder = QString::fromUtf16(u"D:/Chosen folder/Wolfmark \u03a9");
    location->setText(selected.installFolder);
    click(window, QStringLiteral("Install Wolfmark"));
    checkOptions(host, InstallerAction::Install, selected);
}

void updateUsesCurrentCheckboxChoices() {
    RecordingHost host;
    SetupWindow window(&host);
    const InstallerState state = installedState();
    window.showUpdate(state);
    window.show();
    for (QCheckBox* box : window.findChildren<QCheckBox*>()) {
        if (box->isVisible()) box->setChecked(!box->isChecked());
    }
    InstallerOptions selected = state.options;
    selected.fileAssociations = true;
    selected.desktopShortcut = false;
    click(window, QStringLiteral("Update Wolfmark"));
    checkOptions(host, InstallerAction::Update, selected);
}

template <typename Predicate>
void waitFor(Predicate predicate, const char* failure) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 10000) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    require(predicate(), failure);
}

void detectedUpdateReachesCore() {
    MaintenanceController::Paths paths;
    paths.maintenance = qEnvironmentVariable("WOLFMARK_TEST_UI_CORE");
    paths.manifest = qEnvironmentVariable("WOLFMARK_TEST_UI_MANIFEST");
    paths.payload = QApplication::applicationDirPath();
    paths.uninstaller = QApplication::applicationFilePath();
    paths.installFolder = QStringLiteral("C:/Program Files/Wolfmark");
    require(!paths.maintenance.isEmpty(), "Recording maintenance core is required");
    MaintenanceController controller(paths, {}, false);
    SetupWindow window(&controller);
    controller.attach(&window);
    window.setAttribute(Qt::WA_ShowWithoutActivating);
    window.show();
    controller.initialize();
    waitFor([&] { return controller.detectionFinished(); }, "Core detection timed out");
    require(controller.exitCode() == 0, "Core detection failed");
    click(window, QStringLiteral("Update Wolfmark"));
    waitFor([&] {
        for (QPushButton* button : window.findChildren<QPushButton*>()) {
            if (button->isVisible() && button->text() == QStringLiteral("Finish")) return true;
        }
        return false;
    }, "Interactive update did not complete through the recording core");
    require(controller.exitCode() == 0, "Interactive core operation failed");
}
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    try {
        updateFromFreshWindow();
        updateAfterAnotherInstallPage();
        installUsesSelectedDirectory();
        updateUsesCurrentCheckboxChoices();
        detectedUpdateReachesCore();
        std::cout << "Native setup action contracts passed (fresh/stale update, install target, option changes, detected update through core).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Native setup action contract failed: " << error.what() << '\n';
        return 1;
    }
}
