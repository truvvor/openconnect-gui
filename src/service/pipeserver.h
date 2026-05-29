/*
 * openconnect-gui-service — named-pipe acceptor (QLocalServer).
 *
 * The server runs as LocalSystem, so the pipe is opened with WorldAccessOption
 * (otherwise only SYSTEM could connect). Remote clients are rejected by the pipe
 * itself; the "interactive user only" decision is enforced at the application
 * layer by inspecting the client process token (P4 hardening — here we record
 * the client PID via GetNamedPipeClientProcessId). The precise SDDL
 * (oc::ipc::kPipeSddl) is retained for a future hand-rolled accept loop.
 *
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#pragma once

#include <QObject>
#include <QSet>

class QLocalServer;
class IpcSession;

class PipeServer : public QObject {
    Q_OBJECT
public:
    explicit PipeServer(QObject* parent = nullptr);
    ~PipeServer() override;

    bool start();        // listen on \\.\pipe\openconnect-gui\svc
    void stop();         // close all sessions + the listener

private slots:
    void onNewConnection();
    void onSessionClosed(IpcSession* s);

private:
    QLocalServer* m_server = nullptr;
    QSet<IpcSession*> m_sessions;
};
