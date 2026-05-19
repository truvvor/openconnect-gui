/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2026 Keenetic anti-DPI VPN client (fork)
 *
 * GPLv2 — see LICENSE.txt
 */

#pragma once

#include <QByteArray>
#include <QString>
#include <QWidget>

/*
 * Keenetic-camouflage StoredServer.
 *
 * Removed (vs upstream openconnect-gui 1.5.3):
 *   - client cert / key / TPM / token fields (no per-user PKI auth)
 *   - groupname / server_hash (TOFU pinning replaced with bundled CA)
 *   - disable_udp (DTLS always disabled in this fork)
 *
 * Added:
 *   - camouflage_secret (encrypted at rest via QSettings + Windows DPAPI later)
 *   - tunnel_url        (CONNECT path; default /api/v1/session)
 */
class StoredServer {
public:
    StoredServer();
    ~StoredServer();

    int load(QString& name);
    int save();

    const QString& get_username() const;
    void set_username(const QString& username);

    const QString& get_password() const;
    void set_password(const QString& password);

    const QString& get_servername() const;
    void set_servername(const QString& servername);

    const QString& get_label() const;
    void set_label(const QString& label);

    /* Keenetic camouflage fields. */
    const QString& get_camouflage_secret() const;
    void set_camouflage_secret(const QString& secret);
    const QString& get_tunnel_url() const;
    void set_tunnel_url(const QString& url);

    /* Bundled CA file — defaults to keenetic-ca.crt next to the GUI exe. */
    QString get_ca_cert_file();

    void clear_password();

    bool get_batch_mode() const;
    void set_batch_mode(const bool mode);

    bool get_minimize() const;
    void set_minimize(const bool t);

    int get_reconnect_timeout() const;
    void set_reconnect_timeout(const int timeout);

    int get_protocol_id() const;
    void set_protocol_id(const int id);

    const char* get_protocol_name() const;
    void set_protocol_name(const QString name);

    void set_window(QWidget* w);

    QString m_last_err;

private:
    bool m_batch_mode = false;
    bool m_minimize_on_connect = false;
    int m_reconnect_timeout = 30;
    QString m_username;
    QString m_password;
    QString m_servername;
    QString m_label;
    /* Keenetic camouflage profile fields. */
    QString m_camouflage_secret;
    QString m_tunnel_url = QStringLiteral("/api/v1/session");
    int m_protocol_id = 0;
    QString m_protocol_name = QStringLiteral("anyconnect");
};
