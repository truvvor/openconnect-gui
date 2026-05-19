/*
 * KeeneticVpnService — service entry + SCM dispatch + install/uninstall.
 *
 * Build: MinGW-w64 (one toolchain across the project; see CMakeLists.txt).
 *
 * GPL v2.
 */

#include "service.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>

static SERVICE_STATUS         g_svc_status      = {};
static SERVICE_STATUS_HANDLE  g_svc_status_h    = nullptr;
static HANDLE                 g_svc_stop_event  = nullptr;

void svc_log(const char* fmt, ...)
{
    char path_expanded[MAX_PATH];
    wchar_t path[MAX_PATH];
    ExpandEnvironmentStringsW(KEENETIC_LOGFILE, path, MAX_PATH);
    /* mkdir %ProgramData%\KeeneticVPN if missing */
    wchar_t dir[MAX_PATH];
    wcscpy_s(dir, MAX_PATH, path);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) { *slash = 0; CreateDirectoryW(dir, nullptr); }
    /* convert wchar to char for fopen */
    WideCharToMultiByte(CP_UTF8, 0, path, -1, path_expanded, MAX_PATH, nullptr, nullptr);

    FILE* f = fopen(path_expanded, "a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%04u-%02u-%02u %02u:%02u:%02u] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

OpCode op_from_string(const std::string& s)
{
    if (s == "ping")           return OpCode::Ping;
    if (s == "open_adapter")   return OpCode::OpenAdapter;
    if (s == "close_adapter")  return OpCode::CloseAdapter;
    if (s == "set_routes")     return OpCode::SetRoutes;
    if (s == "set_dns")        return OpCode::SetDns;
    if (s == "add_route")      return OpCode::AddRoute;
    if (s == "status")         return OpCode::Status;
    if (s == "shutdown")       return OpCode::Shutdown;
    return OpCode::Unknown;
}

/* Forward declarations of the pipe loop (pipe_server.cpp). */
void pipe_server_run(HANDLE stop_event);

static void WINAPI svc_ctrl_handler(DWORD ctrl)
{
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
        svc_log("SCM: STOP requested");
        g_svc_status.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_svc_status_h, &g_svc_status);
        SetEvent(g_svc_stop_event);
        return;
    default: break;
    }
}

static VOID WINAPI svc_main(DWORD /*argc*/, LPWSTR* /*argv*/)
{
    g_svc_status_h = RegisterServiceCtrlHandlerW(KEENETIC_SVC_NAME, svc_ctrl_handler);
    if (!g_svc_status_h) return;

    g_svc_status.dwServiceType    = SERVICE_WIN32_OWN_PROCESS;
    g_svc_status.dwCurrentState   = SERVICE_START_PENDING;
    g_svc_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    SetServiceStatus(g_svc_status_h, &g_svc_status);

    g_svc_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_svc_stop_event) {
        g_svc_status.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_svc_status_h, &g_svc_status);
        return;
    }

    svc_log("=== KeeneticVpnService starting ===");
    g_svc_status.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_svc_status_h, &g_svc_status);

    pipe_server_run(g_svc_stop_event);

    svc_log("=== KeeneticVpnService stopped ===");
    g_svc_status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_svc_status_h, &g_svc_status);
}

int svc_run_service(void)
{
    SERVICE_TABLE_ENTRYW table[] = {
        { (LPWSTR)KEENETIC_SVC_NAME, (LPSERVICE_MAIN_FUNCTIONW)svc_main },
        { nullptr, nullptr }
    };
    if (!StartServiceCtrlDispatcherW(table)) {
        DWORD e = GetLastError();
        svc_log("StartServiceCtrlDispatcher failed: %lu", e);
        return 1;
    }
    return 0;
}

int svc_install(const wchar_t* binPath)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) return 2;

    SC_HANDLE svc = CreateServiceW(scm, KEENETIC_SVC_NAME, KEENETIC_SVC_DISPLAY,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        binPath,
        nullptr, nullptr, nullptr,
        L"NT AUTHORITY\\LocalSystem", /* user */
        nullptr /* password */);
    if (!svc) {
        DWORD e = GetLastError();
        if (e == ERROR_SERVICE_EXISTS) {
            CloseServiceHandle(scm);
            return 0;
        }
        CloseServiceHandle(scm);
        return 3;
    }

    /* Description. */
    SERVICE_DESCRIPTIONW desc;
    desc.lpDescription = (LPWSTR)L"Owns the WinTun adapter for Keenetic anti-DPI VPN. "
                                  L"Installed by Keenetic-VPN-Setup.msi; do not delete.";
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

int svc_uninstall(void)
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) return 2;
    SC_HANDLE svc = OpenServiceW(scm, KEENETIC_SVC_NAME, SERVICE_ALL_ACCESS);
    if (!svc) { CloseServiceHandle(scm); return 0; }

    SERVICE_STATUS s;
    ControlService(svc, SERVICE_CONTROL_STOP, &s);
    DeleteService(svc);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

int wmain(int argc, wchar_t* argv[])
{
    if (argc >= 2) {
        if (!wcscmp(argv[1], L"install")) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            int rc = svc_install(path);
            fprintf(stdout, "Install: %d\n", rc);
            return rc;
        }
        if (!wcscmp(argv[1], L"uninstall")) {
            int rc = svc_uninstall();
            fprintf(stdout, "Uninstall: %d\n", rc);
            return rc;
        }
        if (!wcscmp(argv[1], L"run-foreground")) {
            /* dev mode: skip SCM, run pipe loop directly. */
            HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            pipe_server_run(stop);
            CloseHandle(stop);
            return 0;
        }
    }
    return svc_run_service();
}
