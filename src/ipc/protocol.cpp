/*
 * openconnect-gui — IPC protocol v1 implementation (oc-ipc).
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#include "protocol.h"

#include <QJsonDocument>

namespace oc::ipc {

Message::Message(const QString& type, quint64 id)
{
    m_obj.insert(QStringLiteral("v"), kProtocolVersion);
    m_obj.insert(QStringLiteral("type"), type);
    if (id != 0)
        m_obj.insert(QStringLiteral("id"), static_cast<double>(id));
}

QString Message::type() const { return m_obj.value(QStringLiteral("type")).toString(); }

int Message::version() const { return m_obj.value(QStringLiteral("v")).toInt(-1); }

quint64 Message::id() const
{
    return static_cast<quint64>(m_obj.value(QStringLiteral("id")).toDouble(0));
}

bool Message::isValid() const
{
    return m_obj.contains(QStringLiteral("v")) && !type().isEmpty();
}

bool Message::versionCompatible() const { return version() == kProtocolVersion; }

QByteArray Message::toLine() const
{
    QByteArray out = QJsonDocument(m_obj).toJson(QJsonDocument::Compact);
    out.append('\n');
    return out;
}

Message Message::fromLine(const QByteArray& line, bool* ok)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line.trimmed(), &err);
    const bool good = (err.error == QJsonParseError::NoError) && doc.isObject();
    if (ok)
        *ok = good;
    return good ? Message(doc.object()) : Message();
}

QString toString(ConnState s)
{
    switch (s) {
    case ConnState::Idle: return QStringLiteral("idle");
    case ConnState::Connecting: return QStringLiteral("connecting");
    case ConnState::Authenticating: return QStringLiteral("authenticating");
    case ConnState::ObtainingCookie: return QStringLiteral("obtaining-cookie");
    case ConnState::Cstp: return QStringLiteral("cstp");
    case ConnState::SetupTun: return QStringLiteral("setup-tun");
    case ConnState::Connected: return QStringLiteral("connected");
    case ConnState::Reconnecting: return QStringLiteral("reconnecting");
    case ConnState::Disconnecting: return QStringLiteral("disconnecting");
    case ConnState::Disconnected: return QStringLiteral("disconnected");
    case ConnState::Error: return QStringLiteral("error");
    }
    return QStringLiteral("idle");
}

ConnState connStateFromString(const QString& s)
{
    if (s == QLatin1String("connecting")) return ConnState::Connecting;
    if (s == QLatin1String("authenticating")) return ConnState::Authenticating;
    if (s == QLatin1String("obtaining-cookie")) return ConnState::ObtainingCookie;
    if (s == QLatin1String("cstp")) return ConnState::Cstp;
    if (s == QLatin1String("setup-tun")) return ConnState::SetupTun;
    if (s == QLatin1String("connected")) return ConnState::Connected;
    if (s == QLatin1String("reconnecting")) return ConnState::Reconnecting;
    if (s == QLatin1String("disconnecting")) return ConnState::Disconnecting;
    if (s == QLatin1String("disconnected")) return ConnState::Disconnected;
    if (s == QLatin1String("error")) return ConnState::Error;
    return ConnState::Idle;
}

} // namespace oc::ipc
