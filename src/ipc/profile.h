/*
 * openconnect-gui — IPC connection profile (the payload of a `connect` message).
 *
 * Fully resolved by the unprivileged GUI (secrets decrypted from DPAPI QSettings)
 * and consumed in-memory by the privileged service. See protocol §5.3.
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace oc::ipc {

/* A gtdb pinned server pubkey carried into the service so cert validation can
 * succeed without the user's QSettings hive, and echoed back on first accept. */
struct TrustPin {
    QString hash;       // e.g. "sha256:abcd..."
    QString derBase64;  // base64 of the DER cert/pubkey

    QJsonObject toJson() const;
    static TrustPin fromJson(const QJsonObject& o);
};

struct Profile {
    QString name;
    QString server;
    QString protocol = QStringLiteral("anyconnect");

    QString camouflageSecret;            // empty ⇒ camouflage off
    bool    disableUdp = false;
    bool    autoAcceptBanner = true;

    int     reconnectTimeout = 300;
    int     dtlsReconnectTimeout = 60;
    QString reportedOs = QStringLiteral("win");

    /* Optional pre-filled credentials. Empty ⇒ the engine raises an auth-form
     * prompt that the GUI answers interactively. */
    QString username;
    QString password;
    QString groupname;

    /* Client cert / key / CA as PEM bytes (not paths): SYSTEM cannot read files
     * under the user profile. Empty caCertPem ⇒ service falls back to bundled LE CA. */
    QByteArray clientCertPem;
    QByteArray clientKeyPem;
    QByteArray caCertPem;

    /* Software token: tokenType matches oc_token_mode_t (0 = none). */
    int     tokenType = 0;
    QString tokenSecret;

    QVector<TrustPin> trust;

    QJsonObject toJson() const;
    static Profile fromJson(const QJsonObject& o);

    /* never log this verbatim — redacts secrets for the connection log */
    QString redactedSummary() const;
};

} // namespace oc::ipc
