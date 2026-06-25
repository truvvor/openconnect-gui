/*
 * openconnect-gui — client side of the privilege-separation IPC.
 *
 * Talks to openconnect-gui-service over the named pipe: opens the connection,
 * does the hello handshake, sends connect/disconnect/status + prompt responses,
 * and turns inbound IPC messages into Qt signals the MainWindow consumes.
 * Runs entirely on the GUI thread (no admin rights needed).
 *
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#pragma once

#include "ipc/profile.h"
#include "ipc/protocol.h"

#include <QJsonObject>
#include <QObject>

class QLocalSocket;

class ServiceClient : public QObject {
    Q_OBJECT
public:
    explicit ServiceClient(QObject* parent = nullptr);

    bool isReady() const { return m_ready; }

    void connectVpn(const oc::ipc::Profile& profile); // opens pipe if needed
    void resendLastConnect();                         // re-send the last profile (busy retry)
    void disconnectVpn();
    void requestStatus();
    void sendPromptResponse(quint64 promptId, bool ok, const QJsonObject& extra = {});

signals:
    void ready();
    void logReceived(int level, const QString& msg);
    void stateChanged(const QString& state, const QString& detail);
    void statsReceived(double rxBytes, double txBytes,
                       const QString& cstpCipher, const QString& dtlsCipher);
    void ipInfoReceived(const QString& addr, const QString& netmask,
                        const QString& addr6, const QString& dns);
    void promptReceived(const QString& kind, quint64 promptId, const QJsonObject& body);
    void persistReceived(const QString& what, const QJsonObject& body);
    void serviceError(const QString& code, const QString& message);
    void serviceUnavailable(const QString& reason);

private slots:
    void onConnected();
    void onReadyRead();
    void onSocketError();

private:
    void send(const oc::ipc::Message& m);
    void ensureConnected();
    void handle(const oc::ipc::Message& m);

    QLocalSocket* m_sock = nullptr;
    QByteArray m_buf;
    bool m_ready = false;
    bool m_haveQueuedConnect = false;
    oc::ipc::Profile m_queuedConnect;
};
