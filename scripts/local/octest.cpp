/*
 * octest — unprivileged IPC test client for openconnect-gui-service.
 * Speaks the protocol-v1 named pipe directly: hello -> connect{profile} and
 * prints state/log/ipinfo, auto-filling auth-form + accepting cert/banner.
 * Proves the privilege-separated path (unprivileged client -> LocalSystem
 * service -> camouflaged VPN) without driving the GUI. GPLv2-or-later.
 *
 * argv: server username password camouflageSecret
 */
#include "ipc/profile.h"
#include "ipc/protocol.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QLocalSocket>
#include <QTextStream>
#include <QTimer>

using namespace oc::ipc;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    Profile p;
    p.server = a.value(1);
    p.username = a.value(2);
    p.password = a.value(3);
    p.camouflageSecret = a.value(4);
    p.protocol = QStringLiteral("anyconnect");
    p.disableUdp = true;        // no-dtls
    p.autoAcceptBanner = true;

    QLocalSocket sock;
    QByteArray buf;
    QTextStream out(stdout);
    auto P = [&](const QString& s) { out << s << "\n"; out.flush(); };

    QObject::connect(&sock, &QLocalSocket::connected, [&]() {
        Message h(QString::fromLatin1(msg::Hello), 1);
        h.json().insert("client", "octest");
        h.json().insert("clientVersion", "test");
        sock.write(h.toLine());
        sock.flush();
    });

    QObject::connect(&sock, &QLocalSocket::readyRead, [&]() {
        buf.append(sock.readAll());
        int nl;
        while ((nl = buf.indexOf('\n')) >= 0) {
            QByteArray line = buf.left(nl);
            buf.remove(0, nl + 1);
            if (line.trimmed().isEmpty())
                continue;
            bool ok = false;
            Message m = Message::fromLine(line, &ok);
            if (!ok)
                continue;
            const QString t = m.type();
            const QJsonObject j = m.json();
            if (t == QLatin1String(msg::HelloAck)) {
                P("[hello-ack] -> connect");
                Message c(QString::fromLatin1(msg::Connect), 2);
                c.json().insert("profile", p.toJson());
                sock.write(c.toLine());
                sock.flush();
            } else if (t == QLatin1String(msg::Log)) {
                P("[log] " + j.value("msg").toString());
            } else if (t == QLatin1String(msg::State)) {
                const QString s = j.value("state").toString();
                P("[state] " + s + " " + j.value("detail").toString());
                if (s == QLatin1String("connected")) {
                    P("RESULT_CONNECTED");
                    QTimer::singleShot(2000, [&]() { app.quit(); });
                } else if (s == QLatin1String("disconnected")) {
                    P("RESULT_DISCONNECTED");
                    QTimer::singleShot(300, [&]() { app.exit(2); });
                }
            } else if (t == QLatin1String(msg::IpInfo)) {
                P("[ipinfo] addr=" + j.value("addr").toString() + " dns=" + j.value("dns").toString());
            } else if (t == QLatin1String(msg::Prompt)) {
                const QString kind = j.value("kind").toString();
                const quint64 pid = static_cast<quint64>(j.value("promptId").toDouble());
                P("[prompt] " + kind);
                Message r(QString::fromLatin1(msg::PromptResponse));
                r.json().insert("promptId", static_cast<double>(pid));
                r.json().insert("ok", true);
                if (kind == QLatin1String("auth-form")) {
                    const QJsonObject form = j.value("form").toObject();
                    QJsonObject fields;
                    for (const auto& v : form.value("opts").toArray()) {
                        const QJsonObject o = v.toObject();
                        fields.insert(o.value("name").toString(),
                                      o.value("type").toString() == QLatin1String("password") ? p.password : p.username);
                    }
                    const QJsonObject g = form.value("authgroup").toObject();
                    if (!g.isEmpty()) {
                        const QJsonArray ch = g.value("choices").toArray();
                        if (!ch.isEmpty())
                            fields.insert("__group__", ch.first().toObject().value("name").toString());
                    }
                    r.json().insert("fields", fields);
                } else if (kind == QLatin1String("pin")) {
                    r.json().insert("value", p.password);
                }
                sock.write(r.toLine());
                sock.flush();
            } else if (t == QLatin1String(msg::Error)) {
                P("[error] " + j.value("code").toString() + ": " + j.value("message").toString());
            }
        }
    });

    QObject::connect(&sock, QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::errorOccurred),
                     [&]() { P("[socket-error] " + sock.errorString()); app.exit(3); });

    sock.connectToServer(QString::fromLatin1(kPipeName));
    QTimer::singleShot(90000, [&]() { P("RESULT_TIMEOUT"); app.exit(4); });
    return app.exec();
}
