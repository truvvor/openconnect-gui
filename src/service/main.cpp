/*
 * openconnect-gui-service — entry point.
 *
 *   --install     register the service with the SCM (run elevated, by installer)
 *   --uninstall   stop + deregister
 *   --console     run in the foreground (local debugging; Ctrl+C to stop)
 *   --run         (default) SCM service entry
 *
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#include "pipeserver.h"
#include "servicelog.h"
#include "winservice.h"

#include <QCoreApplication>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#endif
extern "C" {
#include <openconnect.h>
}

static void setupServer(QCoreApplication& app)
{
    auto* server = new PipeServer(&app);
    if (!server->start())
        svc::log::error(QStringLiteral("IPC pipe server failed to start"));
}

int main(int argc, char** argv)
{
    QCoreApplication::setApplicationName(QStringLiteral("openconnect-gui-service"));
    QCoreApplication::setOrganizationName(QStringLiteral("OpenConnect-GUI Team"));

    /* libopenconnect's command pipe is an emulated socketpair that needs Winsock
     * up; QLocalServer (named pipes) never starts it. Also init gnutls/SSL. */
#ifdef _WIN32
    { WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa); }
#endif
    openconnect_init_ssl();

    const char* mode = (argc > 1) ? argv[1] : "--run";

    if (std::strcmp(mode, "--install") == 0) {
        svc::log::init(true);
        return winservice::install();
    }
    if (std::strcmp(mode, "--uninstall") == 0) {
        svc::log::init(true);
        return winservice::uninstall();
    }
    if (std::strcmp(mode, "--console") == 0)
        return winservice::runConsole(argc, argv, setupServer);

    return winservice::runDispatch(argc, argv, setupServer);
}
