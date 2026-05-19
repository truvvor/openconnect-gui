/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2026 Keenetic anti-DPI VPN client (fork)
 *
 * GPLv2 — see LICENSE.txt
 *
 * Keenetic-camouflage VpnInfo:
 *  - username+password+camouflage-secret authentication only
 *  - bundled CA file (no TOFU/gtdb pinning)
 *  - DTLS forced off (server is TCP-only anti-DPI)
 *  - calls openconnect_set_camouflage_secret() (4th-patch public API)
 */

#include "vpninfo.h"
#include "config.h"
#include "dialog/MyInputDialog.h"
#include "dialog/MyMsgBox.h"
#include "dialog/mainwindow.h"
#include "logger.h"
#include "server_storage.h"
#include "wintun_client.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <cstdarg>
#include <cstdio>

extern "C" {
#include <openconnect.h>
}

static int last_form_empty;

static void stats_vfn(void* privdata, const struct oc_stats* stats)
{
    VpnInfo* vpn = static_cast<VpnInfo*>(privdata);
    const char* cipher = openconnect_get_cstp_cipher(vpn->vpninfo);
    QString cstp;
    if (cipher) cstp = QLatin1String(cipher);
    vpn->m->updateStats(stats, cstp);
}

static void progress_vfn(void* privdata, int level, const char* fmt, ...)
{
    char buf[512];
    if (level == PRG_TRACE) return;

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = 0;
    Logger::instance().addMessage(buf);
}

/* Auth-form callback: username and password only.
 * We never present a certificate/group selector in this fork. */
static int process_auth_form(void* privdata, struct oc_auth_form* form)
{
    VpnInfo* vpn = static_cast<VpnInfo*>(privdata);
    bool ok;
    QString text;
    int empty = 1;

    if (form->banner) Logger::instance().addMessage(QLatin1String(form->banner));
    if (form->message) Logger::instance().addMessage(QLatin1String(form->message));
    if (form->error) Logger::instance().addMessage(QLatin1String(form->error));

    for (struct oc_form_opt* opt = form->opts; opt; opt = opt->next) {
        text.clear();
        if (opt->flags & OC_FORM_OPT_IGNORE) continue;

        if (opt->type == OC_FORM_OPT_TEXT) {
            if (vpn->form_attempt == 0 && !vpn->ss->get_username().isEmpty()
                && strcasecmp(opt->name, "username") == 0) {
                openconnect_set_option_value(opt, vpn->ss->get_username().toUtf8().data());
                empty = 0;
                continue;
            }
            do {
                MyInputDialog dialog(vpn->m, QLatin1String(opt->name),
                    QLatin1String(opt->label), QLineEdit::Normal);
                dialog.show();
                ok = dialog.result(text);
                if (!ok) return OC_FORM_RESULT_CANCELLED;
            } while (text.isEmpty());

            if (strcasecmp(opt->name, "username") == 0) vpn->ss->set_username(text);
            openconnect_set_option_value(opt, text.toUtf8().data());
            vpn->form_attempt++;
            empty = 0;
        } else if (opt->type == OC_FORM_OPT_PASSWORD) {
            if (vpn->form_pass_attempt == 0 && !vpn->ss->get_password().isEmpty()
                && strcasecmp(opt->name, "password") == 0) {
                openconnect_set_option_value(opt, vpn->ss->get_password().toUtf8().data());
                empty = 0;
                continue;
            }
            do {
                MyInputDialog dialog(vpn->m, QLatin1String(opt->name),
                    QLatin1String(opt->label), QLineEdit::Password);
                dialog.show();
                ok = dialog.result(text);
                if (!ok) return OC_FORM_RESULT_CANCELLED;
            } while (text.isEmpty());

            if (strcasecmp(opt->name, "password") == 0 && vpn->form_pass_attempt == 0) {
                vpn->ss->set_password(text);
            }
            openconnect_set_option_value(opt, text.toUtf8().data());
            vpn->form_pass_attempt++;
            empty = 0;
        }
        /* Select/Group opts ignored — we set group via profile if needed. */
    }

    if (last_form_empty && empty) return OC_FORM_RESULT_CANCELLED;
    last_form_empty = empty;
    return OC_FORM_RESULT_OK;
}

/*
 * In keenetic-camouflage fork we use a bundled CA-only PKI (Let's Encrypt
 * intermediates), no TOFU pinning. validate_peer_cert is therefore a no-op
 * that trusts whatever gnutls already validated against /etc/ssl/certs/keenetic-ca.crt.
 */
static int validate_peer_cert(void* privdata, const char* /*reason*/)
{
    (void)privdata;
    return 0;
}

/*
 * setup_tun callback — delegated to KeeneticVpnService via named pipe.
 * The service owns the WinTun adapter (LocalSystem privileges) and we
 * never call openconnect_setup_tun_device() ourselves on Windows so the
 * GUI never needs UAC. The service hands us back a tunnel handle which
 * we hand to openconnect_setup_tun_fd().
 *
 * On non-Windows builds (dev only) we fall back to the upstream path.
 */
static void setup_tun_vfn(void* privdata)
{
    VpnInfo* vpn = static_cast<VpnInfo*>(privdata);

#ifdef _WIN32
    static WintunClient client;
    if (!client.ensureAvailable()) {
        vpn->last_err = QObject::tr(
            "KeeneticVpnService is not available. Install Keenetic-VPN-Setup.msi "
            "(one-time, requires Administrator) — afterwards the GUI runs as a "
            "normal user.");
        Logger::instance().addMessage(vpn->last_err);
        return;
    }
    QString err;
    if (!client.openAdapter(QStringLiteral("KeeneticVPN"), &err)) {
        vpn->last_err = QObject::tr("WinTun open_adapter failed: %1").arg(err);
        Logger::instance().addMessage(vpn->last_err);
        return;
    }
    /* If the service handed us a HANDLE, plug it into libopenconnect. */
    qint64 tunHandle = client.tunHandle();
    if (tunHandle > 0) {
        openconnect_setup_tun_fd(vpn->vpninfo, (int)(intptr_t)tunHandle);
    }
    Logger::instance().addMessage(QObject::tr(
        "WinTun adapter open via KeeneticVpnService — no UAC required"));
#else
    QByteArray vpncScriptFullPath;
    vpncScriptFullPath.append(QCoreApplication::applicationDirPath().toUtf8());
    vpncScriptFullPath.append(QDir::separator().toLatin1());
    vpncScriptFullPath.append(DEFAULT_VPNC_SCRIPT);
    int ret = openconnect_setup_tun_device(vpn->vpninfo,
        vpncScriptFullPath.constData(), nullptr);
    if (ret != 0) vpn->last_err = QObject::tr("Error setting up the TUN device");
    vpn->logVpncScriptOutput();
#endif
}

static inline int set_sock_block(int fd)
{
#ifdef _WIN32
    unsigned long mode = 0;
    return ioctlsocket(fd, FIONBIO, &mode);
#else
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
#endif
}

VpnInfo::VpnInfo(QString name, StoredServer* ss, MainWindow* m)
{
    this->vpninfo = openconnect_vpninfo_new(name.toUtf8().data(),
        validate_peer_cert, nullptr,
        process_auth_form, progress_vfn, this);
    if (!this->vpninfo) throw std::runtime_error("initial setup fails");

    this->cmd_fd = openconnect_setup_cmd_pipe(vpninfo);
    if (this->cmd_fd == INVALID_SOCKET) {
        Logger::instance().addMessage(QObject::tr("invalid socket"));
        throw std::runtime_error("pipe setup fails");
    }
    set_sock_block(this->cmd_fd);

    this->last_err = "";
    this->ss = ss;
    this->m = m;
    form_attempt = 0;
    form_pass_attempt = 0;

    openconnect_set_stats_handler(this->vpninfo, stats_vfn);
    openconnect_set_protocol(vpninfo, ss->get_protocol_name());
    openconnect_set_setup_tun_handler(vpninfo, setup_tun_vfn);

    /* Force TLS-only — server is anti-DPI, DTLS is fingerprintable. */
    openconnect_disable_dtls(vpninfo);

    /* Camouflage Level 2 envelope (HMAC CSTP magic + header rewriting +
     * /api/v1/session tunnel URL + TLS scatter). One call. */
    const QString secret = ss->get_camouflage_secret();
    if (!secret.isEmpty()) {
        openconnect_set_camouflage_secret(vpninfo, secret.toUtf8().constData());
        Logger::instance().addMessage(QObject::tr(
            "Camouflage envelope enabled (secret hash %1)").arg(secret.length()));
    } else {
        Logger::instance().addMessage(QObject::tr(
            "WARNING: no camouflage-secret in profile — connection will fail "
            "(server requires camouflage=2)"));
    }
}

VpnInfo::~VpnInfo()
{
    if (vpninfo) openconnect_vpninfo_free(vpninfo);
    if (ss) delete ss;
}

void VpnInfo::parse_url(const char* url)
{
    openconnect_parse_url(this->vpninfo, const_cast<char*>(url));
}

int VpnInfo::connect()
{
    int ret;
    QString ca_file = ss->get_ca_cert_file();
    if (ca_file.isEmpty()) {
        /* Default: bundled CA installed next to the GUI exe. */
        ca_file = QCoreApplication::applicationDirPath()
                  + QDir::separator() + QLatin1String("keenetic-ca.crt");
    }
    if (QFile::exists(ca_file)) {
        openconnect_set_system_trust(vpninfo, 0);
        openconnect_set_cafile(vpninfo, ca_file.toUtf8().data());
    }

#ifdef Q_OS_WIN32
    const QString osName{ "win" };
#elif defined Q_OS_LINUX
    const QString osName = QString("linux%1")
        .arg(QSysInfo::buildCpuArchitecture() == "i386" ? "" : "-64")
        .toStdString().c_str();
#else
    const QString osName{ "win" };  // fork is Windows-only
#endif
    openconnect_set_reported_os(vpninfo, osName.toStdString().c_str());

    /* Browser-like user agent (no AnyConnect fingerprint). */
    openconnect_set_useragent(vpninfo,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    ret = openconnect_obtain_cookie(vpninfo);
    if (ret != 0) {
        this->last_err = QObject::tr("Authentication error; cannot obtain cookie");
        return ret;
    }

    ret = openconnect_make_cstp_connection(vpninfo);
    if (ret != 0) {
        this->last_err = QObject::tr("Error establishing the CSTP channel");
        return ret;
    }
    return 0;
}

void VpnInfo::mainloop()
{
    while (true) {
        int ret = openconnect_mainloop(vpninfo,
            ss->get_reconnect_timeout(), RECONNECT_INTERVAL_MIN);
        if (ret != 0) {
            this->last_err = QObject::tr("Disconnected");
            logVpncScriptOutput();
            break;
        }
    }
}

void VpnInfo::get_info(QString& dns, QString& ip, QString& ip6)
{
    const struct oc_ip_info* info;
    int ret = openconnect_get_ip_info(this->vpninfo, &info, nullptr, nullptr);
    if (ret == 0) {
        if (info->addr) {
            ip = info->addr;
            if (info->netmask) { ip += "/"; ip += info->netmask; }
        }
        if (info->addr6) {
            ip6 = info->addr6;
            if (info->netmask6) { ip6 += "/"; ip6 += info->netmask6; }
        }
        if (info->dns[0]) dns = info->dns[0];
        if (info->dns[1]) { dns += ", "; dns += info->dns[1]; }
        if (info->dns[2]) { dns += " ";  dns += info->dns[2]; }
    }
}

void VpnInfo::get_cipher_info(QString& cstp)
{
    const char* cipher = openconnect_get_cstp_cipher(this->vpninfo);
    if (cipher) cstp = QLatin1String(cipher);
}

SOCKET VpnInfo::get_cmd_fd() const { return cmd_fd; }

void VpnInfo::reset_vpn()
{
    openconnect_reset_ssl(vpninfo);
    form_pass_attempt = 0;
    form_attempt = 0;
}

bool VpnInfo::get_minimize() const { return ss->get_minimize(); }

void VpnInfo::logVpncScriptOutput()
{
    QString tfile = QDir::tempPath() + QDir::separator() + QLatin1String("vpnc.log");
    QFile file(tfile);
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream in(&file);
        QString bannerMessage;
        bool processBannerMessage = false;
        while (!in.atEnd()) {
            const QString line{ in.readLine() };
            Logger::instance().addMessage(line);
            if (line == QLatin1String("--------------------- BANNER ---------------------")) {
                processBannerMessage = true; continue;
            }
            if (line == QLatin1String("------------------- BANNER end -------------------")) {
                processBannerMessage = false; continue;
            }
            if (processBannerMessage) bannerMessage += line + "\n";
        }
        file.close();
        file.remove();

        if (!ss->get_batch_mode() && !bannerMessage.isEmpty()) {
            MyMsgBox msgBox(this->m, bannerMessage, QString(""), QString("Accept"));
            msgBox.show();
            if (!msgBox.result()) this->m->on_disconnectClicked();
        }
    }
}
