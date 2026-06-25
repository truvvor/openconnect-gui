/*
 * openconnect-gui — IPC client implementation. GPLv2-or-later.
 */
#include "serviceclient.h"

#include <QCoreApplication>
#include <QLocalSocket>

using namespace oc::ipc;

ServiceClient::ServiceClient(QObject* parent) : QObject(parent)
{
    m_sock = new QLocalSocket(this);
    connect(m_sock, &QLocalSocket::connected, this, &ServiceClient::onConnected);
    connect(m_sock, &QLocalSocket::readyRead, this, &ServiceClient::onReadyRead);
    connect(m_sock,
        QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::errorOccurred),
        this, &ServiceClient::onSocketError);
    // The service stopping (or crashing) closes the pipe without an error; treat
    // a clean disconnect as "service gone" too, so the GUI can recover its state.
    connect(m_sock, &QLocalSocket::disconnected, this, [this]() {
        m_ready = false;
        m_haveQueuedConnect = false;
        emit serviceUnavailable(QStringLiteral("connection to the VPN service was closed"));
    });
}

void ServiceClient::resendLastConnect()
{
    // Re-send the previously requested profile (m_queuedConnect still holds it).
    m_haveQueuedConnect = true;
    ensureConnected();
}

void ServiceClient::ensureConnected()
{
    if (m_sock->state() == QLocalSocket::ConnectedState) {
        if (m_ready && m_haveQueuedConnect) {
            m_haveQueuedConnect = false;
            Message m(QString::fromLatin1(msg::Connect), 1);
            m.json().insert(QStringLiteral("profile"), m_queuedConnect.toJson());
            send(m);
        }
        return;
    }
    if (m_sock->state() == QLocalSocket::UnconnectedState) {
        m_ready = false;
        m_buf.clear();
        m_sock->connectToServer(QString::fromLatin1(kPipeName));
    }
}

void ServiceClient::connectVpn(const Profile& profile)
{
    m_queuedConnect = profile;
    m_haveQueuedConnect = true;
    ensureConnected();
}

void ServiceClient::disconnectVpn()
{
    if (m_sock->state() == QLocalSocket::ConnectedState)
        send(Message(QString::fromLatin1(msg::Disconnect), 2));
    m_haveQueuedConnect = false;
}

void ServiceClient::requestStatus()
{
    if (m_sock->state() == QLocalSocket::ConnectedState)
        send(Message(QString::fromLatin1(msg::Status), 3));
}

void ServiceClient::sendPromptResponse(quint64 promptId, bool ok, const QJsonObject& extra)
{
    Message m(QString::fromLatin1(msg::PromptResponse));
    m.json().insert(QStringLiteral("promptId"), static_cast<double>(promptId));
    m.json().insert(QStringLiteral("ok"), ok);
    for (auto it = extra.begin(); it != extra.end(); ++it)
        m.json().insert(it.key(), it.value());
    send(m);
}

void ServiceClient::send(const Message& m)
{
    if (m_sock->state() == QLocalSocket::ConnectedState) {
        m_sock->write(m.toLine());
        m_sock->flush();
    }
}

void ServiceClient::onConnected()
{
    Message hello(QString::fromLatin1(msg::Hello), 1);
    hello.json().insert(QStringLiteral("client"), QStringLiteral("openconnect-gui"));
    hello.json().insert(QStringLiteral("clientVersion"), QStringLiteral("1.5.3"));
    hello.json().insert(QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid()));
    send(hello);
}

void ServiceClient::onSocketError()
{
    m_ready = false;
    emit serviceUnavailable(m_sock->errorString());
}

void ServiceClient::onReadyRead()
{
    m_buf.append(m_sock->readAll());
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        if (line.trimmed().isEmpty())
            continue;
        bool ok = false;
        Message m = Message::fromLine(line, &ok);
        if (ok && m.isValid())
            handle(m);
    }
}

void ServiceClient::handle(const Message& m)
{
    const QString t = m.type();
    const QJsonObject& j = m.json();
    if (t == QLatin1String(msg::HelloAck)) {
        m_ready = true;
        emit ready();
        ensureConnected();   // flush a queued connect
    } else if (t == QLatin1String(msg::Log)) {
        emit logReceived(j.value(QStringLiteral("level")).toInt(),
                         j.value(QStringLiteral("msg")).toString());
    } else if (t == QLatin1String(msg::State)) {
        emit stateChanged(j.value(QStringLiteral("state")).toString(),
                          j.value(QStringLiteral("detail")).toString());
    } else if (t == QLatin1String(msg::Stats)) {
        emit statsReceived(j.value(QStringLiteral("rxBytes")).toDouble(),
                           j.value(QStringLiteral("txBytes")).toDouble(),
                           j.value(QStringLiteral("cstpCipher")).toString(),
                           j.value(QStringLiteral("dtlsCipher")).toString());
    } else if (t == QLatin1String(msg::IpInfo)) {
        emit ipInfoReceived(j.value(QStringLiteral("addr")).toString(),
                            j.value(QStringLiteral("netmask")).toString(),
                            j.value(QStringLiteral("addr6")).toString(),
                            j.value(QStringLiteral("dns")).toString());
    } else if (t == QLatin1String(msg::Prompt)) {
        emit promptReceived(j.value(QStringLiteral("kind")).toString(),
                            static_cast<quint64>(j.value(QStringLiteral("promptId")).toDouble()), j);
    } else if (t == QLatin1String(msg::Persist)) {
        emit persistReceived(j.value(QStringLiteral("what")).toString(), j);
    } else if (t == QLatin1String(msg::Error)) {
        emit serviceError(j.value(QStringLiteral("code")).toString(),
                          j.value(QStringLiteral("message")).toString());
    }
}
