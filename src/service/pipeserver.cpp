/*
 * openconnect-gui-service — named-pipe acceptor implementation.
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#include "pipeserver.h"
#include "ipcsession.h"
#include "servicelog.h"
#include "ipc/protocol.h"

#include <QLocalServer>
#include <QLocalSocket>

#ifdef _WIN32
#include <windows.h>
#endif

PipeServer::PipeServer(QObject* parent) : QObject(parent)
{
    m_server = new QLocalServer(this);
    // Allow any local user to connect; the server itself runs as SYSTEM.
    m_server->setSocketOptions(QLocalServer::WorldAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, &PipeServer::onNewConnection);
}

PipeServer::~PipeServer() { stop(); }

bool PipeServer::start()
{
    const QString name = QString::fromLatin1(oc::ipc::kPipeName);
    // A stale pipe from a crashed instance blocks listen(); clear it first.
    QLocalServer::removeServer(name);
    if (!m_server->listen(name)) {
        svc::log::error(QStringLiteral("listen('%1') failed: %2")
                            .arg(name, m_server->errorString()));
        return false;
    }
    svc::log::info(QStringLiteral("listening on \\\\.\\pipe\\%1").arg(name));
    return true;
}

void PipeServer::stop()
{
    for (IpcSession* s : qAsConst(m_sessions))
        s->deleteLater();
    m_sessions.clear();
    if (m_server && m_server->isListening())
        m_server->close();
}

static quint64 clientPidOf(QLocalSocket* sock)
{
#ifdef _WIN32
    auto h = reinterpret_cast<HANDLE>(sock->socketDescriptor());
    ULONG pid = 0;
    if (h != INVALID_HANDLE_VALUE && GetNamedPipeClientProcessId(h, &pid))
        return pid;
#else
    Q_UNUSED(sock);
#endif
    return 0;
}

void PipeServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QLocalSocket* sock = m_server->nextPendingConnection();
        const quint64 pid = clientPidOf(sock);
        svc::log::info(QStringLiteral("new client connection pid=%1").arg(pid));

        auto* session = new IpcSession(sock, this);
        session->setClientPid(pid);
        connect(session, &IpcSession::closed, this, &PipeServer::onSessionClosed);
        m_sessions.insert(session);
    }
}

void PipeServer::onSessionClosed(IpcSession* s)
{
    m_sessions.remove(s);
    s->deleteLater();
}
