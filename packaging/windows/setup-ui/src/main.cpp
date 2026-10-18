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
#include <exception>

#include "maintenance_controller.h"
#include "setup_window.h"
#include "wolfmark_setup_version.h"
#include "bootstrap_log.h"

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

QString validateArguments(const QStringList& arguments) {
    const QStringList values = {QStringLiteral("--maintenance"), QStringLiteral("--payload"),
        QStringLiteral("--manifest"), QStringLiteral("--uninstaller"), QStringLiteral("--install-dir"),
        QStringLiteral("--action"), QStringLiteral("--setup-source"), QStringLiteral("--bootstrap-log"),
        QStringLiteral("--ui-smoke"), QStringLiteral("--ui-screenshot"), QStringLiteral("--startup-check")};
    QStringList seen;
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        const QString option = argument.section(QLatin1Char('='), 0, 0);
        if (seen.contains(option)) return QStringLiteral("Duplicate startup option.");
        seen.push_back(option);
        if (argument == QStringLiteral("--silent")) continue;
        if (!values.contains(option)) return QStringLiteral("Unknown startup option (value redacted).");
        if (!argument.contains(QLatin1Char('='))) {
            if (++index >= arguments.size() || arguments.at(index).startsWith(QStringLiteral("--"))) {
                return QStringLiteral("Missing value for %1.").arg(option);
            }
        }
        BootstrapLog::write(QStringLiteral("argument %1=%2").arg(option, argumentValue(arguments, option + QLatin1Char('='))));
    }
    const QString action = argumentValue(arguments, QStringLiteral("--action="));
    const QStringList actions = {QString(), QStringLiteral("install"), QStringLiteral("maintenance"),
        QStringLiteral("update"), QStringLiteral("repair"), QStringLiteral("reinstall"),
        QStringLiteral("uninstall"), QStringLiteral("diagnose")};
    if (!actions.contains(action.toLower())) return QStringLiteral("Unsupported setup action.");
    return {};
}

int startupFailure(const QString& message, int code, bool showDialog) {
    BootstrapLog::write(QStringLiteral("stage=startup-failure exit=%1 message=%2").arg(code).arg(message));
    if (showDialog) {
        const QString text = QStringLiteral("Wolfmark Setup could not start.\n\n%1\n\nExit code: %2\nDiagnostic log: %3")
            .arg(message).arg(code).arg(BootstrapLog::path);
        MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(text.utf16()), L"Wolfmark Setup", MB_OK | MB_ICONERROR);
    }
    return code;
}
}

int runSetup() {
    int wideArgumentCount = 0;
    wchar_t** wideArguments = CommandLineToArgvW(GetCommandLineW(), &wideArgumentCount);
    if (!wideArguments) return startupFailure(QStringLiteral("Windows command-line decoding failed."), ERROR_BAD_ARGUMENTS, true);
    std::vector<QByteArray> encodedArguments;
    std::vector<char*> argumentPointers;
    encodedArguments.reserve(static_cast<std::size_t>(wideArgumentCount));
    argumentPointers.reserve(static_cast<std::size_t>(wideArgumentCount) + 1);
    for (int index = 0; index < wideArgumentCount; ++index) {
        encodedArguments.push_back(QString::fromWCharArray(wideArguments[index]).toUtf8());
    }
    LocalFree(wideArguments);
    for (QByteArray& argument : encodedArguments) argumentPointers.push_back(argument.data());
    argumentPointers.push_back(nullptr); // QApplication requires argv[argc] == nullptr.
    QStringList arguments;
    for (const QByteArray& argument : encodedArguments) arguments.push_back(QString::fromUtf8(argument));

    const QString suppliedLog = argumentValue(arguments, QStringLiteral("--bootstrap-log="));
    BootstrapLog::initialize(suppliedLog);
    qInstallMessageHandler(BootstrapLog::qtMessage);
    BootstrapLog::write(QStringLiteral("stage=host-entry executable=%1 cwd=%2").arg(arguments.value(0), QDir::currentPath()));
    const bool silent = hasArgument(arguments, QStringLiteral("--silent"));
    const QString startupCheck = argumentValue(arguments, QStringLiteral("--startup-check="));
    const bool showStartupDialog = !silent && suppliedLog.isEmpty() && startupCheck.isEmpty();
    const QString argumentError = validateArguments(arguments);
    if (!argumentError.isEmpty()) return startupFailure(argumentError, ERROR_BAD_ARGUMENTS, showStartupDialog);

    BootstrapLog::write(QStringLiteral("stage=qapplication-start"));
    QApplication application(wideArgumentCount, argumentPointers.data());
    BootstrapLog::write(QStringLiteral("stage=qapplication-ready directory=%1 platform=%2")
        .arg(QCoreApplication::applicationDirPath(), QApplication::platformName()));
    QApplication::setApplicationName(QStringLiteral("Wolfmark Setup"));
    QApplication::setOrganizationName(QStringLiteral("Wolfmark"));
    QApplication::setApplicationVersion(QStringLiteral(WOLFMARK_SETUP_VERSION_STRING));
    QApplication::setWindowIcon(QIcon(
        QCoreApplication::applicationDirPath() + QStringLiteral("/assets/branding/wolfmark-symbol.png")));

    QString smokeState = argumentValue(arguments, QStringLiteral("--ui-smoke="));
    if (smokeState.isEmpty()) smokeState = qEnvironmentVariable("WOLFMARK_SETUP_SMOKE_STATE");
    if (!smokeState.isEmpty()) {
        const QStringList states = {QStringLiteral("install"), QStringLiteral("update"), QStringLiteral("upgrade"),
            QStringLiteral("maintenance"), QStringLiteral("repair"), QStringLiteral("uninstall"),
            QStringLiteral("progress"), QStringLiteral("complete"), QStringLiteral("error")};
        if (!states.contains(smokeState)) return startupFailure(QStringLiteral("Unknown UI smoke state."), ERROR_BAD_ARGUMENTS, showStartupDialog);
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
    if (paths.maintenance.isEmpty() || paths.manifest.isEmpty() || paths.payload.isEmpty()
        || paths.uninstaller.isEmpty() || paths.installFolder.isEmpty()) {
        return startupFailure(QStringLiteral("This private setup host requires --maintenance, --payload, --manifest, --uninstaller and --install-dir. Run Wolfmark-Setup-win-x64.exe instead."), ERROR_BAD_ARGUMENTS, showStartupDialog);
    }
    for (const QString& file : {paths.maintenance, paths.manifest, paths.uninstaller}) {
        if (!QFileInfo(file).isFile()) return startupFailure(QStringLiteral("Required startup file is missing: %1").arg(file), ERROR_FILE_NOT_FOUND, showStartupDialog);
    }
    if (!QFileInfo(paths.payload).isDir()) return startupFailure(QStringLiteral("Payload directory is missing: %1").arg(paths.payload), ERROR_PATH_NOT_FOUND, showStartupDialog);
    if (silent && !startupCheck.isEmpty()) return startupFailure(QStringLiteral("--startup-check requires interactive mode."), ERROR_BAD_ARGUMENTS, false);

    MaintenanceController controller(paths, requestedAction, silent, &application);
    if (silent) return controller.runSilent();

    SetupWindow window(startupCheck.isEmpty() ? &controller : nullptr);
    controller.attach(&window);
    if (!startupCheck.isEmpty()) window.setAttribute(Qt::WA_ShowWithoutActivating);
    window.show();
    if (startupCheck.isEmpty()) {
        window.raise();
        window.activateWindow();
    }
    BootstrapLog::write(QStringLiteral("stage=window-visible"));
    QTimer::singleShot(0, &controller, [&controller] { controller.initialize(); });
    QTimer checkTimer;
    if (!startupCheck.isEmpty()) {
        // Read-only integration probe: real detection and native UI, no operation.
        QObject::connect(&checkTimer, &QTimer::timeout, &application, [&] {
            if (!controller.detectionFinished()) return;
            QDir().mkpath(QFileInfo(startupCheck).absolutePath());
            const bool saved = window.isVisible() && window.grab().save(startupCheck);
            controller.quit(saved && controller.exitCode() == 0 ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE);
        });
        checkTimer.start(100);
        QTimer::singleShot(30000, &application, [&controller] { controller.quit(ERROR_TIMEOUT); });
    }
    application.exec();
    return controller.exitCode();
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    BootstrapLog::initialize({});
    try {
        const int result = runSetup();
        BootstrapLog::write(QStringLiteral("stage=host-exit exit=%1").arg(result));
        return result;
    } catch (const std::exception& error) {
        return startupFailure(QStringLiteral("Unhandled startup exception: %1").arg(QString::fromUtf8(error.what())), ERROR_INSTALL_FAILURE, false);
    } catch (...) {
        return startupFailure(QStringLiteral("Unhandled startup exception."), ERROR_INSTALL_FAILURE, false);
    }
}
