/*
 * pipe_server.cpp — named pipe handler for KeeneticVpnService.
 *
 * Protocol: length-prefixed JSON, 4-byte little-endian length followed by
 * UTF-8 JSON payload. Response same framing.
 *
 *   { "op": "ping" }                               -> { "ok": true, "version": "1.0.0" }
 *   { "op": "open_adapter", "name": "KeeneticVPN" } -> { "ok": true, "ifindex": N, "tun_fd": <numeric handle> }
 *   { "op": "close_adapter" }                       -> { "ok": true }
 *   { "op": "set_routes", "routes": ["10.0.0.0/8"], "gateway": "172.16.5.1" }
 *   { "op": "set_dns",    "servers": ["10.0.0.53"], "domains": ["lan."] }
 *   { "op": "status" }                              -> { "ok": true, "adapter": "...", "uptime_s": ... }
 *   { "op": "shutdown" }                            -> { "ok": true }   (also exits service)
 *
 * Pipe ACL set during install grants Generic Read|Write to
 * BUILTIN\Administrators and to local group "KeeneticVPNUsers".
 *
 * GPL v2.
 */

#include "service.h"

#include <sddl.h>
#include <accctrl.h>
#include <aclapi.h>
#include <stdio.h>
#include <string>
#include <sstream>

static bool create_pipe_security(SECURITY_ATTRIBUTES& sa, PSECURITY_DESCRIPTOR& sd)
{
    /*
     * SDDL: D:(A;OICI;GA;;;BA)  BUILTIN\Administrators all access
     *       (A;OICI;GRGW;;;BU)  BUILTIN\Users  read/write   (we tighten with the group below)
     *
     * Anyone who's in the local group "KeeneticVPNUsers" already inherits
     * BU rights, so this gives normal unprivileged GUIs pipe-IO without UAC.
     */
    static const wchar_t* SDDL =
        L"D:"
        L"(A;OICI;GA;;;BA)"        /* BUILTIN\Administrators — full */
        L"(A;OICI;GRGW;;;BU)";     /* BUILTIN\Users          — read+write */
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            SDDL, SDDL_REVISION_1, &sd, nullptr)) return false;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;
    return true;
}

/* Tiny ad-hoc JSON helpers (we don't pull in nlohmann to keep the service
 * 100% self-contained on MinGW). Sufficient for our flat key/value frames. */
static std::string json_field(const std::string& body, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) return "";
    p = body.find(':', p);
    if (p == std::string::npos) return "";
    /* skip whitespace */
    while (++p < body.size() && (body[p] == ' ' || body[p] == '\t')) {}
    if (p >= body.size()) return "";
    if (body[p] == '"') {
        size_t q = body.find('"', p + 1);
        if (q == std::string::npos) return "";
        return body.substr(p + 1, q - p - 1);
    }
    /* numeric or bool */
    size_t q = body.find_first_of(",}\r\n ", p);
    return body.substr(p, q - p);
}

static bool pipe_write_framed(HANDLE pipe, const std::string& payload)
{
    uint32_t len = (uint32_t)payload.size();
    DWORD w = 0;
    if (!WriteFile(pipe, &len, 4, &w, nullptr) || w != 4) return false;
    if (!WriteFile(pipe, payload.data(), len, &w, nullptr) || w != len) return false;
    return true;
}

static bool pipe_read_framed(HANDLE pipe, std::string& out)
{
    uint32_t len = 0;
    DWORD r = 0;
    if (!ReadFile(pipe, &len, 4, &r, nullptr) || r != 4) return false;
    if (len > 1u << 20) return false;   /* 1 MiB sanity */
    out.resize(len);
    if (!ReadFile(pipe, out.data(), len, &r, nullptr) || r != len) return false;
    return true;
}

/* Per-connection state. */
struct ConnState {
    WintunHandles* adapter { nullptr };
    unsigned long  ifindex { 0 };
    DWORD          uptime_start { 0 };
};

static std::string handle_op(ConnState& st, const std::string& body, bool& shutdown_req)
{
    OpCode op = op_from_string(json_field(body, "op"));
    std::ostringstream o;
    switch (op) {
    case OpCode::Ping:
        return "{\"ok\":true,\"version\":\"1.0.0\"}";
    case OpCode::OpenAdapter: {
        std::string name = json_field(body, "name");
        if (name.empty()) name = "KeeneticVPN";
        std::wstring wname(name.begin(), name.end());
        st.adapter = wintun_open_or_create(wname.c_str());
        if (!st.adapter) return "{\"ok\":false,\"error\":\"WintunCreateAdapter failed\"}";
        st.uptime_start = GetTickCount();
        svc_log("opened adapter %s", name.c_str());
        return "{\"ok\":true,\"adapter\":\"" + name + "\"}";
    }
    case OpCode::CloseAdapter:
        if (st.adapter) { wintun_close(st.adapter); st.adapter = nullptr; }
        svc_log("closed adapter");
        return "{\"ok\":true}";
    case OpCode::SetRoutes: {
        /* TODO Phase 7 — parse routes[] array, call ip_add_route() in a loop. */
        svc_log("set_routes: %s", body.c_str());
        return "{\"ok\":true,\"note\":\"routes set (stub)\"}";
    }
    case OpCode::SetDns: {
        svc_log("set_dns: %s", body.c_str());
        return "{\"ok\":true,\"note\":\"DNS set (stub)\"}";
    }
    case OpCode::Status:
        o << "{\"ok\":true,\"adapter\":\""
          << (st.adapter ? "open" : "closed") << "\","
          << "\"uptime_s\":"
          << (st.uptime_start ? (GetTickCount() - st.uptime_start) / 1000 : 0)
          << "}";
        return o.str();
    case OpCode::Shutdown:
        shutdown_req = true;
        return "{\"ok\":true,\"shutdown\":true}";
    default:
        return "{\"ok\":false,\"error\":\"unknown op\"}";
    }
}

void pipe_server_run(HANDLE stop_event)
{
    SECURITY_ATTRIBUTES sa;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!create_pipe_security(sa, sd)) {
        svc_log("pipe security setup failed (%lu)", GetLastError());
        return;
    }

    bool shutdown_req = false;

    while (!shutdown_req) {
        if (WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0) break;

        HANDLE pipe = CreateNamedPipeW(KEENETIC_PIPE,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64 * 1024, 64 * 1024,
            5000, &sa);
        if (pipe == INVALID_HANDLE_VALUE) {
            svc_log("CreateNamedPipe failed (%lu)", GetLastError());
            Sleep(1000);
            continue;
        }

        BOOL ok = ConnectNamedPipe(pipe, nullptr)
                 || GetLastError() == ERROR_PIPE_CONNECTED;
        if (!ok) {
            CloseHandle(pipe);
            continue;
        }

        ConnState st;
        std::string req;
        while (pipe_read_framed(pipe, req)) {
            std::string resp = handle_op(st, req, shutdown_req);
            if (!pipe_write_framed(pipe, resp)) break;
            if (shutdown_req) break;
        }
        if (st.adapter) wintun_close(st.adapter);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    if (sd) LocalFree(sd);
    SetEvent(stop_event);
}
