/*
 * wintun_client.cpp — GUI-side named-pipe client for KeeneticVpnService.
 *
 * GPL v2.
 */

#include "wintun_client.h"
#include "logger.h"

#ifdef _WIN32
#  include <windows.h>
#endif

#include <QByteArray>
#include <QFile>
#include <QString>

#ifdef _WIN32
static const wchar_t* kPipeName = L"\\\\.\\pipe\\KeeneticVpnService";
static const wchar_t* kSvcName  = L"KeeneticVpnService";
#endif

WintunClient::WintunClient(QObject* parent) : QObject(parent) {}

WintunClient::~WintunClient()
{
#ifdef _WIN32
    if (m_pipeHandle != -1) CloseHandle(reinterpret_cast<HANDLE>(m_pipeHandle));
#endif
}

bool WintunClient::startServiceIfNeeded()
{
#ifdef _WIN32
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, kSvcName, SERVICE_QUERY_STATUS | SERVICE_START);
    if (!svc) { CloseServiceHandle(scm); return false; }

    SERVICE_STATUS s;
    bool ok = true;
    if (QueryServiceStatus(svc, &s)) {
        if (s.dwCurrentState != SERVICE_RUNNING
            && s.dwCurrentState != SERVICE_START_PENDING) {
            ok = StartServiceW(svc, 0, nullptr) ? true : false;
            if (ok) {
                /* Wait up to 5s for RUNNING. */
                for (int i = 0; i < 50; ++i) {
                    if (!QueryServiceStatus(svc, &s)) break;
                    if (s.dwCurrentState == SERVICE_RUNNING) break;
                    Sleep(100);
                }
            }
        }
    } else { ok = false; }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok;
#else
    return true;
#endif
}

bool WintunClient::ensureAvailable()
{
#ifdef _WIN32
    if (!startServiceIfNeeded()) {
        Logger::instance().addMessage(QObject::tr(
            "KeeneticVpnService not installed or unable to start. "
            "Run Keenetic-VPN-Setup.msi as Administrator once."));
        return false;
    }
    /* Open the pipe (will retry briefly while service is just starting). */
    for (int i = 0; i < 30; ++i) {
        HANDLE h = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            m_pipeHandle = reinterpret_cast<qint64>(h);
            break;
        }
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY) { WaitNamedPipeW(kPipeName, 1000); continue; }
        Sleep(100);
    }
    if (m_pipeHandle == -1) {
        Logger::instance().addMessage(QObject::tr(
            "Cannot open pipe %1").arg(QString::fromWCharArray(kPipeName)));
        return false;
    }
    /* Sanity ping. */
    QByteArray reply;
    if (!sendOp(R"({"op":"ping"})", &reply)) return false;
    return reply.contains("\"ok\":true");
#else
    return true;
#endif
}

bool WintunClient::sendOp(const QByteArray& json, QByteArray* replyOut, QString* errorOut)
{
#ifdef _WIN32
    if (m_pipeHandle == -1) {
        if (errorOut) *errorOut = QStringLiteral("pipe not open");
        return false;
    }
    HANDLE h = reinterpret_cast<HANDLE>(m_pipeHandle);
    uint32_t len = (uint32_t)json.size();
    DWORD w = 0;
    if (!WriteFile(h, &len, 4, &w, nullptr) || w != 4) return false;
    if (!WriteFile(h, json.constData(), len, &w, nullptr) || w != len) return false;
    uint32_t rlen = 0; DWORD r = 0;
    if (!ReadFile(h, &rlen, 4, &r, nullptr) || r != 4) return false;
    if (rlen > 1u << 20) return false;
    QByteArray reply(rlen, 0);
    if (!ReadFile(h, reply.data(), rlen, &r, nullptr) || r != rlen) return false;
    if (replyOut) *replyOut = reply;
    return true;
#else
    (void)json; (void)replyOut; (void)errorOut;
    return false;
#endif
}

static QByteArray jsonEscape(const QString& s)
{
    QByteArray out;
    for (QChar c : s) {
        if (c == '\\' || c == '"') { out += '\\'; out += c.toLatin1(); }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c.unicode() < 0x20) {
            out += QString::asprintf("\\u%04x", c.unicode()).toLatin1();
        } else {
            out += QString(c).toUtf8();
        }
    }
    return out;
}

bool WintunClient::openAdapter(const QString& adapterName, QString* errorOut)
{
    QByteArray req = R"({"op":"open_adapter","name":")";
    req += jsonEscape(adapterName);
    req += "\"}";
    QByteArray reply;
    if (!sendOp(req, &reply, errorOut)) return false;
    if (!reply.contains("\"ok\":true")) {
        if (errorOut) *errorOut = QString::fromUtf8(reply);
        return false;
    }
    /* Parse tun_fd if present. */
    int p = reply.indexOf("\"tun_fd\":");
    if (p > 0) {
        m_tunHandle = atoll(reply.constData() + p + 9);
    }
    return true;
}

bool WintunClient::closeAdapter()
{
    QByteArray reply;
    return sendOp(R"({"op":"close_adapter"})", &reply);
}

bool WintunClient::setRoutes(const QStringList& cidrs, const QString& gateway)
{
    QByteArray req = R"({"op":"set_routes","gateway":")";
    req += jsonEscape(gateway);
    req += "\",\"routes\":[";
    for (int i = 0; i < cidrs.size(); ++i) {
        if (i) req += ",";
        req += "\""; req += jsonEscape(cidrs[i]); req += "\"";
    }
    req += "]}";
    QByteArray reply;
    return sendOp(req, &reply) && reply.contains("\"ok\":true");
}

bool WintunClient::setDns(const QStringList& servers, const QStringList& domains)
{
    QByteArray req = R"({"op":"set_dns","servers":[)";
    for (int i = 0; i < servers.size(); ++i) {
        if (i) req += ",";
        req += "\""; req += jsonEscape(servers[i]); req += "\"";
    }
    req += "],\"domains\":[";
    for (int i = 0; i < domains.size(); ++i) {
        if (i) req += ",";
        req += "\""; req += jsonEscape(domains[i]); req += "\"";
    }
    req += "]}";
    QByteArray reply;
    return sendOp(req, &reply) && reply.contains("\"ok\":true");
}
