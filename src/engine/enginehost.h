/*
 * openconnect-gui — headless VPN engine host interface.
 *
 * VpnEngine (service-side) runs libopenconnect and calls back into an EngineHost
 * for everything that used to touch MainWindow / MyInputDialog / Logger / gtdb.
 * The service implements EngineHost by marshalling to the GUI over IPC.
 *
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace oc::engine {

struct FormChoice {
    QString name;
    QString label;
};

struct FormOpt {
    QString name;
    QString label;
    QString type;            // "text" | "password" | "select" | "hidden"
    QVector<FormChoice> choices;
    QString value;           // filled in by the host on return
};

/* Mirrors oc_auth_form for one interactive round. The host fills group.value
 * and each opts[i].value, then returns true to submit / false to cancel. */
struct AuthForm {
    QString banner;
    QString message;
    QString error;

    bool hasGroup = false;
    QString groupName;
    QString groupLabel;
    QString groupCurrent;            // pre-selected group (from profile)
    QVector<FormChoice> groupChoices;
    QString groupValue;              // filled by host (chosen group name)

    QVector<FormOpt> opts;
};

class EngineHost {
public:
    virtual ~EngineHost() = default;

    /* one-way notifications (engine thread) */
    virtual void onLog(int level, const QString& msg) = 0;
    virtual void onState(const QString& state, const QString& detail = {}) = 0;
    virtual void onStats(quint64 rxBytes, quint64 txBytes,
                         const QString& cstpCipher, const QString& dtlsCipher) = 0;
    virtual void onIpInfo(const QString& addr, const QString& netmask,
                         const QString& addr6, const QString& dns) = 0;
    /* persistent data learned at runtime; GUI writes it to its QSettings */
    virtual void onPersistTrust(const QString& hash, const QString& derBase64) = 0;
    virtual void onPersistString(const QString& what, const QString& value) = 0;

    /* interactive prompts — block the engine thread until answered */
    virtual bool askAuthForm(AuthForm& form) = 0;           // true = submit
    virtual bool askCert(const QString& reason, const QString& host,
                         const QString& hash, const QString& details,
                         const QString& change) = 0;          // true = trust
    virtual bool askBanner(const QString& banner) = 0;        // true = accept
    virtual bool askPin(const QString& tokenUrl, const QString& tokenLabel,
                        unsigned flags, QString& pinOut) = 0; // true = provided
};

} // namespace oc::engine
