/*
 * Copyright (C) 2014 Red Hat
 * Copyright (C) 2026 Keenetic anti-DPI VPN client (fork)
 * GPLv2 — see LICENSE.txt
 */

#pragma once

#include "server_storage.h"

#include <QDialog>
#include <QString>

namespace Ui {
class EditDialog;
}

class EditDialog : public QDialog {
    Q_OBJECT
public:
    explicit EditDialog(QWidget* parent = nullptr);
    explicit EditDialog(const QString& server, QWidget* parent = nullptr);
    ~EditDialog();

    QString get_label() const;

private slots:
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void on_camouflageToggle_toggled(bool checked);

private:
    Ui::EditDialog* ui;
    StoredServer m_server;
    QString m_originalLabel;
};
