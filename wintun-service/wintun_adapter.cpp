/*
 * wintun_adapter.cpp — thin wrapper around WinTun.dll.
 *
 * We dynamically load WinTun (it ships next to KeeneticVpnService.exe as
 * wintun.dll signed by WireGuard LLC) and call WintunCreateAdapter /
 * WintunOpenAdapter / WintunStartSession. The driver is registered with
 * the Driver Store on first call (requires SeLoadDriverPrivilege which
 * LocalSystem has).
 *
 * GPL v2.
 */

#include "service.h"
#include <stdio.h>

/* WinTun ABI from upstream wintun.h, copied minimally so we don't need
 * the WinTun SDK at build time (we only use these symbols). */
typedef struct _WINTUN_ADAPTER* WINTUN_ADAPTER_HANDLE;
typedef struct _WINTUN_SESSION* WINTUN_SESSION_HANDLE;
typedef WINTUN_ADAPTER_HANDLE (WINAPI *WINTUN_CREATE_ADAPTER_FUNC)(LPCWSTR Name, LPCWSTR TunnelType, const GUID* RequestedGUID);
typedef WINTUN_ADAPTER_HANDLE (WINAPI *WINTUN_OPEN_ADAPTER_FUNC)(LPCWSTR Name);
typedef void                  (WINAPI *WINTUN_CLOSE_ADAPTER_FUNC)(WINTUN_ADAPTER_HANDLE Adapter);
typedef WINTUN_SESSION_HANDLE (WINAPI *WINTUN_START_SESSION_FUNC)(WINTUN_ADAPTER_HANDLE Adapter, DWORD Capacity);
typedef void                  (WINAPI *WINTUN_END_SESSION_FUNC)(WINTUN_SESSION_HANDLE Session);
typedef HANDLE                (WINAPI *WINTUN_GET_READ_WAIT_EVENT_FUNC)(WINTUN_SESSION_HANDLE Session);

struct WintunHandles {
    HMODULE                 dll                  { nullptr };
    WINTUN_ADAPTER_HANDLE   adapter              { nullptr };
    WINTUN_SESSION_HANDLE   session              { nullptr };
    WINTUN_CLOSE_ADAPTER_FUNC closeAdapterFunc   { nullptr };
    WINTUN_END_SESSION_FUNC endSessionFunc       { nullptr };
    WINTUN_GET_READ_WAIT_EVENT_FUNC readWaitFunc { nullptr };
};

static HMODULE load_wintun(void)
{
    /* Look next to the service exe, then PATH. */
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) {
        *(slash + 1) = 0;
        wcscat_s(path, MAX_PATH, L"wintun.dll");
        HMODULE m = LoadLibraryW(path);
        if (m) return m;
    }
    return LoadLibraryW(L"wintun.dll");
}

WintunHandles* wintun_open_or_create(const wchar_t* name)
{
    HMODULE dll = load_wintun();
    if (!dll) { svc_log("wintun.dll missing"); return nullptr; }

    auto create  = (WINTUN_CREATE_ADAPTER_FUNC)GetProcAddress(dll, "WintunCreateAdapter");
    auto open    = (WINTUN_OPEN_ADAPTER_FUNC)GetProcAddress(dll, "WintunOpenAdapter");
    auto close_a = (WINTUN_CLOSE_ADAPTER_FUNC)GetProcAddress(dll, "WintunCloseAdapter");
    auto start_s = (WINTUN_START_SESSION_FUNC)GetProcAddress(dll, "WintunStartSession");
    auto end_s   = (WINTUN_END_SESSION_FUNC)GetProcAddress(dll, "WintunEndSession");
    auto rdw     = (WINTUN_GET_READ_WAIT_EVENT_FUNC)GetProcAddress(dll, "WintunGetReadWaitEvent");

    if (!create || !open || !close_a || !start_s || !end_s || !rdw) {
        svc_log("wintun.dll missing exports");
        FreeLibrary(dll);
        return nullptr;
    }

    WintunHandles* h = new WintunHandles();
    h->dll = dll;
    h->closeAdapterFunc = close_a;
    h->endSessionFunc   = end_s;
    h->readWaitFunc     = rdw;

    h->adapter = open(name);
    if (!h->adapter) {
        h->adapter = create(name, L"WinTun", nullptr);
    }
    if (!h->adapter) {
        svc_log("WintunCreate/OpenAdapter failed: %lu", GetLastError());
        FreeLibrary(dll); delete h; return nullptr;
    }
    h->session = start_s(h->adapter, 0x400000); /* 4 MiB ring */
    if (!h->session) {
        svc_log("WintunStartSession failed: %lu", GetLastError());
        close_a(h->adapter); FreeLibrary(dll); delete h; return nullptr;
    }
    return h;
}

void wintun_close(WintunHandles* h)
{
    if (!h) return;
    if (h->session) h->endSessionFunc(h->session);
    if (h->adapter) h->closeAdapterFunc(h->adapter);
    if (h->dll)     FreeLibrary(h->dll);
    delete h;
}

HANDLE wintun_read_wait_event(WintunHandles* h)
{
    if (!h || !h->session) return nullptr;
    return h->readWaitFunc(h->session);
}

/* Placeholder route/DNS helpers — Phase 7 (live test) fills in actual
 * CreateIpForwardEntry2 + SetInterfaceDnsSettings calls based on what the
 * server's CSTP CONFIG-REPLY brings. The stubs return true so the pipe
 * protocol stays usable for early integration testing. */
bool ip_add_route(const std::wstring& /*cidr*/, const std::wstring& /*gateway*/,
                  unsigned long /*ifindex*/)
{
    return true;
}

bool ip_set_dns(unsigned long /*ifindex*/,
                const std::vector<std::wstring>& /*servers*/,
                const std::vector<std::wstring>& /*search_domains*/)
{
    return true;
}
