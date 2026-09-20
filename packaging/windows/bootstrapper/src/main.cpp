#include <windows.h>
#include <shellapi.h>
#include <msiquery.h>

#include <BootstrapperApplication.h>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMetaObject>
#include <QTimer>

#include <thread>
#include <vector>

#include "burn_controller.h"
#include "setup_window.h"

namespace {
QString argumentValue(const QStringList& arguments, const QString& prefix) {
    const QString option = prefix.endsWith(QLatin1Char('=')) ? prefix.chopped(1) : prefix;
    for (qsizetype index = 0; index < arguments.size(); ++index) {
        const QString& argument = arguments.at(index);
        if (argument.startsWith(prefix)) {
            return argument.mid(prefix.size());
        }
        if (argument == option && index + 1 < arguments.size()) {
            return arguments.at(index + 1);
        }
    }
    return {};
}
}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int wideArgumentCount = 0;
    wchar_t** wideArguments = CommandLineToArgvW(GetCommandLineW(), &wideArgumentCount);
    if (!wideArguments) {
        return ERROR_BAD_ARGUMENTS;
    }
    std::vector<QByteArray> encodedArguments;
    std::vector<char*> argumentPointers;
    encodedArguments.reserve(static_cast<std::size_t>(wideArgumentCount));
    argumentPointers.reserve(static_cast<std::size_t>(wideArgumentCount));
    for (int index = 0; index < wideArgumentCount; ++index) {
        encodedArguments.push_back(QString::fromWCharArray(wideArguments[index]).toUtf8());
    }
    LocalFree(wideArguments);
    for (QByteArray& argument : encodedArguments) {
        argumentPointers.push_back(argument.data());
    }
    QApplication application(wideArgumentCount, argumentPointers.data());
    QApplication::setApplicationName(QStringLiteral("Wolfmark Setup"));
    QApplication::setOrganizationName(QStringLiteral("Wolfmark"));
    QApplication::setWindowIcon(QIcon(
        QCoreApplication::applicationDirPath() +
        QStringLiteral("/assets/branding/wolfmark-symbol.png")));

    const QStringList arguments = application.arguments();
    QString smokeState = argumentValue(arguments, QStringLiteral("--ui-smoke="));
    if (smokeState.isEmpty()) {
        smokeState = qEnvironmentVariable("WOLFMARK_SETUP_SMOKE_STATE");
    }
    if (!smokeState.isEmpty()) {
        SetupWindow window(nullptr);
        window.showSmokeState(smokeState);
        window.show();
        QString screenshot = argumentValue(arguments, QStringLiteral("--ui-screenshot="));
        if (screenshot.isEmpty()) {
            screenshot = qEnvironmentVariable("WOLFMARK_SETUP_SCREENSHOT");
        }
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

    auto* controller = new BurnController;
    SetupWindow window(controller);
    controller->setWindow(&window);
    controller->AddRef();
    std::thread engineThread([controller] {
        const HRESULT result = BootstrapperApplicationRun(controller);
        QMetaObject::invokeMethod(qApp, [result] {
            if (FAILED(result)) {
                qApp->exit(static_cast<int>(result));
            }
        }, Qt::QueuedConnection);
        controller->Release();
    });

    const int result = application.exec();
    controller->quit(result == 0 ? ERROR_SUCCESS : static_cast<DWORD>(result));
    engineThread.join();
    controller->Release();
    return result;
}
