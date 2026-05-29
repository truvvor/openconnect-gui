/*
 * openconnect-gui-service — one client session over the named pipe.
 *
 * Implements EngineHost: runs a VpnEngine in a worker thread and marshals its
 * callbacks to the GUI over IPC. One-way notifications (log/state/stats/ipinfo/
 * persist) are queued to the main thread for sending; interactive prompts
 * (auth-form/cert/banner/pin) block the engine thread until prompt-response.
 *
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#pragma once

#include "engine/enginehost.h"
#include "ipc/protocol.h"

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QWaitCondition>
#include <thread>

class QLocalSocket;
namespace oc::engine { class VpnEngine; }

class IpcSession : public QObject, public oc::engine::EngineHost {
    Q_OBJECT
public:
    explicit IpcSession(QLocalSocket* sock, QObject* parent = nullptr);
    ~IpcSession() override;

    void setClientPid(quint64 pid) { m_clientPid = pid; }
    quint64 clientPid() const { return m_clientPid; }

    /* EngineHost — invoked from the engine worker thread */
    void onLog(int level, const QString& msg) override;
    void onState(const QString& state, const QString& detail = {}) override;
    void onStats(quint64 rxBytes, quint64 txBytes,
                 const QString& cstpCipher, const QString& dtlsCipher) override;
    void onIpInfo(const QString& addr, const QString& netmask,
                  const QString& addr6, const QString& dns) override;
    void onPersistTrust(const QString& hash, const QString& derBase64) override;
    void onPersistString(const QString& what, const QString& value) override;
    bool askAuthForm(oc::engine::AuthForm& form) override;
    bool askCert(const QString& reason, const QString& host, const QString& hash,
                 const QString& details, const QString& change) override;
    bool askBanner(const QString& banner) override;
    bool askPin(const QString& tokenUrl, const QString& tokenLabel,
                unsigned flags, QString& pinOut) override;

signals:
    void closed(IpcSession* self);

private slots:
    void onReadyRead();
    void onDisconnected();
    void sendQueued(const QByteArray& line);   // main-thread socket write
    void finalizeEngine();                      // main-thread join + delete

private:
    void send(const oc::ipc::Message& m);
    void postSend(const oc::ipc::Message& m);   // queue a send onto the main thread
    void sendError(quint64 id, const char* code, const QString& message);

    void dispatch(const oc::ipc::Message& m);
    void onHello(const oc::ipc::Message& m);
    void onConnect(const oc::ipc::Message& m);
    void onDisconnect(const oc::ipc::Message& m);
    void onStatus(const oc::ipc::Message& m);
    void onPromptResponse(const oc::ipc::Message& m);

    void runEngine();

    /* blocking prompt rendezvous (engine thread <-> main thread) */
    struct Pending {
        QMutex mutex;
        QWaitCondition cv;
        bool done = false;
        bool ok = false;
        QJsonObject response;
    };
    bool awaitPrompt(const QString& kind, QJsonObject body, QJsonObject* respOut);
    void wakeAllPending(bool ok);

    QLocalSocket* m_sock = nullptr;
    QByteArray m_buf;
    quint64 m_clientPid = 0;
    bool m_helloDone = false;

    oc::engine::VpnEngine* m_engine = nullptr;
    std::thread m_engineThread;
    QString m_state = QStringLiteral("idle");

    QMutex m_pendingMutex;
    QHash<quint64, Pending*> m_pending;
    quint64 m_promptSeq = 0;
};
