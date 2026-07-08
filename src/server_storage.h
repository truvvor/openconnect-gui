/*
 * Copyright (C) 2014 Red Hat
 *
 * This file is part of openconnect-gui.
 *
 * openconnect-gui is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "keypair.h"

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

    const QString& get_groupname() const;
    void set_groupname(const QString& groupname);

    const QString& get_servername() const;
    void set_servername(const QString& servername);

    const QString& get_label() const;
    void set_label(const QString& label);

    bool get_disable_udp() const;
    void set_disable_udp(bool v);

    /* Split-tunnel: when true, the client does NOT install a default route;
     * only the VPN's own subnet (and any server-pushed routes) go via the tunnel. */
    bool get_no_default_route() const;
    void set_no_default_route(bool v);

    /* Anti-DPI camouflage (Level 2): activates camouflaged wire protocol
     * in libopenconnect 5.10+ when non-empty. Persisted encrypted. */
    const QString& get_camouflage_secret() const;
    void set_camouflage_secret(const QString& secret);
    void clear_camouflage_secret();

    /* When true, the server "Welcome to KN-...." / login-banner is logged
     * and silently accepted instead of popping a modal Accept/Disconnect box. */
    bool get_auto_accept_banner() const;
    void set_auto_accept_banner(bool v);

    /* When true, always prompt for the password at connect time even if one is
     * saved (batch mode); the saved value pre-fills the prompt in the GUI. */
    bool get_force_password_prompt() const;
    void set_force_password_prompt(bool v);

    /* When true, peer-certificate-changed / unknown-peer prompts are silently
     * accepted (new key is trusted-on-first-use, changed key is auto-rotated).
     * Reason: with LE-cafile-based trust the TOFU pubkey pin is redundant and
     * generates noise on every LE cert rotation. */
    bool get_suppress_cert_change() const;
    void set_suppress_cert_change(bool v);

    QString get_cert_file();
    QString get_key_file();
    QString get_key_url() const;
    QString get_ca_cert_file();

    void clear_cert();
    void clear_key();
    void clear_ca();
    void clear_password();
    void clear_groupname();
    void clear_server_hash();

    QString get_client_cert_hash();
    int set_client_cert(const QString& filename);

    QString get_ca_cert_hash();
    int set_ca_cert(const QString& filename);

    bool get_batch_mode() const;
    void set_batch_mode(const bool mode);

    bool get_minimize() const;
    void set_minimize(const bool t);

    bool get_proxy() const;
    void set_proxy(const bool t);

    int get_reconnect_timeout() const;
    void set_reconnect_timeout(const int timeout);

    int get_dtls_reconnect_timeout() const;
    void set_dtls_reconnect_timeout(const int timeout);

    QString get_token_str();
    void set_token_str(const QString& str);

    int get_token_type();
    void set_token_type(const int type);

    int get_protocol_id() const;
    void set_protocol_id(const int id);

    const char* get_protocol_name() const;
    void set_protocol_name(const QString name);

    unsigned get_server_hash(QByteArray& hash) const;
    void get_server_hash(QString& hash) const;
    void set_server_hash(const unsigned algo, const QByteArray& hash);

    bool client_is_complete() const;

    void set_window(QWidget* w);

    int set_client_key(const QString& filename);

    QString m_last_err;

private:
    bool m_batch_mode;
    bool m_minimize_on_connect;
    bool m_proxy;
    bool m_disable_udp;
    bool m_no_default_route;
    bool m_auto_accept_banner;
    bool m_force_password_prompt;
    bool m_suppress_cert_change;
    int m_reconnect_timeout;
    int m_dtls_attempt_period;
    QString m_username;
    QString m_password;
    QString m_groupname;
    QString m_servername;
    QString m_token_string;
    QString m_label;
    int m_token_type;
    int m_protocol_id;
    QString m_protocol_name;
    QByteArray m_server_hash;
    unsigned m_server_hash_algo;
    QString m_camouflage_secret;
    Cert m_ca_cert;
    KeyPair m_client;
};
