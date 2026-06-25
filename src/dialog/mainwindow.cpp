/*
 * Copyright (C) 2014, 2015 Red Hat
 *
 * This file is part of openconnect-gui.
 *
 * openconnect-gui is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mainwindow.h"
#include "NewProfileDialog.h"
#include "config.h"
#include "editdialog.h"
#include "logdialog.h"
#include "openconnect-gui.h"
#include "server_storage.h"
#include "timestamp.h"
#include "ui_mainwindow.h"
#include "vpninfo.h"

#include "serviceclient.h"
#include "ipc/profile.h"
#include "MyCertMsgBox.h"
#include "MyInputDialog.h"
#include "MyMsgBox.h"
#include <QFile>
#include <QInputDialog>
#include <QJsonArray>
#include <QLineEdit>
#include <QMessageBox>

#include "logger.h"

extern "C" {
#include <gnutls/gnutls.h>
}
#include <spdlog/spdlog.h>

#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QEventTransition>
#include <QFileSelector>
#include <QFutureWatcher>
#include <QLineEdit>
#include <QMessageBox>
#include <QSettings>
#include <QSignalTransition>
#include <QStateMachine>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkProxyFactory>
#include <QtNetwork/QNetworkProxyQuery>

#include <cmath>
#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
#define pipe_write(x, y, z) send(x, y, z, 0)
#else
#define pipe_write(x, y, z) write(x, y, z)
#endif

MainWindow::MainWindow(QWidget* parent, const QString profileName)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    connect(ui->viewLogButton, &QPushButton::clicked,
        this, &MainWindow::createLogDialog);

    timer = new QTimer(this);
    blink_timer = new QTimer(this);
    this->cmd_fd = INVALID_SOCKET;

    /* privilege separation: all VPN work is delegated to the LocalSystem
     * service over the named pipe; this GUI process needs no admin rights. */
    m_svc = new ServiceClient(this);
    connect(m_svc, &ServiceClient::stateChanged, this, &MainWindow::onSvcState);
    connect(m_svc, &ServiceClient::logReceived, this, &MainWindow::onSvcLog);
    connect(m_svc, &ServiceClient::statsReceived, this, &MainWindow::onSvcStats);
    connect(m_svc, &ServiceClient::ipInfoReceived, this, &MainWindow::onSvcIpInfo);
    connect(m_svc, &ServiceClient::promptReceived, this, &MainWindow::onSvcPrompt);
    connect(m_svc, &ServiceClient::persistReceived, this, &MainWindow::onSvcPersist);
    connect(m_svc, &ServiceClient::serviceError, this, &MainWindow::onSvcError);
    connect(m_svc, &ServiceClient::serviceUnavailable, this, [](const QString& why) {
        Logger::instance().addMessage(QObject::tr("VPN service unavailable: ") + why);
    });

    connect(ui->actionQuit, &QAction::triggered,
        [=]() {
            if (m_disconnectAction->isEnabled()) {
                connect(this, &MainWindow::readyToShutdown,
                    qApp, &QApplication::quit);
                on_disconnectClicked();
            } else {
                qApp->quit();
            }
        });

    connect(blink_timer, &QTimer::timeout,
        this, &MainWindow::blink_ui,
        Qt::QueuedConnection);
    connect(timer, &QTimer::timeout,
        this, &MainWindow::request_update_stats,
        Qt::QueuedConnection);
    connect(this, &MainWindow::vpn_status_changed_sig,
        this, &MainWindow::changeStatus,
        Qt::QueuedConnection);
    connect(ui->connectionButton, &QPushButton::clicked,
        this, &MainWindow::on_connectClicked,
        Qt::QueuedConnection);
    connect(this, &MainWindow::stats_changed_sig,
        this, &MainWindow::statsChanged,
        Qt::QueuedConnection);

    ui->iconLabel->setPixmap(OFF_ICON);
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        createTrayIcon();

        connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, &MainWindow::iconActivated);

        QFileSelector selector;
        QIcon icon(selector.select(QStringLiteral(":/images/network-disconnected.png")));
        icon.setIsMask(true);
        m_trayIcon->setIcon(icon);
        m_trayIcon->show();
    } else {
        Logger::instance().addMessage(QLatin1String("System doesn't support tray icon"));
        m_trayIcon = nullptr;
    }

    // TODO: initial state machine
    QStateMachine* machine = new QStateMachine(this);
    QState* s1_noProfiles = new QState();
    s1_noProfiles->assignProperty(ui->connectionButton, "enabled", false);
    s1_noProfiles->assignProperty(ui->serverList, "enabled", false);
    s1_noProfiles->assignProperty(ui->actionEditSelectedProfile, "enabled", false);
    s1_noProfiles->assignProperty(ui->actionRemoveSelectedProfile, "enabled", false);

    s1_noProfiles->assignProperty(m_trayIconMenuConnections, "title", tr("(no servers to connect)"));
    s1_noProfiles->assignProperty(m_trayIconMenuConnections, "enabled", false);
    s1_noProfiles->assignProperty(m_disconnectAction, "enabled", false);
    machine->addState(s1_noProfiles);

    QState* s2_connectionReady = new QState();
    s2_connectionReady->assignProperty(ui->connectionButton, "enabled", true);
    s2_connectionReady->assignProperty(ui->serverList, "enabled", true);
    s2_connectionReady->assignProperty(ui->actionEditSelectedProfile, "enabled", true);
    s2_connectionReady->assignProperty(ui->actionRemoveSelectedProfile, "enabled", true);

    s2_connectionReady->assignProperty(m_trayIconMenuConnections, "title", tr("Connect to..."));
    s2_connectionReady->assignProperty(m_trayIconMenuConnections, "enabled", true);
    machine->addState(s2_connectionReady);

    class ServerListTransition : public QSignalTransition {
    public:
        ServerListTransition(QComboBox* cb, bool hasServers)
            : QSignalTransition(cb, SIGNAL(currentIndexChanged(int)))
            , hasServers(hasServers)
        {
        }

    protected:
        bool eventTest(QEvent* e)
        {
            if (!QSignalTransition::eventTest(e)) {
                return false;
            }
            QStateMachine::SignalEvent* se = static_cast<QStateMachine::SignalEvent*>(e);
            bool isEmpty = se->arguments().at(0).toInt() == -1;
            return (hasServers ? !isEmpty : isEmpty);
        }

    private:
        bool hasServers;
    };

    ServerListTransition* t1 = new ServerListTransition(ui->serverList, true);
    t1->setTargetState(s2_connectionReady);
    s1_noProfiles->addTransition(t1);

    ServerListTransition* t2 = new ServerListTransition(ui->serverList, false);
    t2->setTargetState(s1_noProfiles);
    s2_connectionReady->addTransition(t2);

    machine->setInitialState(s1_noProfiles);
    machine->start();
    connect(machine, &QStateMachine::started, [=]() {
        // LCA: find better way to load/fill combobox...
        this->reload_settings();

        if (!profileName.isEmpty()) {
            // TODO: better place when refactor SM...
            const int profileIndex = ui->serverList->findText(profileName);
            if (profileIndex != -1) {
                ui->serverList->setCurrentIndex(profileIndex);
                emit on_connectClicked();
                return;
            } else {
                QMessageBox::warning(this,
                    tr("Connection failed"),
                    tr("Selected VPN profile '<b>%1</b>' does not exists.").arg(profileName));
            }
        }

        QSettings settings;
        const int currentIndex = settings.value("Profiles/currentIndex", -1).toInt();
        if (currentIndex != -1 && currentIndex < ui->serverList->count()) {
            ui->serverList->setCurrentIndex(currentIndex);
        }
    });

    QMenu* serverProfilesMenu = new QMenu(this);
    serverProfilesMenu->addAction(ui->actionNewProfile);
    serverProfilesMenu->addAction(ui->actionNewProfileAdvanced);
    serverProfilesMenu->addAction(ui->actionEditSelectedProfile);
    serverProfilesMenu->addAction(ui->actionRemoveSelectedProfile);
    ui->serverListControl->setMenu(serverProfilesMenu);

    readSettings();
    // TODO: initial app window state machine
    m_appWindowStateMachine = new QStateMachine(this);

    QState* s111_normalWindow = new QState();
    m_appWindowStateMachine->addState(s111_normalWindow);
    s111_normalWindow->assignProperty(ui->actionRestore, "enabled", false);
    s111_normalWindow->assignProperty(ui->actionMinimize, "enabled", true);

    QState* s112_minimizedWindow = new QState();
    m_appWindowStateMachine->addState(s112_minimizedWindow);
    connect(s112_minimizedWindow, &QState::entered, [=]() {
        showMinimized();
        if (ui->actionMinimizeToTheNotificationArea->isChecked()) {
            QTimer::singleShot(10, this, SLOT(hide()));
        }
    });
    connect(s112_minimizedWindow, &QState::exited, [=]() {
        this->showNormal();
        if (ui->actionMinimizeToTheNotificationArea->isChecked()) {
            show();
            raise();
            activateWindow();
        }
    });
    s112_minimizedWindow->assignProperty(ui->actionRestore, "enabled", true);
    s112_minimizedWindow->assignProperty(ui->actionMinimize, "enabled", false);

    if (ui->actionStartMinimized->isChecked()) {
        m_appWindowStateMachine->setInitialState(s112_minimizedWindow);
    } else {
        m_appWindowStateMachine->setInitialState(s111_normalWindow);
    }

    // TODO: move outside...
    class MinimizeEventTransition : public QEventTransition {
    public:
        MinimizeEventTransition(QMainWindow* mw, Qt::WindowState state)
            : QEventTransition(mw, QEvent::WindowStateChange)
            , m_mw(mw)
            , m_state(state)
        {
        }

    protected:
        bool eventTest(QEvent* e) override
        {
            if (!QEventTransition::eventTest(e)) {
                return false;
            }
            QStateMachine::WrappedEvent* we = static_cast<QStateMachine::WrappedEvent*>(e);
            if (we->event()->type() == QEvent::WindowStateChange) {
                return (m_mw->windowState() == m_state);
            }
            return false;
        }

    private:
        QMainWindow* m_mw;
        Qt::WindowState m_state;
    };
    // TODO: move outside...
    class RestoreEventTransition : public QEventTransition {
    public:
        RestoreEventTransition(QMainWindow* mw, Qt::WindowState state)
            : QEventTransition(mw, QEvent::WindowStateChange)
            , m_mw(mw)
            , m_state(state)
        {
        }

    protected:
        bool eventTest(QEvent* e) override
        {
            if (!QEventTransition::eventTest(e)) {
                return false;
            }
            QStateMachine::WrappedEvent* we = static_cast<QStateMachine::WrappedEvent*>(e);
            if (we->event()->type() == QEvent::WindowStateChange) {
                return (m_mw->windowState() == m_state);
            }
            return false;
        }

    private:
        QMainWindow* m_mw;
        Qt::WindowState m_state;
    };

    MinimizeEventTransition* minimizeEvent = new MinimizeEventTransition(this, Qt::WindowMinimized);
    minimizeEvent->setTargetState(s112_minimizedWindow);
    s111_normalWindow->addTransition(minimizeEvent);

    RestoreEventTransition* restoreEvent = new RestoreEventTransition(this, Qt::WindowNoState);
    restoreEvent->setTargetState(s111_normalWindow);
    s112_minimizedWindow->addTransition(restoreEvent);

    m_appWindowStateMachine->start();
}

static void term_thread(MainWindow* m, SOCKET* fd)
{
    char cmd = OC_CMD_CANCEL;

    if (*fd != INVALID_SOCKET) {
        m->vpn_status_changed(STATUS_DISCONNECTING);
        int ret = pipe_write(*fd, &cmd, 1);
        if (ret < 0) {
            Logger::instance().addMessage(QObject::tr("term_thread: IPC error: ") + QString::number(net_errno));
        }
        *fd = INVALID_SOCKET;
        ms_sleep(200);
    } else {
        m->vpn_status_changed(STATUS_DISCONNECTED);
    }
}

MainWindow::~MainWindow()
{
    if (this->timer->isActive()) {
        timer->stop();
    }
    if (m_svc)
        m_svc->disconnectVpn();

    writeSettings();

    delete ui;
    delete timer;
    delete blink_timer;
}

void MainWindow::vpn_status_changed(int connected)
{
    emit vpn_status_changed_sig(connected);
}

void MainWindow::vpn_status_changed(int connected, QString& dns, QString& ip, QString& ip6, QString& cstp_cipher, QString& dtls_cipher)
{
    this->dns = dns;
    this->ip = ip;
    this->ip6 = ip6;
    this->dtls_cipher = dtls_cipher;
    this->cstp_cipher = cstp_cipher;

    emit vpn_status_changed_sig(connected);
}

QString MainWindow::normalize_byte_size(uint64_t bytes)
{
    const unsigned unit = 1024; // TODO: add support for SI units? (optional)
    if (bytes < unit) {
        return QString("%1 B").arg(QString::number(bytes));
    }
    const int exp = static_cast<int>(std::log(bytes) / std::log(unit));
    static const char suffixChar[] = "KMGTPE";
    return QString("%1 %2B").arg(QString::number(bytes / std::pow(unit, exp), 'f', 3)).arg(suffixChar[exp - 1]);
}

void MainWindow::statsChanged(QString tx, QString rx, QString dtls)
{
    ui->downloadLabel->setText(rx);
    ui->uploadLabel->setText(tx);
    ui->cipherDTLSLabel->setText(dtls);
}

void MainWindow::updateStats(const struct oc_stats* stats, QString dtls)
{
    emit stats_changed_sig(
        normalize_byte_size(stats->tx_bytes),
        normalize_byte_size(stats->rx_bytes),
        dtls);
}

#define PREFIX "server:" // LCA: remot this...
void MainWindow::reload_settings()
{
    ui->serverList->clear();
    m_trayIconMenuConnections->clear();

    QSettings settings;
    for (const auto& key : settings.allKeys()) {
        if (key.startsWith(PREFIX) && key.endsWith("/server")) {
            QString str{ key };
            str.remove(0, sizeof(PREFIX) - 1); /* remove prefix */
            str.remove(str.size() - 7, 7); /* remove /server suffix */
            ui->serverList->addItem(str);

            QAction* act = m_trayIconMenuConnections->addAction(str);
            connect(act, &QAction::triggered, [act, this]() {
                int idx = ui->serverList->findText(act->text());
                if (idx != -1) {
                    ui->serverList->setCurrentIndex(idx);
                    on_connectClicked();
                }
            });
        }
    }
}

void MainWindow::blink_ui()
{
    static unsigned t = 1;

    if (t % 2 == 0) {
        ui->iconLabel->setPixmap(CONNECTING_ICON);
    } else {
        ui->iconLabel->setPixmap(CONNECTING_ICON2);
    }
    ++t;
}

void MainWindow::changeStatus(int val)
{
    if (val == STATUS_CONNECTED) {

        blink_timer->stop();

        ui->serverList->setEnabled(false);

        m_trayIconMenuConnections->setEnabled(false);
        m_disconnectAction->setEnabled(true);

        ui->iconLabel->setPixmap(ON_ICON);
        ui->connectionButton->setIcon(QIcon(":/images/process-stop.png"));
        ui->connectionButton->setText(tr("Disconnect"));

        QFileSelector selector;
        QIcon icon(selector.select(QStringLiteral(":/images/network-connected.png")));
        icon.setIsMask(true);
        m_trayIcon->setIcon(icon);

        this->ui->ipV4Label->setText(ip);
        this->ui->ipV6Label->setText(ip6);
        this->ui->dnsLabel->setText(dns);
        this->ui->cipherCSTPLabel->setText(cstp_cipher);
        this->ui->cipherDTLSLabel->setText(dtls_cipher);

        timer->start(UPDATE_TIMER);

        if (this->minimize_on_connect) {
            if (m_trayIcon) {
                hide();
                m_trayIcon->showMessage(QLatin1String("Connected"), QLatin1String("You were connected to ") + ui->serverList->currentText(),
                    QSystemTrayIcon::Information,
                    10000);
            } else {
                this->setWindowState(Qt::WindowMinimized);
            }
        }
    } else if (val == STATUS_CONNECTING) {

        if (m_trayIcon) {
            QFileSelector selector;
            QIcon icon(selector.select(QStringLiteral(":/images/network-disconnected.png")));
            icon.setIsMask(true);
            m_trayIcon->setIcon(icon);
        }

        ui->serverList->setEnabled(false);

        m_trayIconMenuConnections->setEnabled(false);
        m_disconnectAction->setEnabled(true);

        ui->iconLabel->setPixmap(CONNECTING_ICON);
        ui->connectionButton->setIcon(QIcon(":/images/process-stop.png"));
        ui->connectionButton->setText(tr("Cancel"));
        blink_timer->start(1500);

        disconnect(ui->connectionButton, &QPushButton::clicked,
            this, &MainWindow::on_connectClicked);
        connect(ui->connectionButton, &QPushButton::clicked,
            this, &MainWindow::on_disconnectClicked,
            Qt::QueuedConnection);
    } else if (val == STATUS_DISCONNECTED) {
        blink_timer->stop();
        if (this->timer->isActive()) {
            timer->stop();
        }
        cmd_fd = INVALID_SOCKET;

        ui->ipV4Label->clear();
        ui->ipV6Label->clear();
        ui->dnsLabel->clear();
        ui->uploadLabel->clear();
        ui->downloadLabel->clear();
        ui->cipherCSTPLabel->clear();
        ui->cipherDTLSLabel->clear();
        Logger::instance().addMessage(QObject::tr("Disconnected"));

        ui->serverList->setEnabled(true);

        m_trayIconMenuConnections->setEnabled(true);
        m_disconnectAction->setEnabled(false);

        ui->iconLabel->setPixmap(OFF_ICON);
        ui->connectionButton->setEnabled(true);
        ui->connectionButton->setIcon(QIcon(":/images/network-wired.png"));
        ui->connectionButton->setText(tr("Connect"));

        if (m_trayIcon) {
            QFileSelector selector;
            QIcon icon(selector.select(QStringLiteral(":/images/network-disconnected.png")));
            icon.setIsMask(true);
            m_trayIcon->setIcon(icon);

            if (this->isHidden() == true)
                m_trayIcon->showMessage(QLatin1String("Disconnected"), QLatin1String("You were disconnected from the VPN"),
                    QSystemTrayIcon::Warning,
                    10000);
        }
        disconnect(ui->connectionButton, &QPushButton::clicked,
            this, &MainWindow::on_disconnectClicked);
        connect(ui->connectionButton, &QPushButton::clicked,
            this, &MainWindow::on_connectClicked,
            Qt::QueuedConnection);

        emit readyToShutdown();
    } else if (val == STATUS_DISCONNECTING) {
        ui->iconLabel->setPixmap(CONNECTING_ICON);
        ui->connectionButton->setIcon(QIcon(":/images/process-stop.png"));
        ui->connectionButton->setEnabled(false);
        blink_timer->start(1500);
    } else {
        qDebug() << "TODO: was is das?";
    }
}

static void main_loop(VpnInfo* vpninfo, MainWindow* m)
{
    m->vpn_status_changed(STATUS_CONNECTING);

    bool pass_was_empty;
    pass_was_empty = vpninfo->ss->get_password().isEmpty();

    QString ip, ip6, dns, cstp, dtls;

    int ret = 0;
    bool retry = false;
    int retries = 2;
    do {
        retry = false;
        ret = vpninfo->connect();
        if (ret != 0) {
            if (retries-- <= 0)
                goto fail;

            QString oldpass, oldgroup;
            bool reset_password = false;
            if (pass_was_empty != true) {
                /* authentication failed in batch mode? switch to non
                 * batch and retry */
                oldpass = vpninfo->ss->get_password();
                oldgroup = vpninfo->ss->get_groupname();
                vpninfo->ss->clear_password();
                vpninfo->ss->clear_groupname();
                retry = true;
                reset_password = true;
                Logger::instance().addMessage(QObject::tr("Authentication failed in batch mode, retrying with batch mode disabled"));
                vpninfo->reset_vpn();
                continue;
            }

            /* if we didn't manage to connect on a retry, the failure reason
             * may not have been a changed password, reset it */
            if (reset_password == true) {
                vpninfo->ss->set_password(oldpass);
                vpninfo->ss->set_groupname(oldgroup);
            }

            Logger::instance().addMessage(vpninfo->last_err);
            goto fail;
        }

    } while (retry == true);

    ret = vpninfo->dtls_connect();
    if (ret != 0) {
        Logger::instance().addMessage(vpninfo->last_err);
    }

    vpninfo->get_info(dns, ip, ip6);
    vpninfo->get_cipher_info(cstp, dtls);
    m->vpn_status_changed(STATUS_CONNECTED, dns, ip, ip6, cstp, dtls);

    vpninfo->ss->save();
    vpninfo->mainloop();

fail: // LCA: drop this 'goto' and optimize values...
    m->vpn_status_changed(STATUS_DISCONNECTED);

    delete vpninfo;
}

void MainWindow::on_disconnectClicked()
{
    if (this->timer->isActive()) {
        this->timer->stop();
    }
    Logger::instance().addMessage(QObject::tr("Disconnecting..."));
    vpn_status_changed(STATUS_DISCONNECTING);
    m_svc->disconnectVpn();
}

void MainWindow::on_connectClicked()
{
    if (ui->serverList->currentText().isEmpty()) {
        QMessageBox::information(this,
            qApp->applicationName(),
            tr("You need to specify a gateway. e.g. vpn.example.com:443"));
        return;
    }

    QString nm = ui->serverList->currentText();
    m_connectingName = nm;

    StoredServer ss;
    ss.load(nm);

    auto readPem = [](const QString& path) -> QByteArray {
        if (path.isEmpty())
            return {};
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
    };

    oc::ipc::Profile p;
    p.name = nm;
    p.server = ss.get_servername();
    p.protocol = QString::fromLatin1(ss.get_protocol_name());
    if (p.protocol.isEmpty())
        p.protocol = QStringLiteral("anyconnect");
    p.camouflageSecret = ss.get_camouflage_secret();
    p.disableUdp = ss.get_disable_udp();
    p.noDefaultRoute = ss.get_no_default_route();
    p.autoAcceptBanner = ss.get_auto_accept_banner();
    p.reconnectTimeout = ss.get_reconnect_timeout();
    p.dtlsReconnectTimeout = ss.get_dtls_reconnect_timeout();
    p.reportedOs = QStringLiteral("win");
    p.username = ss.get_username();
    m_savedPassword = ss.get_password();
    m_forcePasswordPrompt = ss.get_force_password_prompt();
    // "Save password but still ask": when forced, send no password so the engine
    // raises an auth prompt; the GUI pre-fills it with the saved value.
    p.password = m_forcePasswordPrompt ? QString() : m_savedPassword;
    p.groupname = ss.get_groupname();
    p.tokenType = ss.get_token_type();
    p.tokenSecret = ss.get_token_str();
    p.clientCertPem = readPem(ss.get_cert_file());
    p.clientKeyPem = readPem(ss.get_key_file());
    p.caCertPem = readPem(ss.get_ca_cert_file());

    /* Remembered server certificate -> profile.trust, so the service does not
     * raise a cert prompt on every connect once the user has trusted it. */
    {
        QSettings st;
        const QString base = QStringLiteral("server:") + nm + "/";
        const QString pinHash = st.value(base + "pinned-cert-hash").toString();
        if (!pinHash.isEmpty()) {
            oc::ipc::TrustPin pin;
            pin.hash = pinHash;
            pin.derBase64 = st.value(base + "pinned-cert-der").toString();
            p.trust.append(pin);
        }
    }

    this->minimize_on_connect = ss.get_minimize();

    Logger::instance().addMessage(tr("Connecting via service to ") + p.server);
    vpn_status_changed(STATUS_CONNECTING);
    m_svc->connectVpn(p);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_trayIcon && m_trayIcon->isVisible() && ui->actionMinimizeTheApplicationInsteadOfClosing->isChecked()) {
        this->showMinimized();
        event->ignore();
    } else {
        event->accept();

        if (m_disconnectAction->isEnabled()) {
            connect(this, &MainWindow::readyToShutdown,
                qApp, &QApplication::quit);
            on_disconnectClicked();
        } else {
            qApp->quit();
        }
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::request_update_stats()
{
    if (m_svc)
        m_svc->requestStatus();
}

void MainWindow::readSettings()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    resize(settings.value("size").toSize());
    if (settings.contains("pos")) {
        move(settings.value("pos").toPoint());
    }
    settings.endGroup();

    settings.beginGroup("Settings");
    ui->actionMinimizeToTheNotificationArea->setChecked(settings.value("minimizeToTheNotificationArea", true).toBool());
    ui->actionMinimizeTheApplicationInsteadOfClosing->setChecked(settings.value("minimizeTheApplicationInsteadOfClosing", true).toBool());
    ui->actionStartMinimized->setChecked(settings.value("startMinimized", false).toBool());
    ui->actionSingleInstanceMode->setChecked(settings.value("singleInstanceMode", true).toBool());
    connect(ui->actionSingleInstanceMode, &QAction::toggled, [](bool checked) {
        QSettings settings;
        settings.setValue("Settings/singleInstanceMode", checked);
    });
    settings.endGroup();
}

void MainWindow::writeSettings()
{
    QSettings settings;
    settings.beginGroup("MainWindow");
    settings.setValue("size", size());
    settings.setValue("pos", pos());
    settings.endGroup();

    settings.beginGroup("Settings");
    settings.setValue("minimizeToTheNotificationArea", ui->actionMinimizeToTheNotificationArea->isChecked());
    settings.setValue("minimizeTheApplicationInsteadOfClosing", ui->actionMinimizeTheApplicationInsteadOfClosing->isChecked());
    settings.setValue("startMinimized", ui->actionStartMinimized->isChecked());
    settings.setValue("singleInstanceMode", ui->actionSingleInstanceMode->isChecked());
    settings.endGroup();

    settings.setValue("Profiles/currentIndex", ui->serverList->currentIndex());
}

void MainWindow::createLogDialog()
{
    auto dialog{ new LogDialog() };

    disconnect(ui->viewLogButton, &QPushButton::clicked,
        this, &MainWindow::createLogDialog);

    connect(ui->viewLogButton, &QPushButton::clicked,
        dialog, &QDialog::show);
    connect(ui->viewLogButton, &QPushButton::clicked,
        dialog, &QDialog::raise);
    connect(ui->viewLogButton, &QPushButton::clicked,
        dialog, &QDialog::activateWindow);

    connect(dialog, &QDialog::finished,
        [this]() {
            connect(ui->viewLogButton, &QPushButton::clicked,
                this, &MainWindow::createLogDialog);
        });
    connect(dialog, &QDialog::finished,
        dialog, &QDialog::deleteLater);

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::createTrayIcon()
{
    m_trayIconMenu = new QMenu(this);

    m_trayIconMenuConnections = new QMenu(this);
    m_trayIconMenu->addMenu(m_trayIconMenuConnections);
    m_disconnectAction = new QAction(tr("Disconnect"), this);
    m_trayIconMenu->addAction(m_disconnectAction);
    connect(m_disconnectAction, &QAction::triggered,
        this, &MainWindow::on_disconnectClicked);

    m_trayIconMenu->addSeparator();
    m_trayIconMenu->addAction(ui->actionLogWindow);
    m_trayIconMenu->addSeparator();
    m_trayIconMenu->addAction(ui->actionMinimize);
    m_trayIconMenu->addAction(ui->actionRestore);
    m_trayIconMenu->addSeparator();
    m_trayIconMenu->addAction(ui->actionQuit);

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setContextMenu(m_trayIconMenu);
}

void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason) {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::DoubleClick:
    case QSystemTrayIcon::MiddleClick:
#ifdef Q_OS_WIN
        if (isMinimized()) {
            showNormal();
        } else {
            showMinimized();
        }
#endif
        break;
    default:
        break;
    }
}

void MainWindow::on_actionNewProfile_triggered()
{
    NewProfileDialog dialog(this);
    connect(&dialog, &NewProfileDialog::connect,
        this, &MainWindow::on_connectClicked,
        Qt::QueuedConnection);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    reload_settings();
    ui->serverList->setCurrentText(dialog.getNewProfileName());
}

void MainWindow::on_actionNewProfileAdvanced_triggered()
{
    // TODO: the new profile has no name yet...
    EditDialog dialog("", this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    reload_settings();
    ui->serverList->setCurrentText(dialog.getEditedProfileName());
}

void MainWindow::on_actionEditSelectedProfile_triggered()
{
    EditDialog dialog(ui->serverList->currentText(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    int idx = ui->serverList->currentIndex();
    reload_settings();
    // TODO: may be signal/slot?
    if (idx < ui->serverList->maxVisibleItems() && idx >= 0) {
        ui->serverList->setCurrentIndex(idx);
    } else if (ui->serverList->maxVisibleItems() == 0) {
        ui->serverList->setCurrentIndex(0);
    }
    // LCA: else ???
}

#define PREFIX "server:" // LCA: remote this...
void MainWindow::on_actionRemoveSelectedProfile_triggered()
{
    QMessageBox mbox;
    mbox.setText(tr("Are you sure you want to remove '%1' host?").arg(ui->serverList->currentText()));
    mbox.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
    mbox.setDefaultButton(QMessageBox::Cancel);
    mbox.setButtonText(QMessageBox::Ok, tr("Remove"));
    if (mbox.exec() == QMessageBox::Ok) {
        QSettings settings;
        QString prefix = PREFIX;
        for (const auto& key : settings.allKeys()) {
            //qDebug() << key << ":" << QString(prefix + ui->serverList->currentText());
            if (key.startsWith(prefix + ui->serverList->currentText() + "/")) {
                settings.remove(key);
            }
        }

        reload_settings(); // LCA: remove this feature...
    }
}

void MainWindow::on_actionAbout_triggered()
{
    QString txt = QLatin1String("<h2>") + QLatin1String(appDescriptionLong) + QLatin1String("</h2>");
    txt += tr("Version <i>%1</i> (%2 bit)").arg(appVersion).arg(QSysInfo::buildCpuArchitecture() == QLatin1String("i386") ? 32 : 64);
    txt += tr("<br><br>Build on ") + QLatin1String("<i>") + QLatin1String(appBuildOn) + QLatin1String("</i>");
    txt += tr("<br>Based on");
    txt += tr("<br>- <a href=\"https://www.infradead.org/openconnect\">OpenConnect</a> ") + QLatin1String(openconnect_get_version());
    txt += tr("<br>- <a href=\"https://www.gnutls.org\">GnuTLS</a> v") + QLatin1String(gnutls_check_version(nullptr));
    txt += tr("<br>- <a href=\"https://github.com/gabime/spdlog\">spdlog</a> v%1.%2.%3").arg(QString::number(SPDLOG_VER_MAJOR)).arg(QString::number(SPDLOG_VER_MINOR)).arg(QString::number(SPDLOG_VER_PATCH));
    txt += tr("<br>- <a href=\"https://www.qt.io\">Qt</a> v%1").arg(QT_VERSION_STR);
    txt += tr("<br><br>%1<br>").arg(appCopyright);
    txt += tr("<br><i>%1</i> comes with ABSOLUTELY NO WARRANTY. This is free software, "
              "and you are welcome to redistribute it under the conditions "
              "of the GNU General Public License version 2.")
               .arg(appDescriptionLong);

    QMessageBox::about(this, "", txt);
}

void MainWindow::on_actionAboutQt_triggered()
{
    qApp->aboutQt();
}

void MainWindow::on_actionWebSite_triggered()
{
    QDesktopServices::openUrl(QUrl("https://openconnect.github.io/openconnect-gui"));
}


/* ===================================================================== *
 * Privilege separation: events arriving from the unprivileged service   *
 * client. All run on the GUI thread, so dialogs can be shown directly.  *
 * ===================================================================== */

void MainWindow::onSvcState(const QString& state, const QString& detail)
{
    if (state == QLatin1String("connected")) {
        changeStatus(STATUS_CONNECTED);
    } else if (state == QLatin1String("disconnecting")) {
        changeStatus(STATUS_DISCONNECTING);
    } else if (state == QLatin1String("disconnected")) {
        if (!detail.isEmpty())
            Logger::instance().addMessage(detail);
        changeStatus(STATUS_DISCONNECTED);
    } else if (state == QLatin1String("error")) {
        Logger::instance().addMessage(tr("VPN error: ") + detail);
        changeStatus(STATUS_DISCONNECTED);
    } else {
        /* connecting / authenticating / obtaining-cookie / cstp / setup-tun / reconnecting */
        changeStatus(STATUS_CONNECTING);
    }
}

void MainWindow::onSvcLog(int level, const QString& msg)
{
    Q_UNUSED(level);
    Logger::instance().addMessage(msg);
}

void MainWindow::onSvcStats(double rx, double tx, const QString& cstp, const QString& dtls)
{
    ui->downloadLabel->setText(normalize_byte_size(static_cast<uint64_t>(rx)));
    ui->uploadLabel->setText(normalize_byte_size(static_cast<uint64_t>(tx)));
    this->cstp_cipher = cstp;
    this->dtls_cipher = dtls;
    ui->cipherCSTPLabel->setText(cstp);
    ui->cipherDTLSLabel->setText(dtls);
}

void MainWindow::onSvcIpInfo(const QString& addr, const QString& netmask,
                             const QString& addr6, const QString& dns)
{
    this->ip = addr;
    if (!netmask.isEmpty())
        this->ip += "/" + netmask;
    this->ip6 = addr6;
    this->dns = dns;
    ui->ipV4Label->setText(this->ip);
    ui->ipV6Label->setText(this->ip6);
    ui->dnsLabel->setText(this->dns);
}

void MainWindow::onSvcPrompt(const QString& kind, quint64 promptId, const QJsonObject& body)
{
    if (kind == QLatin1String("auth-form")) {
        const QJsonObject form = body.value("form").toObject();
        QJsonObject fields;

        const QJsonObject g = form.value("authgroup").toObject();
        if (!g.isEmpty()) {
            QStringList labels, names;
            for (const auto& v : g.value("choices").toArray()) {
                const QJsonObject o = v.toObject();
                names << o.value("name").toString();
                labels << o.value("label").toString();
            }
            bool ok = false;
            const int cur = qMax(0, names.indexOf(g.value("current").toString()));
            const QString glabel = g.value("label").toString();
            const QString chosen = QInputDialog::getItem(this, tr("VPN group"),
                glabel.isEmpty() ? tr("Select group:") : glabel, labels, cur, false, &ok);
            if (!ok) { m_svc->sendPromptResponse(promptId, false); return; }
            const int idx = labels.indexOf(chosen);
            fields.insert("__group__", names.value(idx < 0 ? 0 : idx));
        }

        for (const auto& v : form.value("opts").toArray()) {
            const QJsonObject o = v.toObject();
            const QString nm = o.value("name").toString();
            const QString label = o.value("label").toString();
            const QString type = o.value("type").toString();
            if (type == QLatin1String("select")) {
                QStringList labels, names;
                for (const auto& cv : o.value("choices").toArray()) {
                    const QJsonObject co = cv.toObject();
                    names << co.value("name").toString();
                    labels << co.value("label").toString();
                }
                bool ok = false;
                const QString chosen = QInputDialog::getItem(this, windowTitle(),
                    label.isEmpty() ? nm : label, labels, 0, false, &ok);
                if (!ok) { m_svc->sendPromptResponse(promptId, false); return; }
                const int idx = labels.indexOf(chosen);
                fields.insert(nm, names.value(idx < 0 ? 0 : idx));
            } else {
                const bool isPass = (type == QLatin1String("password"));
                bool ok = false;
                // Pre-fill the saved password ONLY for the "always ask" confirm
                // case. A re-prompt after a failed saved password (or a changed
                // username) must start EMPTY, otherwise the stale password is
                // shown and can be re-submitted unchanged.
                const QString prefill = (isPass && m_forcePasswordPrompt) ? m_savedPassword : QString();
                const QString text = QInputDialog::getText(this, windowTitle(),
                    label.isEmpty() ? nm : label,
                    isPass ? QLineEdit::Password : QLineEdit::Normal,
                    prefill, &ok);
                if (!ok) { m_svc->sendPromptResponse(promptId, false); return; }
                fields.insert(nm, text);
            }
        }
        m_svc->sendPromptResponse(promptId, true, QJsonObject{ { "fields", fields } });

    } else if (kind == QLatin1String("cert")) {
        const QString host = body.value("host").toString();
        const QString hash = body.value("hash").toString();
        const bool mismatch = body.value("change").toString() == QLatin1String("key-mismatch");
        QMessageBox box(this);
        box.setIcon(mismatch ? QMessageBox::Warning : QMessageBox::Question);
        box.setWindowTitle(tr("Verify server certificate"));
        box.setText(mismatch
            ? tr("This peer is known but presents a DIFFERENT key. You may be under attack. Proceed anyway?")
            : tr("First connection to this peer. Trust this certificate and continue?"));
        box.setInformativeText(tr("Host: %1\n%2").arg(host, hash));
        const QString details = body.value("details").toString();
        if (!details.isEmpty())
            box.setDetailedText(details);
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box.setDefaultButton(QMessageBox::No);
        m_svc->sendPromptResponse(promptId, box.exec() == QMessageBox::Yes);

    } else if (kind == QLatin1String("banner")) {
        QMessageBox box(this);
        box.setWindowTitle(tr("Server banner"));
        box.setText(body.value("banner").toString());
        box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Ok);
        m_svc->sendPromptResponse(promptId, box.exec() == QMessageBox::Ok);

    } else if (kind == QLatin1String("pin")) {
        bool ok = false;
        const QString text = QInputDialog::getText(this,
            body.value("tokenLabel").toString(), tr("Enter PIN:"),
            QLineEdit::Password, QString(), &ok);
        m_svc->sendPromptResponse(promptId, ok, QJsonObject{ { "value", text } });
    }
}

void MainWindow::onSvcPersist(const QString& what, const QJsonObject& body)
{
    if (m_connectingName.isEmpty())
        return;
    QString nm = m_connectingName;

    if (what == QLatin1String("trust")) {
        /* Remember the accepted server certificate (hash + DER); replayed as
         * profile.trust on the next connect so the prompt does not reappear. */
        QSettings st;
        const QString base = QStringLiteral("server:") + nm + "/";
        st.setValue(base + "pinned-cert-hash", body.value("hash").toString());
        st.setValue(base + "pinned-cert-der", body.value("derBase64").toString());
        return;
    }

    StoredServer ss;
    ss.load(nm);
    const QString value = body.value("value").toString();
    bool changed = true;
    if (what == QLatin1String("username")) ss.set_username(value);
    else if (what == QLatin1String("password")) {
        /* Save a password entered at the connect prompt if the profile opts in
         * ("Save password") OR already has a saved password — the latter lets a
         * corrected password overwrite a stale/wrong saved one. */
        if (ss.get_batch_mode() || !ss.get_password().isEmpty()) ss.set_password(value);
        else changed = false;
    }
    else if (what == QLatin1String("groupname")) ss.set_groupname(value);
    else if (what == QLatin1String("token")) ss.set_token_str(value);
    else changed = false;
    if (changed)
        ss.save();
}

void MainWindow::onSvcError(const QString& code, const QString& message)
{
    Logger::instance().addMessage(tr("Service error [%1]: %2").arg(code, message));
    changeStatus(STATUS_DISCONNECTED);
}
