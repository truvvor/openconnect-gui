/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2026 Keenetic anti-DPI VPN client (fork)
 * GPLv2 — see LICENSE.txt
 *
 * Compact main window for Keenetic camouflage VPN client.
 *
 * Differences from upstream:
 *   - Tray icon with Connect / Disconnect / Show / Quit (Q4 locked)
 *   - File menu: Import camouflage config / Export camouflage config /
 *                Edit camouflage config (system editor)
 *   - Connection menu: toggle "Compatibility mode" (camouflage OFF for
 *                       active profile, sets tunnel URL to /CSCOSSLC/tunnel)
 *   - Auto-reconnect default 30s, up to 10 attempts (Q5 locked)
 *   - No cert/key/PKCS#11 UI; no TOFU pinning dialog
 */

#pragma once

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QFutureWatcher>

class VpnInfo;
struct oc_stats;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr, const QString& autoProfile = QString());
    ~MainWindow();

    void updateStats(const struct oc_stats* stats, const QString& cstpCipher);

public slots:
    void on_connectClicked();
    void on_disconnectClicked();
    void on_editClicked();
    void on_newClicked();
    void on_deleteClicked();
    void on_importCamouflageConfig();
    void on_exportCamouflageConfig();
    void on_editCamouflageConfigInEditor();
    void on_toggleCompatibilityMode(bool legacy);
    void on_about();
    void on_quit();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onWorkerFinished();

private:
    void refreshProfiles(const QString& selectAfter = QString());
    void updateStatus(const QString& text, bool connected);
    void buildTray();
    void buildMenus();
    bool ensureWintunServiceRunning();
    QString camouflageConfigDir() const;
    QString camouflageConfigForProfile(const QString& profile) const;

    Ui::MainWindow* ui;
    QSystemTrayIcon* m_tray { nullptr };
    QMenu* m_trayMenu { nullptr };
    QAction* m_actConnect { nullptr };
    QAction* m_actDisconnect { nullptr };
    QAction* m_actCompatibility { nullptr };

    VpnInfo* m_vpn { nullptr };
    QFutureWatcher<int>* m_worker { nullptr };
    int m_reconnectAttempts { 0 };
    bool m_userInitiatedDisconnect { false };
};
