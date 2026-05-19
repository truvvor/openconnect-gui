/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2016 by Lubomír Carik <Lubomir.Carik@gmail.com>
 * Copyright (C) 2026 Keenetic anti-DPI VPN client (fork)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.  See LICENSE.txt for details.
 */

#include "common.h"
#include "config.h"
#include "dialog/MyInputDialog.h"
#include "dialog/mainwindow.h"
#include "openconnect-gui.h"

#include "FileLogger.h"
#include "logger.h"

extern "C" {
#include <openconnect.h>
}

#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>
#include <QtSingleApplication>

#include <csignal>
#include <cstdio>

/*
 * Keenetic anti-DPI fork: no per-user certificate / PKCS#11 / smart-card
 * authentication. Authentication is username + password + camouflage-secret
 * via our patched ocserv. macOS support is dropped (Windows-only).
 */

int main(int argc, char* argv[])
{
    qputenv("LOG2FILE", "1");

    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    qRegisterMetaType<Logger::Message>();

#ifdef PROJ_INI_SETTINGS
    QSettings::setDefaultFormat(QSettings::IniFormat);
#endif

    QCoreApplication::setApplicationName(appDescription);
    QCoreApplication::setApplicationVersion(appVersion);
    QCoreApplication::setOrganizationName(appOrganizationName);
    QCoreApplication::setOrganizationDomain(appOrganizationDomain);

    QtSingleApplication app(argc, argv);
    if (app.isRunning()) {
        QSettings settings;
        if (settings.value(QLatin1Literal("Settings/singleInstanceMode"), true).toBool()) {
            app.sendMessage("Wake up!");
            return 0;
        }
    }
    app.setApplicationDisplayName(appDescriptionLong);
    app.setQuitOnLastWindowClosed(false);

    auto fileLog = std::make_unique<FileLogger>();
    Logger::instance().addMessage(QString("%1 (%2) logging started...")
        .arg(app.applicationDisplayName())
        .arg(app.applicationVersion()));

    /* libopenconnect (Keenetic-camouflage build) global init. */
    openconnect_init_ssl();
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr(
        "Keenetic anti-DPI VPN client. "
        "Connects to a patched ocserv server (camouflage Level 2: HMAC-SHA256 "
        "CSTP magic, X-S-*/X-D-* header rewriting, /api/v1/session tunnel URL, "
        "TLS ClientHello scatter)."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({ { "s", "server" },
        QObject::tr("auto-connect to existing profile <name>"),
        QObject::tr("name") });
    parser.process(app);

    const QString profileName{ parser.value(QLatin1String("server")) };
    MainWindow mainWindow(nullptr, profileName);
    app.setActivationWindow(&mainWindow);

    mainWindow.show();
    QObject::connect(&app, &QtSingleApplication::messageReceived,
        [&mainWindow](const QString& message) {
            Logger::instance().addMessage(message);
        });
    return app.exec();
}
