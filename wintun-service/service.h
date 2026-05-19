/*
 * KeeneticVpnService — privileged Windows service that owns the WinTun
 * adapter on behalf of the unprivileged GUI client.
 *
 * Architecture:
 *   - Service runs as LocalSystem, autostart=demand.
 *   - Listens on \\.\pipe\KeeneticVpnService (named pipe).
 *   - Pipe ACL grants connect/read/write to BUILTIN\Administrators and
 *     to a local group "KeeneticVPNUsers" (installer adds first user).
 *   - On `open_adapter` op, calls WintunCreateAdapter / WintunOpenAdapter
 *     from wintun.dll (bundled), starts a session and returns the WinTun
 *     SESSION handle so the GUI can pump packets directly.
 *   - On `set_routes` / `set_dns` op, applies routing-table and resolver
 *     entries via IPHelper (CreateIpForwardEntry2, SetInterfaceDnsSettings).
 *
 * The result: the GUI process never needs UAC. The driver, the adapter
 * and the route-table edits all live behind the service's LocalSystem
 * security context. Install once → run forever.
 *
 * GPL v2.
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <string>

#define KEENETIC_SVC_NAME    L"KeeneticVpnService"
#define KEENETIC_SVC_DISPLAY L"Keenetic anti-DPI VPN — WinTun service"
#define KEENETIC_PIPE        L"\\\\.\\pipe\\KeeneticVpnService"
#define KEENETIC_GROUP       L"KeeneticVPNUsers"
#define KEENETIC_ADAPTER     L"KeeneticVPN"
#define KEENETIC_LOGFILE     L"%ProgramData%\\KeeneticVPN\\service.log"

void  svc_log(const char* fmt, ...);
int   svc_install(const wchar_t* binPath);
int   svc_uninstall(void);
int   svc_run_service(void);

/* Pipe protocol op codes (length-prefixed JSON). */
enum class OpCode {
    Ping,
    OpenAdapter,
    CloseAdapter,
    SetRoutes,
    SetDns,
    AddRoute,
    Status,
    Shutdown,
    Unknown,
};

OpCode op_from_string(const std::string& s);

/* WinTun adapter wrapper. */
struct WintunHandles;
WintunHandles* wintun_open_or_create(const wchar_t* name);
void           wintun_close(WintunHandles* h);
HANDLE         wintun_read_wait_event(WintunHandles* h);

/* IPHelper convenience. */
bool ip_add_route(const std::wstring& cidr, const std::wstring& gateway, unsigned long ifindex);
bool ip_set_dns(unsigned long ifindex, const std::vector<std::wstring>& servers,
                const std::vector<std::wstring>& search_domains);
