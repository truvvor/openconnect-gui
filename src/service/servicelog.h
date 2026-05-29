/*
 * openconnect-gui-service — internal diagnostic log.
 *
 * The service runs as LocalSystem with no console when started by the SCM, so
 * startup/pipe/error diagnostics go to %ProgramData%\OpenConnect-GUI\service.log
 * (and to stderr/OutputDebugString in --console mode). VPN/openconnect output is
 * NOT logged here — it is streamed to the GUI as `log` IPC events.
 *
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#pragma once

#include <QString>

namespace svc::log {

enum class Level { Debug, Info, Warn, Error };

/* console=true also echoes to stderr (used by --console mode). */
void init(bool console);
void write(Level level, const QString& msg);

inline void debug(const QString& m) { write(Level::Debug, m); }
inline void info(const QString& m)  { write(Level::Info, m); }
inline void warn(const QString& m)  { write(Level::Warn, m); }
inline void error(const QString& m) { write(Level::Error, m); }

QString logFilePath();

} // namespace svc::log
