#include <windows.h>
#include <shellapi.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTimer>

#include <algorithm>
#include <vector>

#include "maintenance_controller.h"
#include "setup_window.h"
#include "wolfmark_setup_version.h"

namespace {
QString argumentValue(const QStringList& arguments, const QString& prefix) {
    const QString option = prefix.endsWith(QLatin1Char('=')) ? prefix.chopped(1) : prefix;
    for (qsizetype index = 0; index < arguments.size(); ++index) {
        const QString& argument = arguments.at(index);
        if (argument.startsWith(prefix)) return argument.mid(prefix.size());
        if (argument == option && index + 1 < arguments.size()) return arguments.at(index + 1);
    }
    return {};
}

bool hasArgument(const QStringList& arguments, const QString& value) {
    return std::any_of(arguments.cbegin(), arguments.cend(), [&value](const QString& argument) {
        return argument.compare(value, Qt::CaseInsensitive) == 0;
    });
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int wideArgumentCount = 0;
    wchar_t** wideArguments = CommandLineToArgvW(GetCommandLineW(), &wideArgumentCount);
    if (!wideArguments) return ERROR_BAD_ARGUMENTS;
    std::vector<QByteArray> encodedArguments;
    std::vector<char*> argumentPointers;
    encodedArguments.reserve(static_cast<std::size_t>(wideArgumentCount));
    argumentPointers.reserve(static_cast<std::size_t>(wideArgumentCount));
    for (int index = 0; index < wideArgumentCount; ++index) {
        encodedArguments.push_back(QString::fromWCharArray(wideArguments[index]).toUtf8());
    }
    LocalFree(wideArguments);
    for (QByteArray& argument : encodedArguments) argumentPointers.push_back(argument.data());
    QStringList arguments;
    for (const QByteArray& argument : encodedArguments) arguments.push_back(QString::fromUtf8(argument));

    QApplication application(wideArgumentCount, argumentPointers.data());
    QApplication::setApplicationName(QStringLiteral("Wolfmark Setup"));
    QApplication::setOrganizationName(QStringLiteral("Wolfmark"));
    QApplication::setApplicationVersion(QStringLiteral(WOLFMARK_SETUP_VERSION_STRING));
    QApplication::setWindowIcon(QIcon(
        QCoreApplication::applicationDirPath() + QStringLiteral("/assets/branding/wolfmark-symbol.png")));

    QString smokeState = argumentValue(arguments, QStringLiteral("--ui-smoke="));
    if (smokeState.isEmpty()) smokeState = qEnvironmentVariable("WOLFMARK_SETUP_SMOKE_STATE");
    if (!smokeState.isEmpty()) {
        SetupWindow window(nullptr);
        window.showSmokeState(smokeState);
        window.show();
        QString screenshot = argumentValue(arguments, QStringLiteral("--ui-screenshot="));
        if (screenshot.isEmpty()) screenshot = qEnvironmentVariable("WOLFMARK_SETUP_SCREENSHOT");
        QTimer::singleShot(300, &window, [&application, &window, screenshot] {
            if (!screenshot.isEmpty()) {
                QDir().mkpath(QFileInfo(screenshot).absolutePath());
                if (!window.grab().save(screenshot)) {
                    application.exit(ERROR_WRITE_FAULT);
                    return;
                }
            }
            window.hide();
            application.quit();
        });
        return application.exec();
    }

    MaintenanceController::Paths paths;
    paths.maintenance = argumentValue(arguments, QStringLiteral("--maintenance="));
    paths.payload = argumentValue(arguments, QStringLiteral("--payload="));
    paths.manifest = argumentValue(arguments, QStringLiteral("--manifest="));
    paths.uninstaller = argumentValue(arguments, QStringLiteral("--uninstaller="));
    paths.installFolder = argumentValue(arguments, QStringLiteral("--install-dir="));
    const QString requestedAction = argumentValue(arguments, QStringLiteral("--action="));
    const bool silent = hasArgument(arguments, QStringLiteral("--silent"));
    if (paths.maintenance.isEmpty() || paths.manifest.isEmpty()) return ERROR_BAD_ARGUMENTS;

    MaintenanceController controller(paths, requestedAction, silent, &application);
    if (silent) return controller.runSilent();

    SetupWindow window(&controller);
    controller.attach(&window);
    window.show();
    window.raise();
    window.activateWindow();
    QTimer::singleShot(0, &controller, [&controller] { controller.initialize(); });
    application.exec();
    return controller.exitCode();
}
