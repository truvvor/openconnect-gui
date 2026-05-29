/*
 * openconnect-gui-service — internal diagnostic log implementation.
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#include "servicelog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QTextStream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace svc::log {

namespace {
QMutex g_mutex;
bool g_console = false;
bool g_inited = false;

QString dataDir()
{
    QString base = qEnvironmentVariable("ProgramData");
    if (base.isEmpty())
        base = QStringLiteral("C:/ProgramData");
    return base + QStringLiteral("/OpenConnect-GUI");
}

const char* levelTag(Level l)
{
    switch (l) {
    case Level::Debug: return "DBG";
    case Level::Info:  return "INF";
    case Level::Warn:  return "WRN";
    case Level::Error: return "ERR";
    }
    return "INF";
}
} // namespace

QString logFilePath() { return dataDir() + QStringLiteral("/service.log"); }

void init(bool console)
{
    QMutexLocker lock(&g_mutex);
    g_console = console;
    QDir().mkpath(dataDir());
    g_inited = true;
}

void write(Level level, const QString& msg)
{
    QMutexLocker lock(&g_mutex);
    const QString line = QStringLiteral("%1 [%2] %3")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
             QLatin1String(levelTag(level)), msg);

    if (g_inited) {
        QFile f(logFilePath());
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << line << '\n';
        }
    }
#ifdef _WIN32
    OutputDebugStringW(reinterpret_cast<const wchar_t*>(QString(line + '\n').utf16()));
#endif
    if (g_console) {
        QTextStream err(stderr);
        err << line << '\n';
    }
}

} // namespace svc::log
