/*
 * openconnect-gui-service — one client session over the named pipe.
 *
 * Owns a QLocalSocket, reframes newline-delimited JSON into protocol Messages,
 * and dispatches them. P2 implements the hello handshake and message plumbing;
 * the VPN lifecycle (connect/disconnect/status + prompt/persist marshalling) is
 * wired onto the VpnEngine in P4.
 *
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#pragma once

#include "ipc/protocol.h"

#include <QObject>

class QLocalSocket;
class VpnEngine; // P3/P4

class IpcSession : public QObject {
    Q_OBJECT
public:
    explicit IpcSession(QLocalSocket* sock, QObject* parent = nullptr);
    ~IpcSession() override;

    void setClientPid(quint64 pid) { m_clientPid = pid; }
    quint64 clientPid() const { return m_clientPid; }

    /* outbound helpers (used by the engine bridge in P4) */
    void send(const oc::ipc::Message& m);
    void sendLog(int level, const QString& source, const QString& msg);
    void sendState(oc::ipc::ConnState s, const QString& detail = {});
    void sendError(quint64 id, const char* code, const QString& message);

signals:
    void closed(IpcSession* self);

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    void dispatch(const oc::ipc::Message& m);
    void onHello(const oc::ipc::Message& m);
    void onConnect(const oc::ipc::Message& m);
    void onDisconnect(const oc::ipc::Message& m);
    void onStatus(const oc::ipc::Message& m);
    void onPromptResponse(const oc::ipc::Message& m);

    QLocalSocket* m_sock = nullptr;
    QByteArray m_buf;
    quint64 m_clientPid = 0;
    bool m_helloDone = false;
};
