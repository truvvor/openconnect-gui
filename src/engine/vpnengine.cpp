/*
 * openconnect-gui — headless libopenconnect engine (service-side).
 * Ported from src/vpninfo.cpp. GPLv2-or-later.
 */
#include "vpnengine.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextCodec>

#include <cstdarg>
#include <cstdio>
#include <stdexcept>

extern "C" {
#include <openconnect.h>
}

#ifdef _WIN32
#include <windows.h>   // GetOEMCP() for decoding vpnc-script console output
#else
#include <fcntl.h>
#endif

using namespace oc::engine;

namespace {
int last_form_empty = 0;

QString tmpDir()
{
    QString base = qEnvironmentVariable("ProgramData");
    if (base.isEmpty())
        base = QStringLiteral("C:/ProgramData");
    QString d = base + QStringLiteral("/OpenConnect-GUI/tmp");
    QDir().mkpath(d);
    return d;
}

void stats_vfn(void* priv, const struct oc_stats* stats)
{
    auto* e = static_cast<VpnEngine*>(priv);
    QString cstp, dtls;
    if (const char* c = openconnect_get_cstp_cipher(e->vpninfo))
        cstp = QLatin1String(c);
    if (const char* d = openconnect_get_dtls_cipher(e->vpninfo))
        dtls = QLatin1String(d);
    e->host->onStats(stats->rx_bytes, stats->tx_bytes, cstp, dtls);
}

void progress_vfn(void* priv, int level, const char* fmt, ...)
{
    auto* e = static_cast<VpnEngine*>(priv);
    if (level == PRG_TRACE)
        return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    size_t len = strlen(buf);
    if (len && buf[len - 1] == '\n')
        buf[len - 1] = 0;
    e->host->onLog(level, QString::fromUtf8(buf));
}
} // namespace

namespace {
int process_auth_form(void* priv, struct oc_auth_form* form)
{
    auto* e = static_cast<VpnEngine*>(priv);
    EngineHost* H = e->host;

    if (form->banner) H->onLog(PRG_INFO, QLatin1String(form->banner));
    if (form->message) H->onLog(PRG_INFO, QLatin1String(form->message));
    if (form->error) H->onLog(PRG_ERR, QLatin1String(form->error));

    /* authgroup selection (may require an extra round -> NEWGROUP) */
    if (form->authgroup_opt) {
        struct oc_form_opt_select* sel = form->authgroup_opt;
        QStringList names;
        for (int i = 0; i < sel->nr_choices; i++)
            names << QLatin1String(sel->choices[i]->name);

        if (sel->nr_choices == 1) {
            openconnect_set_option_value(&sel->form, sel->choices[0]->name);
        } else if (names.contains(e->profile.groupname)) {
            openconnect_set_option_value(&sel->form, e->profile.groupname.toLatin1().data());
        } else {
            AuthForm gf;
            gf.hasGroup = true;
            gf.groupName = QLatin1String(sel->form.name);
            gf.groupLabel = QLatin1String(sel->form.label);
            gf.groupCurrent = e->profile.groupname;
            for (int i = 0; i < sel->nr_choices; i++)
                gf.groupChoices.append({ QLatin1String(sel->choices[i]->name),
                                         QLatin1String(sel->choices[i]->label) });
            if (!H->askAuthForm(gf) || gf.groupValue.isEmpty())
                return OC_FORM_RESULT_CANCELLED;
            openconnect_set_option_value(&sel->form, gf.groupValue.toLatin1().data());
            e->profile.groupname = gf.groupValue;
            H->onPersistString(QStringLiteral("groupname"), gf.groupValue);
        }
        if (e->authgroup_set == 0) {
            e->authgroup_set = 1;
            return OC_FORM_RESULT_NEWGROUP;
        }
    }

    /* gather remaining opts into one form, pre-filling known creds */
    AuthForm af;
    if (form->banner) af.banner = QLatin1String(form->banner);
    if (form->message) af.message = QLatin1String(form->message);
    if (form->error) af.error = QLatin1String(form->error);

    QVector<struct oc_form_opt*> mapped;
    int empty = 1;
    for (struct oc_form_opt* opt = form->opts; opt; opt = opt->next) {
        if (opt->flags & OC_FORM_OPT_IGNORE)
            continue;
        if (opt->type == OC_FORM_OPT_SELECT) {
            auto* so = reinterpret_cast<struct oc_form_opt_select*>(opt);
            if (so == form->authgroup_opt)
                continue;
            FormOpt fo;
            fo.name = QLatin1String(opt->name);
            fo.label = QLatin1String(opt->label);
            fo.type = QStringLiteral("select");
            for (int i = 0; i < so->nr_choices; i++)
                fo.choices.append({ QLatin1String(so->choices[i]->name),
                                    QLatin1String(so->choices[i]->label) });
            af.opts.append(fo);
            mapped.append(opt);
            empty = 0;
        } else if (opt->type == OC_FORM_OPT_TEXT) {
            if (e->form_attempt == 0 && !e->profile.username.isEmpty()
                && strcasecmp(opt->name, "username") == 0) {
                openconnect_set_option_value(opt, e->profile.username.toLatin1().data());
                empty = 0;
                continue;
            }
            FormOpt fo;
            fo.name = QLatin1String(opt->name);
            fo.label = QLatin1String(opt->label);
            fo.type = QStringLiteral("text");
            af.opts.append(fo);
            mapped.append(opt);
            empty = 0;
        } else if (opt->type == OC_FORM_OPT_PASSWORD) {
            if (e->form_pass_attempt == 0 && !e->profile.password.isEmpty()
                && strcasecmp(opt->name, "password") == 0) {
                openconnect_set_option_value(opt, e->profile.password.toLatin1().data());
                e->form_pass_attempt++; /* used once; a re-presented form (wrong saved pw) will prompt */
                empty = 0;
                continue;
            }
            FormOpt fo;
            fo.name = QLatin1String(opt->name);
            fo.label = QLatin1String(opt->label);
            fo.type = QStringLiteral("password");
            af.opts.append(fo);
            mapped.append(opt);
            empty = 0;
        }
    }

    if (!af.opts.isEmpty()) {
        if (!H->askAuthForm(af))
            return OC_FORM_RESULT_CANCELLED;
        for (int i = 0; i < af.opts.size(); i++) {
            const QString val = af.opts[i].value;
            struct oc_form_opt* opt = mapped[i];
            openconnect_set_option_value(opt, val.toLatin1().data());
            if (strcasecmp(opt->name, "username") == 0) {
                e->profile.username = val;
                H->onPersistString(QStringLiteral("username"), val);
            } else if (strcasecmp(opt->name, "password") == 0) {
                e->profile.password = val;
                e->password_set = 1;
                H->onPersistString(QStringLiteral("password"), val);
            }
        }
        e->form_attempt++;
        e->form_pass_attempt++;
    }

    if (last_form_empty && empty)
        return OC_FORM_RESULT_CANCELLED;
    last_form_empty = empty;
    return OC_FORM_RESULT_OK;
}

int validate_peer_cert(void* priv, const char* reason)
{
    auto* e = static_cast<VpnEngine*>(priv);
    EngineHost* H = e->host;

    const char* hash = openconnect_get_peer_cert_hash(e->vpninfo);
    if (hash == nullptr) {
        H->onLog(PRG_ERR, QStringLiteral("Error getting peer's certificate hash"));
        return -1;
    }
    const QString curHash = QLatin1String(hash);

    unsigned char* der = nullptr;
    int derSize = openconnect_get_peer_cert_DER(e->vpninfo, &der);
    QByteArray derBytes;
    if (derSize > 0)
        derBytes = QByteArray(reinterpret_cast<const char*>(der), derSize);

    /* pinned trust passed in via profile.trust (the GUI's gtdb store) */
    bool known = false, mismatch = false;
    for (const auto& pin : e->profile.trust) {
        if (pin.hash == curHash) { known = true; break; }
    }
    if (!known && !e->profile.trust.isEmpty())
        mismatch = true;
    if (known)
        return 0;

    char* details = openconnect_get_peer_cert_details(e->vpninfo);
    QString dstr;
    if (details) { dstr = QString::fromUtf8(details); free(details); }

    const QString change = mismatch ? QStringLiteral("key-mismatch") : QStringLiteral("unknown");
    if (!H->askCert(QString::fromUtf8(reason ? reason : ""), e->profile.server,
                    curHash, dstr, change))
        return -1;

    H->onPersistTrust(curHash, QString::fromLatin1(derBytes.toBase64()));
    return 0;
}

int lock_token_vfn(void* priv)
{
    auto* e = static_cast<VpnEngine*>(priv);
    openconnect_set_token_mode(e->vpninfo,
        (oc_token_mode_t)e->profile.tokenType, e->profile.tokenSecret.toLatin1().data());
    return 0;
}

int unlock_token_vfn(void* priv, const char* newtok)
{
    auto* e = static_cast<VpnEngine*>(priv);
    e->profile.tokenSecret = QString::fromLatin1(newtok);
    e->host->onPersistString(QStringLiteral("token"), e->profile.tokenSecret);
    return 0;
}

void setup_tun_vfn(void* priv)
{
    auto* e = static_cast<VpnEngine*>(priv);
    /* Use the vpnc script installed next to the binary. The installer ships it
     * as "vpnc-script.js" (DEFAULT_VPNC_SCRIPT); fall back to the openconnect
     * zip's "vpnc-script-win.js" only if that's what's actually present. */
    const QString dir = QCoreApplication::applicationDirPath();
    QString scriptPath = dir + "/vpnc-script.js";
    if (!QFile::exists(scriptPath))
        scriptPath = dir + "/vpnc-script-win.js";
    const QByteArray script = scriptPath.toLatin1();
    int ret = openconnect_setup_tun_device(e->vpninfo, script.constData(), nullptr);
    if (ret == 0)
        e->host->onLog(PRG_INFO, QStringLiteral("vpnc-script: %1").arg(scriptPath));
    if (ret != 0)
        e->m_lastErr = QStringLiteral("Error setting up the TUN device");
    e->logVpncScriptOutput();
}
} // namespace

#ifdef _WIN32
#define oc_pipe_write(fd, p, n) send((fd), (p), (n), 0)
#else
#define oc_pipe_write(fd, p, n) write((fd), (p), (n))
#endif

VpnEngine::VpnEngine(const oc::ipc::Profile& p, EngineHost* h)
    : profile(p), host(h) {}

VpnEngine::~VpnEngine()
{
    if (vpninfo)
        openconnect_vpninfo_free(vpninfo);
    cleanupTempFiles();
}

QString VpnEngine::writeTempPem(const QByteArray& pem, const QString& tag)
{
    if (pem.isEmpty())
        return {};
    const QString path = QStringLiteral("%1/oc-%2-%3.pem")
        .arg(tmpDir(), tag).arg(QCoreApplication::applicationPid());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        host->onLog(PRG_ERR, QStringLiteral("cannot write temp %1 file").arg(tag));
        return {};
    }
    f.write(pem);
    f.close();
    m_tempFiles << path;
    return path;
}

void VpnEngine::cleanupTempFiles()
{
    for (const QString& p : m_tempFiles)
        QFile::remove(p);
    m_tempFiles.clear();
}

bool VpnEngine::setup()
{
    vpninfo = openconnect_vpninfo_new("Open AnyConnect VPN Agent",
        validate_peer_cert, nullptr, process_auth_form, progress_vfn, this);
    if (!vpninfo) {
        m_lastErr = QStringLiteral("libopenconnect init failed");
        return false;
    }
    m_cmdFd = openconnect_setup_cmd_pipe(vpninfo);
    if (m_cmdFd == INVALID_SOCKET) {
        m_lastErr = QStringLiteral("command pipe setup failed");
        return false;
    }
#ifdef _WIN32
    { unsigned long mode = 0; ioctlsocket(m_cmdFd, FIONBIO, &mode); }
#endif
    openconnect_set_stats_handler(vpninfo, stats_vfn);

    if (profile.tokenType != 0 && !profile.tokenSecret.isEmpty()) {
        openconnect_set_token_callbacks(vpninfo, this, lock_token_vfn, unlock_token_vfn);
        openconnect_set_token_mode(vpninfo,
            (oc_token_mode_t)profile.tokenType, profile.tokenSecret.toLatin1().data());
    }

    openconnect_set_protocol(vpninfo, profile.protocol.toLatin1().data());

#ifdef OC_HAVE_CAMOUFLAGE_API
    if (!profile.camouflageSecret.isEmpty()) {
        openconnect_set_camouflage_secret(vpninfo, profile.camouflageSecret.toUtf8().constData());
        host->onLog(PRG_INFO, QStringLiteral("Anti-DPI camouflage: enabled"));
    }
#endif
    if (profile.disableUdp)
        openconnect_disable_dtls(vpninfo);

    openconnect_set_setup_tun_handler(vpninfo, setup_tun_vfn);
    openconnect_parse_url(vpninfo, profile.server.toLocal8Bit().data());
    return true;
}

int VpnEngine::doConnect()
{
    QString certFile = writeTempPem(profile.clientCertPem, QStringLiteral("cert"));
    QString keyFile = profile.clientKeyPem.isEmpty()
        ? certFile : writeTempPem(profile.clientKeyPem, QStringLiteral("key"));
    if (!certFile.isEmpty())
        openconnect_set_client_cert(vpninfo, certFile.toLatin1().data(), keyFile.toLatin1().data());

    QString caFile = writeTempPem(profile.caCertPem, QStringLiteral("ca"));
    if (!caFile.isEmpty()) {
        openconnect_set_system_trust(vpninfo, 0);
        openconnect_set_cafile(vpninfo, caFile.toLatin1().data());
    } else {
        const QString bundle = QCoreApplication::applicationDirPath() + "/ca-certificates.crt";
        if (QFile::exists(bundle)) {
            openconnect_set_cafile(vpninfo, bundle.toLatin1().data());
            host->onLog(PRG_INFO, QStringLiteral("Using bundled CA: %1").arg(bundle));
        }
    }

    openconnect_set_reported_os(vpninfo, profile.reportedOs.toLatin1().data());

    int ret = openconnect_obtain_cookie(vpninfo);
    if (ret != 0) {
        m_lastErr = QStringLiteral("Authentication error; cannot obtain cookie");
        return ret;
    }
    ret = openconnect_make_cstp_connection(vpninfo);
    if (ret != 0) {
        m_lastErr = QStringLiteral("Error establishing the CSTP channel");
        return ret;
    }
    return 0;
}

int VpnEngine::dtlsConnect()
{
    if (!profile.disableUdp) {
        int ret = openconnect_setup_dtls(vpninfo, profile.dtlsReconnectTimeout);
        if (ret != 0) {
            m_lastErr = QStringLiteral("Error setting up DTLS");
            return ret;
        }
    }
    return 0;
}

void VpnEngine::mainloop()
{
    while (true) {
        int ret = openconnect_mainloop(vpninfo, profile.reconnectTimeout, RECONNECT_INTERVAL_MIN);
        if (ret != 0) {
            m_lastErr = QStringLiteral("Disconnected");
            logVpncScriptOutput();
            break;
        }
    }
}

int VpnEngine::run()
{
    host->onState(QStringLiteral("connecting"));
    const bool passWasEmpty = profile.password.isEmpty();
    int ret = 0, retries = 2;
    bool retry;
    do {
        retry = false;
        ret = doConnect();
        if (ret != 0) {
            if (retries-- <= 0) { host->onState(QStringLiteral("error"), m_lastErr); host->onState(QStringLiteral("disconnected")); return ret; }
            if (!passWasEmpty) {
                profile.password.clear();
                profile.groupname.clear();
                openconnect_reset_ssl(vpninfo);
                form_pass_attempt = password_set = authgroup_set = form_attempt = 0;
                host->onLog(PRG_INFO, QStringLiteral("Auth failed in batch mode, retrying interactively"));
                retry = true;
                continue;
            }
            host->onLog(PRG_ERR, m_lastErr);
            host->onState(QStringLiteral("error"), m_lastErr);
            host->onState(QStringLiteral("disconnected"));
            return ret;
        }
    } while (retry);

    dtlsConnect();

    const struct oc_ip_info* info = nullptr;
    if (openconnect_get_ip_info(vpninfo, &info, nullptr, nullptr) == 0 && info) {
        QString dns;
        if (info->dns[0]) dns = QLatin1String(info->dns[0]);
        if (info->dns[1]) dns += ", " + QLatin1String(info->dns[1]);
        host->onIpInfo(info->addr ? QLatin1String(info->addr) : QString(),
                       info->netmask ? QLatin1String(info->netmask) : QString(),
                       info->addr6 ? QLatin1String(info->addr6) : QString(), dns);
    }

    host->onState(QStringLiteral("connected"));
    mainloop();
    host->onState(QStringLiteral("disconnected"), m_lastErr);
    return 0;
}

void VpnEngine::cancel()
{
    if (m_cmdFd != INVALID_SOCKET) {
        char cmd = OC_CMD_CANCEL;
        oc_pipe_write(m_cmdFd, &cmd, 1);
    }
}

void VpnEngine::requestStats()
{
    if (m_cmdFd != INVALID_SOCKET) {
        char cmd = OC_CMD_STATS;
        oc_pipe_write(m_cmdFd, &cmd, 1);
    }
}

void VpnEngine::logVpncScriptOutput()
{
    QString path = QDir::tempPath() + QStringLiteral("/vpnc.log");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray raw = file.readAll();
    file.close();
    file.remove();

    /* The vpnc-script log is MIXED-encoding: `route` emits the OEM codepage
     * (cp866 on RU Windows) while `netsh` emits UTF-8 when its output is
     * redirected. Decode per line: use UTF-8 when the bytes are valid UTF-8,
     * otherwise fall back to the OEM codepage. */
#ifdef _WIN32
    QTextCodec* oem = QTextCodec::codecForName("CP" + QByteArray::number(GetOEMCP()));
    if (!oem)
        oem = QTextCodec::codecForName("IBM866");
#else
    QTextCodec* oem = nullptr;
#endif

    QString banner;
    bool inBanner = false;
    const QList<QByteArray> rawLines = raw.split('\n');
    for (QByteArray rl : rawLines) {
        if (rl.endsWith('\r'))
            rl.chop(1);
        if (rl.isEmpty())
            continue;
        QString line = QString::fromUtf8(rl);
        if (oem && line.contains(QChar(QChar::ReplacementCharacter)))
            line = oem->toUnicode(rl);   // not valid UTF-8 -> OEM (route output)
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        host->onLog(PRG_INFO, line);
        if (line == QLatin1String("--------------------- BANNER ---------------------")) { inBanner = true; continue; }
        if (line == QLatin1String("------------------- BANNER end -------------------")) { inBanner = false; continue; }
        if (inBanner) banner += line + "\n";
    }
    if (!banner.isEmpty()) {
        if (profile.autoAcceptBanner) {
            host->onLog(PRG_INFO, QStringLiteral("Banner auto-accepted"));
        } else if (!host->askBanner(banner)) {
            cancel();
        }
    }
}
