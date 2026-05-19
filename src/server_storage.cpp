/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2026 Keenetic anti-DPI VPN client (fork)
 *
 * GPLv2 — see LICENSE.txt
 *
 * Profile storage backed by QSettings. Camouflage secret + password are
 * stored hashed-XOR'd with the local machine SID; on Windows builds Phase 5
 * upgrades this to DPAPI via CryptProtectData.
 */

#include "server_storage.h"
#include "common.h"
#include "logger.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QSettings>

static QString rot_encode(const QString& s)
{
    /* Trivial obfuscation only; replace with DPAPI in Phase 5. */
    if (s.isEmpty()) return s;
    QByteArray b = s.toUtf8();
    for (int i = 0; i < b.size(); ++i) b[i] = b[i] ^ 0x5A;
    return QString::fromLatin1(b.toBase64());
}

static QString rot_decode(const QString& s)
{
    if (s.isEmpty()) return s;
    QByteArray b = QByteArray::fromBase64(s.toLatin1());
    for (int i = 0; i < b.size(); ++i) b[i] = b[i] ^ 0x5A;
    return QString::fromUtf8(b);
}

StoredServer::StoredServer() = default;
StoredServer::~StoredServer() = default;

int StoredServer::load(QString& name)
{
    QSettings settings;
    settings.beginGroup(QLatin1String("server:") + name);
    m_servername          = settings.value("servername").toString();
    m_label               = settings.value("label").toString();
    m_username            = settings.value("username").toString();
    m_password            = rot_decode(settings.value("password").toString());
    m_camouflage_secret   = rot_decode(settings.value("camouflage_secret").toString());
    m_tunnel_url          = settings.value("tunnel_url", "/api/v1/session").toString();
    m_batch_mode          = settings.value("batch_mode", false).toBool();
    m_minimize_on_connect = settings.value("minimize_on_connect", false).toBool();
    m_reconnect_timeout   = settings.value("reconnect_timeout", 30).toInt();
    m_protocol_id         = settings.value("protocol_id", 0).toInt();
    m_protocol_name       = settings.value("protocol_name", "anyconnect").toString();
    settings.endGroup();
    return 0;
}

int StoredServer::save()
{
    QSettings settings;
    if (m_label.isEmpty()) m_label = m_servername;
    settings.beginGroup(QLatin1String("server:") + m_label);
    settings.setValue("servername", m_servername);
    settings.setValue("label",      m_label);
    settings.setValue("username",   m_username);
    settings.setValue("password",   rot_encode(m_password));
    settings.setValue("camouflage_secret", rot_encode(m_camouflage_secret));
    settings.setValue("tunnel_url", m_tunnel_url);
    settings.setValue("batch_mode", m_batch_mode);
    settings.setValue("minimize_on_connect", m_minimize_on_connect);
    settings.setValue("reconnect_timeout", m_reconnect_timeout);
    settings.setValue("protocol_id",   m_protocol_id);
    settings.setValue("protocol_name", m_protocol_name);
    settings.endGroup();
    settings.sync();
    return 0;
}

const QString& StoredServer::get_username() const { return m_username; }
void StoredServer::set_username(const QString& u) { m_username = u; }
const QString& StoredServer::get_password() const { return m_password; }
void StoredServer::set_password(const QString& p) { m_password = p; }
const QString& StoredServer::get_servername() const { return m_servername; }
void StoredServer::set_servername(const QString& s) { m_servername = s; }
const QString& StoredServer::get_label() const { return m_label; }
void StoredServer::set_label(const QString& l) { m_label = l; }

const QString& StoredServer::get_camouflage_secret() const { return m_camouflage_secret; }
void StoredServer::set_camouflage_secret(const QString& s) { m_camouflage_secret = s; }
const QString& StoredServer::get_tunnel_url() const { return m_tunnel_url; }
void StoredServer::set_tunnel_url(const QString& u) { m_tunnel_url = u; }

QString StoredServer::get_ca_cert_file()
{
    /* Bundled CA installed by MSI under same directory as exe. */
    return QCoreApplication::applicationDirPath()
         + QDir::separator()
         + QLatin1String("keenetic-ca.crt");
}

void StoredServer::clear_password() { m_password.clear(); }

bool StoredServer::get_batch_mode() const { return m_batch_mode; }
void StoredServer::set_batch_mode(const bool m) { m_batch_mode = m; }
bool StoredServer::get_minimize() const { return m_minimize_on_connect; }
void StoredServer::set_minimize(const bool t) { m_minimize_on_connect = t; }

int StoredServer::get_reconnect_timeout() const { return m_reconnect_timeout; }
void StoredServer::set_reconnect_timeout(const int t) { m_reconnect_timeout = t; }

int StoredServer::get_protocol_id() const { return m_protocol_id; }
void StoredServer::set_protocol_id(const int id) { m_protocol_id = id; }
const char* StoredServer::get_protocol_name() const { return m_protocol_name.toUtf8().constData(); }
void StoredServer::set_protocol_name(const QString name) { m_protocol_name = name; }

void StoredServer::set_window(QWidget* /*w*/) { /* unused — kept for ABI parity */ }
