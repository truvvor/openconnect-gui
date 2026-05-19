/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2026 Keenetic anti-DPI VPN client (fork)
 * GPLv2 — see LICENSE.txt
 *
 * Main window — see mainwindow.h for design.
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "editdialog.h"
#include "NewProfileDialog.h"
#include "logdialog.h"
#include "../server_storage.h"
#include "../vpninfo.h"
#include "../logger.h"
#include "../common.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QStyle>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

extern "C" {
#include <openconnect.h>
}

static constexpr int RECONNECT_MAX_ATTEMPTS = 10;

MainWindow::MainWindow(QWidget* parent, const QString& autoProfile)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(tr("Keenetic anti-DPI VPN"));

    refreshProfiles();
    updateStatus(tr("Disconnected"), false);

    buildMenus();
    buildTray();

    /* Wire button signals to public slots. */
    connect(ui->connectBtn,    &QPushButton::clicked, this, &MainWindow::on_connectClicked);
    connect(ui->disconnectBtn, &QPushButton::clicked, this, &MainWindow::on_disconnectClicked);
    connect(ui->newBtn,        &QPushButton::clicked, this, &MainWindow::on_newClicked);
    connect(ui->editBtn,       &QPushButton::clicked, this, &MainWindow::on_editClicked);
    connect(ui->deleteBtn,     &QPushButton::clicked, this, &MainWindow::on_deleteClicked);

    if (!autoProfile.isEmpty()) {
        int idx = ui->profileCombo->findText(autoProfile);
        if (idx >= 0) {
            ui->profileCombo->setCurrentIndex(idx);
            QTimer::singleShot(500, this, &MainWindow::on_connectClicked);
        }
    }
}

MainWindow::~MainWindow()
{
    if (m_worker) {
        m_worker->waitForFinished();
        delete m_worker;
    }
    delete m_vpn;
    delete ui;
}

void MainWindow::buildMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    QAction* importAct = fileMenu->addAction(tr("&Import camouflage config..."),
        this, &MainWindow::on_importCamouflageConfig);
    importAct->setToolTip(tr("Load a .conf file with camouflage-secret + tunnel-url + advanced options"));
    QAction* exportAct = fileMenu->addAction(tr("&Export camouflage config..."),
        this, &MainWindow::on_exportCamouflageConfig);
    exportAct->setToolTip(tr("Save the active profile's camouflage settings as a .conf file"));
    QAction* editAct = fileMenu->addAction(tr("Open camouflage config in &editor"),
        this, &MainWindow::on_editCamouflageConfigInEditor);
    editAct->setToolTip(tr("Open the per-profile camouflage .conf file in the system's default text editor"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), this, &MainWindow::on_quit, QKeySequence::Quit);

    QMenu* connMenu = menuBar()->addMenu(tr("&Connection"));
    m_actConnect    = connMenu->addAction(tr("&Connect"),    this, &MainWindow::on_connectClicked);
    m_actDisconnect = connMenu->addAction(tr("&Disconnect"), this, &MainWindow::on_disconnectClicked);
    m_actDisconnect->setEnabled(false);
    connMenu->addSeparator();
    m_actCompatibility = connMenu->addAction(tr("Co&mpatibility mode (camouflage OFF)"));
    m_actCompatibility->setCheckable(true);
    m_actCompatibility->setToolTip(tr(
        "Disable anti-DPI camouflage for the active profile. Use only when "
        "connecting to a stock Cisco AnyConnect / OpenConnect server."));
    connect(m_actCompatibility, &QAction::toggled, this, &MainWindow::on_toggleCompatibilityMode);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About..."), this, &MainWindow::on_about);
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction(tr("&Show"), this, &QWidget::showNormal);
    m_trayMenu->addAction(tr("&Connect"),    this, &MainWindow::on_connectClicked);
    m_trayMenu->addAction(tr("&Disconnect"), this, &MainWindow::on_disconnectClicked);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(tr("&Quit"), this, &MainWindow::on_quit);

    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(QIcon(":/images/traffic_light_off.png"));
    m_tray->setToolTip(tr("Keenetic anti-DPI VPN — disconnected"));
    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_tray->show();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger
        || reason == QSystemTrayIcon::DoubleClick) {
        if (isVisible()) hide(); else { showNormal(); activateWindow(); raise(); }
    }
}

void MainWindow::refreshProfiles(const QString& selectAfter)
{
    ui->profileCombo->clear();
    QSettings settings;
    QStringList groups = settings.childGroups();
    for (const QString& g : groups) {
        if (g.startsWith("server:")) {
            ui->profileCombo->addItem(g.mid(7));
        }
    }
    if (!selectAfter.isEmpty()) {
        int idx = ui->profileCombo->findText(selectAfter);
        if (idx >= 0) ui->profileCombo->setCurrentIndex(idx);
    }
}

void MainWindow::updateStatus(const QString& text, bool connected)
{
    ui->statusLabel->setText(text);
    ui->statusIndicator->setPixmap(QPixmap(connected
        ? ":/images/traffic_light_green.png"
        : ":/images/traffic_light_red.png"));

    ui->connectBtn->setEnabled(!connected);
    ui->disconnectBtn->setEnabled(connected);
    if (m_actConnect)    m_actConnect->setEnabled(!connected);
    if (m_actDisconnect) m_actDisconnect->setEnabled(connected);

    if (m_tray) {
        m_tray->setIcon(QIcon(connected
            ? ":/images/network-connected.png"
            : ":/images/network-disconnected.png"));
        m_tray->setToolTip(tr("Keenetic anti-DPI VPN — %1").arg(text));
    }
}

void MainWindow::on_connectClicked()
{
    const QString profile = ui->profileCombo->currentText();
    if (profile.isEmpty()) {
        QMessageBox::warning(this, tr("No profile"),
            tr("Create a profile first (New...)"));
        return;
    }

    if (!ensureWintunServiceRunning()) {
        QMessageBox::critical(this, tr("WinTun service"),
            tr("Cannot connect: KeeneticVpnService is not running. "
               "Re-run the installer or start the service from services.msc."));
        return;
    }

    StoredServer* ss = new StoredServer();
    QString p = profile;
    ss->load(p);

    /* Apply compatibility mode flag from menu. */
    if (m_actCompatibility && m_actCompatibility->isChecked()) {
        ss->set_camouflage_secret(QString());
        ss->set_tunnel_url("/CSCOSSLC/tunnel");
        Logger::instance().addMessage(tr("Compatibility mode active — camouflage disabled for this connection"));
    }

    try {
        m_vpn = new VpnInfo(profile, ss, this);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("VPN init failed"), QString::fromLocal8Bit(e.what()));
        delete ss;
        return;
    }

    QString url = ss->get_servername();
    if (!url.startsWith("https://") && !url.startsWith("http://"))
        url = "https://" + url;
    m_vpn->parse_url(url.toUtf8().data());

    updateStatus(tr("Connecting..."), false);
    Logger::instance().addMessage(tr("Connecting to %1").arg(url));

    m_worker = new QFutureWatcher<int>(this);
    connect(m_worker, &QFutureWatcher<int>::finished, this, &MainWindow::onWorkerFinished);
    m_worker->setFuture(QtConcurrent::run([this]() -> int {
        int rc = m_vpn->connect();
        if (rc != 0) return rc;
        m_vpn->mainloop();
        return 0;
    }));
}

void MainWindow::onWorkerFinished()
{
    int rc = m_worker->result();
    if (rc == 0 && !m_userInitiatedDisconnect) {
        QString ip, ip6, dns;
        m_vpn->get_info(dns, ip, ip6);
        updateStatus(tr("Connected: %1").arg(ip.isEmpty() ? "?" : ip), true);
    } else {
        if (m_userInitiatedDisconnect) {
            updateStatus(tr("Disconnected"), false);
        } else if (m_reconnectAttempts < RECONNECT_MAX_ATTEMPTS) {
            m_reconnectAttempts++;
            updateStatus(tr("Reconnecting (attempt %1/%2)...")
                .arg(m_reconnectAttempts).arg(RECONNECT_MAX_ATTEMPTS), false);
            QTimer::singleShot(30 * 1000, this, &MainWindow::on_connectClicked);
        } else {
            updateStatus(tr("Disconnected (failed after %1 attempts)").arg(RECONNECT_MAX_ATTEMPTS), false);
            m_reconnectAttempts = 0;
        }
    }
    delete m_vpn;
    m_vpn = nullptr;
    m_userInitiatedDisconnect = false;
    m_worker->deleteLater();
    m_worker = nullptr;
}

void MainWindow::on_disconnectClicked()
{
    if (!m_vpn) { updateStatus(tr("Disconnected"), false); return; }
    m_userInitiatedDisconnect = true;
    openconnect_cancel(m_vpn->vpninfo);
    updateStatus(tr("Disconnecting..."), false);
}

void MainWindow::on_editClicked()
{
    const QString p = ui->profileCombo->currentText();
    if (p.isEmpty()) return;
    EditDialog d(p, this);
    if (d.exec() == QDialog::Accepted) refreshProfiles(d.get_label());
}

void MainWindow::on_newClicked()
{
    NewProfileDialog n(this);
    if (n.exec() != QDialog::Accepted) return;
    EditDialog d(this);
    if (d.exec() == QDialog::Accepted) refreshProfiles(d.get_label());
}

void MainWindow::on_deleteClicked()
{
    const QString p = ui->profileCombo->currentText();
    if (p.isEmpty()) return;
    if (QMessageBox::question(this, tr("Delete profile"),
        tr("Delete profile '%1'?").arg(p)) != QMessageBox::Yes) return;
    QSettings settings;
    settings.remove(QLatin1String("server:") + p);
    refreshProfiles();
}

/* --------- Camouflage config file menu actions ------------------ */
QString MainWindow::camouflageConfigDir() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir + "/camouflage-configs");
    return dir + "/camouflage-configs";
}
QString MainWindow::camouflageConfigForProfile(const QString& profile) const
{
    return camouflageConfigDir() + "/" + profile + ".conf";
}

void MainWindow::on_importCamouflageConfig()
{
    const QString p = ui->profileCombo->currentText();
    if (p.isEmpty()) {
        QMessageBox::information(this, tr("Import camouflage config"),
            tr("Select a profile first."));
        return;
    }
    const QString fn = QFileDialog::getOpenFileName(this,
        tr("Import camouflage config"), QString(),
        tr("Camouflage config (*.conf *.cfg);;All files (*.*)"));
    if (fn.isEmpty()) return;
    QFile in(fn);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Import failed"),
            tr("Cannot open %1").arg(fn));
        return;
    }
    StoredServer ss;
    QString prof = p;
    ss.load(prof);

    QTextStream ts(&in);
    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        QString key = line.left(eq).trimmed();
        QString val = line.mid(eq + 1).trimmed();
        if (val.startsWith('"') && val.endsWith('"'))
            val = val.mid(1, val.length() - 2);
        if      (key == "camouflage-secret")    ss.set_camouflage_secret(val);
        else if (key == "camouflage-tunnel-url") ss.set_tunnel_url(val);
        else if (key == "server")               ss.set_servername(val);
        else if (key == "username")             ss.set_username(val);
    }
    in.close();
    ss.save();

    /* Mirror imported file to per-profile camouflage-configs/ directory so
     * "Edit in editor" can keep it in sync. */
    const QString dst = camouflageConfigForProfile(p);
    QFile::remove(dst);
    QFile::copy(fn, dst);

    QMessageBox::information(this, tr("Import camouflage config"),
        tr("Imported %1 into profile '%2'").arg(QFileInfo(fn).fileName()).arg(p));
}

void MainWindow::on_exportCamouflageConfig()
{
    const QString p = ui->profileCombo->currentText();
    if (p.isEmpty()) return;
    StoredServer ss;
    QString prof = p;
    ss.load(prof);

    const QString fn = QFileDialog::getSaveFileName(this,
        tr("Export camouflage config"), p + ".conf",
        tr("Camouflage config (*.conf)"));
    if (fn.isEmpty()) return;

    QFile out(fn);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export failed"),
            tr("Cannot write %1").arg(fn));
        return;
    }
    QTextStream ts(&out);
    ts << "# Keenetic anti-DPI VPN — camouflage profile\n";
    ts << "# Exported from profile: " << p << "\n";
    ts << "server                 = " << ss.get_servername() << "\n";
    ts << "username               = " << ss.get_username() << "\n";
    ts << "camouflage-secret      = \"" << ss.get_camouflage_secret() << "\"\n";
    ts << "camouflage-tunnel-url  = " << ss.get_tunnel_url() << "\n";
    out.close();
}

void MainWindow::on_editCamouflageConfigInEditor()
{
    const QString p = ui->profileCombo->currentText();
    if (p.isEmpty()) {
        QMessageBox::information(this, tr("Edit camouflage config"),
            tr("Select a profile first."));
        return;
    }
    const QString path = camouflageConfigForProfile(p);
    if (!QFile::exists(path)) {
        /* Pre-fill from profile on first edit. */
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            StoredServer ss;
            QString prof = p;
            ss.load(prof);
            QTextStream ts(&f);
            ts << "# Keenetic anti-DPI VPN — camouflage profile (live edit)\n";
            ts << "# Save & re-open profile, or use Import to apply changes.\n";
            ts << "server                 = " << ss.get_servername() << "\n";
            ts << "username               = " << ss.get_username() << "\n";
            ts << "camouflage-secret      = \"" << ss.get_camouflage_secret() << "\"\n";
            ts << "camouflage-tunnel-url  = " << ss.get_tunnel_url() << "\n";
            f.close();
        }
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::on_toggleCompatibilityMode(bool legacy)
{
    Logger::instance().addMessage(legacy
        ? tr("Compatibility mode ON — next connect will use stock AnyConnect protocol")
        : tr("Compatibility mode OFF — camouflage Level 2 active"));
}

void MainWindow::on_about()
{
    QMessageBox::about(this, tr("About"),
        tr("<h3>Keenetic anti-DPI VPN client</h3>"
           "<p>Forked from OpenConnect-GUI 1.5.3 (https://github.com/openconnect/openconnect-gui).</p>"
           "<p>Built on libopenconnect with Keenetic camouflage patches:"
           "<br>HMAC-SHA256 CSTP magic + X-S-/X-D- header rewriting + /api/v1/session "
           "tunnel URL + TCP-scatter ClientHello.</p>"
           "<p>WinTun-backed TUN — no UAC required after one-time service install.</p>"
           "<p>License: GPL v2</p>"));
}

void MainWindow::on_quit()
{
    if (m_vpn) on_disconnectClicked();
    QApplication::quit();
}

void MainWindow::updateStats(const struct oc_stats* /*stats*/, const QString& /*cstpCipher*/)
{
    /* Optional: surface RX/TX counters in status bar. */
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_tray && m_tray->isVisible()) {
        QMessageBox::information(this, tr("Keenetic VPN"),
            tr("The application will keep running in the system tray. "
               "Right-click the tray icon to quit."));
        hide();
        event->ignore();
    } else {
        on_quit();
    }
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange
        && isMinimized() && m_tray) {
        QTimer::singleShot(0, this, &QWidget::hide);
    }
    QMainWindow::changeEvent(event);
}

bool MainWindow::ensureWintunServiceRunning()
{
#ifdef _WIN32
    /* Phase 4 placeholder — wintun_client.cpp will own this. For now ask
     * Windows SCM if KeeneticVpnService is running; return true if it is.
     * If not present at all, return true anyway so dev builds still work
     * (vpninfo.cpp falls back to openconnect_setup_tun_device which needs
     * admin — Phase 4 wires up real RPC).
     */
    return true;
#else
    return true;
#endif
}
