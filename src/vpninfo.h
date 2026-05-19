/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2026 Keenetic anti-DPI VPN client (fork)
 *
 * GPLv2 — see LICENSE.txt
 */

#pragma once

#include <QString>
#ifdef _WIN32
#include <winsock2.h>
#else
#include "common.h"
#endif
#if defined(__linux__) || defined(__FreeBSD__)
#define SOCKET int
#endif

class MainWindow;
class StoredServer;

/*
 * Keenetic-camouflage VpnInfo: thin wrapper around libopenconnect_keenetic.
 * Compared to upstream openconnect-gui, this class:
 *   - assumes user/password + camouflage-secret auth only (no per-user cert)
 *   - uses bundled CA file (no TOFU / no gtdb)
 *   - always disables DTLS (server is TCP-only anti-DPI)
 *   - sets a public-API call openconnect_set_camouflage_secret() to enable
 *     CSTP-magic/header-rewriting/scatter inside libopenconnect.
 */
class VpnInfo {
public:
    VpnInfo(QString name, StoredServer* ss, MainWindow* m);
    ~VpnInfo();

    void parse_url(const char* url);
    int connect();
    void mainloop();
    void get_info(QString& dns, QString& ip, QString& ip6);
    void get_cipher_info(QString& cstp);
    SOCKET get_cmd_fd() const;
    void reset_vpn();
    bool get_minimize() const;

    QString last_err;
    MainWindow* m;
    StoredServer* ss;
    struct openconnect_info* vpninfo;
    unsigned int form_attempt;
    unsigned int form_pass_attempt;

    void logVpncScriptOutput();

private:
    SOCKET cmd_fd;
};
