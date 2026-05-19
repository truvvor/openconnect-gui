/*
 * wintun_client.h — GUI-side RPC client for KeeneticVpnService.
 *
 * Connects to \\.\pipe\KeeneticVpnService (LocalSystem) and sends the
 * length-prefixed JSON ops documented in wintun-service/pipe_server.cpp.
 *
 * The point of this layer: openconnect_setup_tun_device() normally needs
 * Administrator on Windows because it talks directly to tap-windows6.sys.
 * Instead we ask the service to own the WinTun adapter on our behalf —
 * the GUI then never touches the driver and never asks for UAC.
 *
 * GPL v2.
 */

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class WintunClient : public QObject {
    Q_OBJECT
public:
    explicit WintunClient(QObject* parent = nullptr);
    ~WintunClient();

    /* Returns true if the service is running and the pipe answered our ping. */
    bool ensureAvailable();

    bool openAdapter(const QString& adapterName, QString* errorOut = nullptr);
    bool closeAdapter();
    bool setRoutes(const QStringList& cidrs, const QString& gateway);
    bool setDns(const QStringList& servers, const QStringList& domains);

    /* If the service replies with a Win32 HANDLE value for the adapter session,
     * we expose it here so vpninfo.cpp can hand it to openconnect_setup_tun_fd. */
    qint64 tunHandle() const { return m_tunHandle; }

    /* OS-level service control (uses sc.exe / SCM). Does not need admin if
     * the user is in KeeneticVPNUsers group (the MSI installer adds them). */
    static bool startServiceIfNeeded();

private:
    bool sendOp(const QByteArray& json, QByteArray* replyOut, QString* errorOut = nullptr);

    qint64 m_pipeHandle { -1 };   /* HANDLE on Windows */
    qint64 m_tunHandle  { -1 };
};
