#pragma once

#include <windows.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QString>

namespace BootstrapLog {
inline QString path;

inline void initialize(const QString& suppliedPath) {
    path = suppliedPath.isEmpty()
        ? QDir::temp().filePath(QStringLiteral("Wolfmark-Setup-bootstrap-%1.log").arg(GetCurrentProcessId()))
        : suppliedPath;
}

inline void write(QString message) {
    // NSIS and the host append UTF-16LE to the same, per-launch Windows log.
    message.replace(QLatin1Char('\r'), QLatin1Char(' '));
    message.replace(QLatin1Char('\n'), QLatin1Char(' '));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    if (file.size() == 0) file.write("\xff\xfe", 2);
    const QString line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
        + QLatin1Char(' ') + message + QStringLiteral("\r\n");
    file.write(reinterpret_cast<const char*>(line.utf16()), line.size() * 2);
}

inline void qtMessage(QtMsgType type, const QMessageLogContext&, const QString& message) {
    write(QStringLiteral("stage=qt type=%1 message=%2").arg(static_cast<int>(type)).arg(message));
}
}
