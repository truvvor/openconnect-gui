/*
 * openconnect-gui — IPC connection profile (de)serialization (oc-ipc).
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#include "profile.h"

namespace oc::ipc {

QJsonObject TrustPin::toJson() const
{
    return QJsonObject{
        { QStringLiteral("hash"), hash },
        { QStringLiteral("derBase64"), derBase64 },
    };
}

TrustPin TrustPin::fromJson(const QJsonObject& o)
{
    TrustPin p;
    p.hash = o.value(QStringLiteral("hash")).toString();
    p.derBase64 = o.value(QStringLiteral("derBase64")).toString();
    return p;
}

static QString b64(const QByteArray& a) { return QString::fromLatin1(a.toBase64()); }
static QByteArray unb64(const QString& s) { return QByteArray::fromBase64(s.toLatin1()); }

QJsonObject Profile::toJson() const
{
    QJsonArray pins;
    for (const auto& t : trust)
        pins.append(t.toJson());

    return QJsonObject{
        { QStringLiteral("name"), name },
        { QStringLiteral("server"), server },
        { QStringLiteral("protocol"), protocol },
        { QStringLiteral("camouflageSecret"), camouflageSecret },
        { QStringLiteral("disableUdp"), disableUdp },
        { QStringLiteral("autoAcceptBanner"), autoAcceptBanner },
        { QStringLiteral("noDefaultRoute"), noDefaultRoute },
        { QStringLiteral("reconnectTimeout"), reconnectTimeout },
        { QStringLiteral("dtlsReconnectTimeout"), dtlsReconnectTimeout },
        { QStringLiteral("reportedOs"), reportedOs },
        { QStringLiteral("username"), username },
        { QStringLiteral("password"), password },
        { QStringLiteral("groupname"), groupname },
        { QStringLiteral("clientCertPem"), b64(clientCertPem) },
        { QStringLiteral("clientKeyPem"), b64(clientKeyPem) },
        { QStringLiteral("caCertPem"), b64(caCertPem) },
        { QStringLiteral("tokenType"), tokenType },
        { QStringLiteral("tokenSecret"), tokenSecret },
        { QStringLiteral("trust"), pins },
    };
}

Profile Profile::fromJson(const QJsonObject& o)
{
    Profile p;
    p.name = o.value(QStringLiteral("name")).toString();
    p.server = o.value(QStringLiteral("server")).toString();
    p.protocol = o.value(QStringLiteral("protocol")).toString(QStringLiteral("anyconnect"));
    p.camouflageSecret = o.value(QStringLiteral("camouflageSecret")).toString();
    p.disableUdp = o.value(QStringLiteral("disableUdp")).toBool(false);
    p.autoAcceptBanner = o.value(QStringLiteral("autoAcceptBanner")).toBool(true);
    p.noDefaultRoute = o.value(QStringLiteral("noDefaultRoute")).toBool(false);
    p.reconnectTimeout = o.value(QStringLiteral("reconnectTimeout")).toInt(300);
    p.dtlsReconnectTimeout = o.value(QStringLiteral("dtlsReconnectTimeout")).toInt(60);
    p.reportedOs = o.value(QStringLiteral("reportedOs")).toString(QStringLiteral("win"));
    p.username = o.value(QStringLiteral("username")).toString();
    p.password = o.value(QStringLiteral("password")).toString();
    p.groupname = o.value(QStringLiteral("groupname")).toString();
    p.clientCertPem = unb64(o.value(QStringLiteral("clientCertPem")).toString());
    p.clientKeyPem = unb64(o.value(QStringLiteral("clientKeyPem")).toString());
    p.caCertPem = unb64(o.value(QStringLiteral("caCertPem")).toString());
    p.tokenType = o.value(QStringLiteral("tokenType")).toInt(0);
    p.tokenSecret = o.value(QStringLiteral("tokenSecret")).toString();
    const QJsonArray pins = o.value(QStringLiteral("trust")).toArray();
    for (const auto& v : pins)
        p.trust.append(TrustPin::fromJson(v.toObject()));
    return p;
}

QString Profile::redactedSummary() const
{
    auto yn = [](bool b) { return b ? QStringLiteral("yes") : QStringLiteral("no"); };
    return QStringLiteral("profile{name=%1 server=%2 proto=%3 camouflage=%4 "
                          "disableUdp=%5 autoBanner=%6 user=%7 cert=%8 ca=%9 token=%10 pins=%11}")
        .arg(name, server, protocol,
             yn(!camouflageSecret.isEmpty()), yn(disableUdp), yn(autoAcceptBanner),
             username.isEmpty() ? QStringLiteral("(prompt)") : QStringLiteral("(set)"),
             yn(!clientCertPem.isEmpty()), yn(!caCertPem.isEmpty()))
        .arg(tokenType)
        .arg(trust.size());
}

} // namespace oc::ipc
