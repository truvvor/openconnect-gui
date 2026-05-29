/*
 * openconnect-gui-service — Windows SCM integration.
 *
 * --install / --uninstall manage service registration (run once, elevated, by
 * the installer). runDispatch() hands control to the SCM and runs the Qt event
 * loop inside ServiceMain; setup() wires the PipeServer onto that loop. The same
 * setup() is reused by --console mode for local debugging without registration.
 *
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#pragma once

#include <functional>

class QCoreApplication;

namespace winservice {

constexpr const wchar_t* kServiceName = L"OpenConnectGuiService";
constexpr const wchar_t* kDisplayName = L"OpenConnect-GUI VPN Service";
constexpr const wchar_t* kDescription =
    L"Runs the OpenConnect VPN tunnel with system privileges so the OpenConnect-GUI "
    L"client can connect without per-launch administrator elevation.";

/* Called inside the running event loop to attach the IPC server. */
using SetupFn = std::function<void(QCoreApplication&)>;

int install();                                   // 0 = ok; requires admin
int uninstall();                                 // 0 = ok; requires admin
int runDispatch(int argc, char** argv, SetupFn setup);   // SCM service entry (--run)
int runConsole(int argc, char** argv, SetupFn setup);    // --console (no SCM)

} // namespace winservice
