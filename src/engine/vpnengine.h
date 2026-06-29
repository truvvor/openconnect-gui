/*
 * openconnect-gui — headless libopenconnect engine (service-side).
 * Ported from src/vpninfo.cpp; all UI/persistence goes through EngineHost.
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#pragma once

#include "enginehost.h"
#include "ipc/profile.h"

#include <QString>
#include <QStringList>

#include <atomic>

#ifdef _WIN32
#include <winsock2.h>
#else
#define SOCKET int
#endif

struct openconnect_info;

namespace oc::engine {

class VpnEngine {
public:
    VpnEngine(const oc::ipc::Profile& profile, EngineHost* host);
    ~VpnEngine();

    bool setup();          // build vpninfo + apply options; false on fatal error
    int  run();            // connect (+batch retry) -> dtls -> mainloop; blocks
    void cancel();         // OC_CMD_CANCEL via the command pipe (any thread)
    void requestStats();   // OC_CMD_STATS
    void teardown();       // free vpninfo (runs reason=disconnect script + closes tun).
                           // MUST run on the worker thread, never the service event loop.
    /* Called from the auth/cert callbacks when the user cancels a prompt, so the
     * connect/retry loop stops instead of re-prompting 2-3 more times. */
    void requestUserAbort() { m_cancelRequested = true; }

    QString lastError() const { return m_lastErr; }
    SOCKET  cmdFd() const { return m_cmdFd; }

    /* public so the C-style libopenconnect callbacks (privdata=this) can reach
     * the host, the resolved profile and the per-attempt counters. */
    oc::ipc::Profile profile;
    EngineHost*      host = nullptr;
    openconnect_info* vpninfo = nullptr;
    QString          m_lastErr;

    unsigned authgroup_set = 0;
    unsigned password_set = 0;
    unsigned form_attempt = 0;
    unsigned form_pass_attempt = 0;

    void logVpncScriptOutput();   // reads %TEMP%\vpnc.log, handles banner

private:
    int  doConnect();
    int  dtlsConnect();
    void mainloop();
    QString writeTempPem(const QByteArray& pem, const QString& tag);
    void cleanupTempFiles();

    SOCKET m_cmdFd = (SOCKET)-1;
    QStringList m_tempFiles;
    /* set by cancel() (any thread); honored by run() so a disconnect aborts an
     * in-progress or retrying connect, not just a running mainloop. */
    std::atomic<bool> m_cancelRequested{false};
};

} // namespace oc::engine
