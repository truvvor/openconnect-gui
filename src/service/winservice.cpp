/*
 * openconnect-gui-service — Windows SCM integration implementation.
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#include "winservice.h"
#include "servicelog.h"

#include <QCoreApplication>
#include <QString>

#ifdef _WIN32
#include <windows.h>
#endif

namespace winservice {
namespace {

#ifdef _WIN32
SERVICE_STATUS g_status{};
SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
#endif
SetupFn g_setup;
QCoreApplication* g_app = nullptr;
int g_argc = 0;
char** g_argv = nullptr;

#ifdef _WIN32
void reportStatus(DWORD state, DWORD waitHint = 0)
{
    static DWORD checkpoint = 1;
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = NO_ERROR;
    g_status.dwWaitHint = waitHint;
    g_status.dwControlsAccepted =
        (state == SERVICE_START_PENDING) ? 0
                                         : (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
    g_status.dwCheckPoint =
        (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0 : checkpoint++;
    if (g_statusHandle)
        SetServiceStatus(g_statusHandle, &g_status);
}

DWORD WINAPI controlHandler(DWORD ctrl, DWORD, LPVOID, LPVOID)
{
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        reportStatus(SERVICE_STOP_PENDING, 5000);
        if (g_app)
            QMetaObject::invokeMethod(g_app, "quit", Qt::QueuedConnection);
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI serviceMain(DWORD, LPWSTR*)
{
    g_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, controlHandler, nullptr);
    if (!g_statusHandle)
        return;
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    reportStatus(SERVICE_START_PENDING, 5000);

    svc::log::init(false);
    QCoreApplication app(g_argc, g_argv);
    g_app = &app;
    if (g_setup)
        g_setup(app);

    reportStatus(SERVICE_RUNNING);
    svc::log::info(QStringLiteral("service running"));
    const int rc = app.exec();
    svc::log::info(QStringLiteral("service stopping (rc=%1)").arg(rc));

    g_app = nullptr;
    reportStatus(SERVICE_STOPPED);
}
#endif // _WIN32

} // namespace

#ifdef _WIN32

int install()
{
    wchar_t modPath[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, modPath, MAX_PATH))
        return 1;
    const QString bin = QStringLiteral("\"%1\" --run")
                            .arg(QString::fromWCharArray(modPath));

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        svc::log::error(QStringLiteral("OpenSCManager failed: %1").arg(GetLastError()));
        return 1;
    }

    SC_HANDLE svc = CreateServiceW(
        scm, kServiceName, kDisplayName, SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        reinterpret_cast<LPCWSTR>(bin.utf16()),
        nullptr, nullptr, nullptr, nullptr /*LocalSystem*/, nullptr);

    if (!svc) {
        const DWORD e = GetLastError();
        CloseServiceHandle(scm);
        if (e == ERROR_SERVICE_EXISTS) {
            svc::log::info(QStringLiteral("service already installed"));
            return 0;
        }
        svc::log::error(QStringLiteral("CreateService failed: %1").arg(e));
        return 1;
    }

    SERVICE_DESCRIPTIONW desc{ const_cast<LPWSTR>(kDescription) };
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    // Restart on failure: after 5s, twice, reset the count daily.
    SC_ACTION actions[2] = { { SC_ACTION_RESTART, 5000 }, { SC_ACTION_RESTART, 5000 } };
    SERVICE_FAILURE_ACTIONSW fa{};
    fa.dwResetPeriod = 86400;
    fa.cActions = 2;
    fa.lpsaActions = actions;
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, &fa);

    StartServiceW(svc, 0, nullptr); // best effort; SCM auto-starts on next boot anyway
    svc::log::info(QStringLiteral("service installed"));
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return 0;
}

int uninstall()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
        return 1;
    SC_HANDLE svc = OpenServiceW(scm, kServiceName,
                                 SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (!svc) {
        CloseServiceHandle(scm);
        return (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) ? 0 : 1;
    }
    SERVICE_STATUS st{};
    ControlService(svc, SERVICE_CONTROL_STOP, &st); // best effort
    const bool ok = DeleteService(svc);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    svc::log::info(QStringLiteral("service uninstalled (%1)").arg(ok ? "ok" : "err"));
    return ok ? 0 : 1;
}

int runDispatch(int argc, char** argv, SetupFn setup)
{
    g_argc = argc;
    g_argv = argv;
    g_setup = std::move(setup);
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(kServiceName), serviceMain },
        { nullptr, nullptr },
    };
    if (!StartServiceCtrlDispatcherW(table)) {
        svc::log::error(QStringLiteral("StartServiceCtrlDispatcher failed: %1 "
                                       "(run with --console for foreground)")
                            .arg(GetLastError()));
        return 1;
    }
    return 0;
}

static BOOL WINAPI consoleCtrl(DWORD)
{
    if (g_app)
        QMetaObject::invokeMethod(g_app, "quit", Qt::QueuedConnection);
    return TRUE;
}

int runConsole(int argc, char** argv, SetupFn setup)
{
    svc::log::init(true);
    QCoreApplication app(argc, argv);
    g_app = &app;
    SetConsoleCtrlHandler(consoleCtrl, TRUE);
    if (setup)
        setup(app);
    svc::log::info(QStringLiteral("running in console mode; Ctrl+C to stop"));
    const int rc = app.exec();
    g_app = nullptr;
    return rc;
}

#else  // !_WIN32 — service target is Windows-only; provide stubs.
int install() { return -1; }
int uninstall() { return -1; }
int runDispatch(int, char**, SetupFn) { return -1; }
int runConsole(int, char**, SetupFn) { return -1; }
#endif

} // namespace winservice
