/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2026 Keenetic anti-DPI VPN client (fork)
 * GPLv2 — see LICENSE.txt
 *
 * Keenetic-camouflage profile editor.
 *
 * Stripped compared to upstream openconnect-gui:
 *   - removed cert/key file pickers, key password, group name combo,
 *     token type/string fields, "disable DTLS" checkbox (DTLS always off)
 *   - added camouflage secret (password mask), tunnel URL field,
 *     "Camouflage mode" toggle (off = legacy AnyConnect-compatible)
 *
 * Camouflage toggle semantics:
 *   ON  (default) — passes camouflage-secret + tunnel_url=/api/v1/session
 *                   to libopenconnect, full anti-DPI envelope
 *   OFF           — clears camouflage-secret, tunnel_url=/CSCOSSLC/tunnel,
 *                   client behaves like stock openconnect for legacy
 *                   AnyConnect / Cisco SSL VPN endpoints
 */

#include "editdialog.h"
#include "ui_editdialog.h"

EditDialog::EditDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::EditDialog)
    , m_originalLabel("")
{
    ui->setupUi(this);
    ui->camouflageToggle->setChecked(true);
    ui->tunnelUrlEdit->setText("/api/v1/session");
}

EditDialog::EditDialog(const QString& server, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::EditDialog)
    , m_originalLabel(server)
{
    ui->setupUi(this);
    QString name = server;
    m_server.load(name);

    ui->labelEdit->setText(m_server.get_label());
    ui->serverEdit->setText(m_server.get_servername());
    ui->usernameEdit->setText(m_server.get_username());
    ui->passwordEdit->setText(m_server.get_password());
    ui->camouflageSecretEdit->setText(m_server.get_camouflage_secret());
    ui->tunnelUrlEdit->setText(m_server.get_tunnel_url());
    ui->reconnectSpin->setValue(m_server.get_reconnect_timeout());
    ui->minimizeCheck->setChecked(m_server.get_minimize());

    /* If camouflage secret is non-empty, toggle is ON. */
    const bool camouflage = !m_server.get_camouflage_secret().isEmpty();
    ui->camouflageToggle->setChecked(camouflage);
    on_camouflageToggle_toggled(camouflage);
}

EditDialog::~EditDialog() { delete ui; }

QString EditDialog::get_label() const { return m_server.get_label(); }

void EditDialog::on_camouflageToggle_toggled(bool checked)
{
    /* Grey-out / enable camouflage-specific fields. */
    ui->camouflageSecretEdit->setEnabled(checked);
    ui->camouflageSecretLabel->setEnabled(checked);
    ui->tunnelUrlEdit->setEnabled(checked);
    ui->tunnelUrlLabel->setEnabled(checked);

    if (checked) {
        if (ui->tunnelUrlEdit->text().isEmpty()
            || ui->tunnelUrlEdit->text() == QLatin1String("/CSCOSSLC/tunnel"))
            ui->tunnelUrlEdit->setText("/api/v1/session");
    } else {
        ui->tunnelUrlEdit->setText("/CSCOSSLC/tunnel");
        ui->camouflageSecretEdit->clear();
    }
}

void EditDialog::on_buttonBox_accepted()
{
    m_server.set_label(ui->labelEdit->text());
    m_server.set_servername(ui->serverEdit->text());
    m_server.set_username(ui->usernameEdit->text());
    m_server.set_password(ui->passwordEdit->text());

    if (ui->camouflageToggle->isChecked()) {
        m_server.set_camouflage_secret(ui->camouflageSecretEdit->text());
        m_server.set_tunnel_url(ui->tunnelUrlEdit->text().isEmpty()
            ? QStringLiteral("/api/v1/session") : ui->tunnelUrlEdit->text());
    } else {
        m_server.set_camouflage_secret(QString());
        m_server.set_tunnel_url("/CSCOSSLC/tunnel");
    }
    m_server.set_reconnect_timeout(ui->reconnectSpin->value());
    m_server.set_minimize(ui->minimizeCheck->isChecked());
    m_server.save();
    accept();
}

void EditDialog::on_buttonBox_rejected() { reject(); }
