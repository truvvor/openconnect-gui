/*
 * openconnect-gui-service — client session implementation.
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#include "ipcsession.h"
#include "servicelog.h"
#include "ipc/profile.h"

#include <QLocalSocket>

using namespace oc::ipc;

IpcSession::IpcSession(QLocalSocket* sock, QObject* parent)
    : QObject(parent), m_sock(sock)
{
    m_sock->setParent(this);
    connect(m_sock, &QLocalSocket::readyRead, this, &IpcSession::onReadyRead);
    connect(m_sock, &QLocalSocket::disconnected, this, &IpcSession::onDisconnected);
}

IpcSession::~IpcSession() = default;

void IpcSession::send(const Message& m)
{
    if (!m_sock || m_sock->state() != QLocalSocket::ConnectedState)
        return;
    m_sock->write(m.toLine());
    m_sock->flush();
}

void IpcSession::sendLog(int level, const QString& source, const QString& msg)
{
    Message m(QString::fromLatin1(msg::Log));
    m.json().insert(QStringLiteral("level"), level);
    m.json().insert(QStringLiteral("source"), source);
    m.json().insert(QStringLiteral("msg"), msg);
    send(m);
}

void IpcSession::sendState(ConnState s, const QString& detail)
{
    Message m(QString::fromLatin1(msg::State));
    m.json().insert(QStringLiteral("state"), toString(s));
    if (!detail.isEmpty())
        m.json().insert(QStringLiteral("detail"), detail);
    send(m);
}

void IpcSession::sendError(quint64 id, const char* code, const QString& message)
{
    Message m(QString::fromLatin1(msg::Error), id);
    m.json().insert(QStringLiteral("code"), QString::fromLatin1(code));
    m.json().insert(QStringLiteral("message"), message);
    send(m);
}

void IpcSession::onReadyRead()
{
    m_buf.append(m_sock->readAll());
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        if (line.trimmed().isEmpty())
            continue;
        bool ok = false;
        Message m = Message::fromLine(line, &ok);
        if (!ok || !m.isValid()) {
            svc::log::warn(QStringLiteral("dropping malformed IPC line (%1 bytes)").arg(line.size()));
            continue;
        }
        dispatch(m);
    }
}

void IpcSession::onDisconnected()
{
    svc::log::info(QStringLiteral("client pid=%1 disconnected").arg(m_clientPid));
    emit closed(this);
}

void IpcSession::dispatch(const Message& m)
{
    const QString t = m.type();
    if (!m.versionCompatible()) {
        sendError(m.id(), err::BadVersion,
                  QStringLiteral("service speaks protocol v%1").arg(kProtocolVersion));
        return;
    }

    if (t == QLatin1String(msg::Hello)) { onHello(m); return; }
    if (!m_helloDone) {
        sendError(m.id(), err::Unauthorized, QStringLiteral("hello required first"));
        return;
    }
    if (t == QLatin1String(msg::Connect)) onConnect(m);
    else if (t == QLatin1String(msg::Disconnect)) onDisconnect(m);
    else if (t == QLatin1String(msg::Status)) onStatus(m);
    else if (t == QLatin1String(msg::PromptResponse)) onPromptResponse(m);
    else svc::log::warn(QStringLiteral("unknown message type '%1'").arg(t));
}

void IpcSession::onHello(const Message& m)
{
    const QString client = m.json().value(QStringLiteral("client")).toString();
    const QString cver = m.json().value(QStringLiteral("clientVersion")).toString();
    svc::log::info(QStringLiteral("hello from %1 v%2 pid=%3").arg(client, cver).arg(m_clientPid));

    m_helloDone = true;
    Message ack(QString::fromLatin1(msg::HelloAck), m.id());
    ack.json().insert(QStringLiteral("serviceVersion"), QStringLiteral("1.5.3"));
    QJsonArray caps{ QStringLiteral("vpn"), QStringLiteral("camouflage"),
                     QStringLiteral("verify-peer-pid") };
    ack.json().insert(QStringLiteral("caps"), caps);
    send(ack);
}

/* --- VPN lifecycle: stubbed in P2, wired to VpnEngine in P4 --------------- */

void IpcSession::onConnect(const Message& m)
{
    const Profile p = Profile::fromJson(m.json().value(QStringLiteral("profile")).toObject());
    svc::log::info(QStringLiteral("connect requested: %1").arg(p.redactedSummary()));
    // P4: hand p to VpnEngine, stream state/log/ipinfo, marshal prompts.
    sendError(m.id(), err::Internal,
              QStringLiteral("VPN engine not wired in this build (pending P4)"));
}

void IpcSession::onDisconnect(const Message& m)
{
    svc::log::info(QStringLiteral("disconnect requested"));
    sendState(ConnState::Disconnected, QStringLiteral("no active connection"));
    Q_UNUSED(m);
}

void IpcSession::onStatus(const Message& m)
{
    Message s(QString::fromLatin1(msg::State), m.id());
    s.json().insert(QStringLiteral("state"), toString(ConnState::Idle));
    send(s);
}

void IpcSession::onPromptResponse(const Message& m)
{
    Q_UNUSED(m); // P4: route to the pending engine prompt
}
