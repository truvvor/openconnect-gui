/*
 * openconnect-gui-service — client session: EngineHost + IPC marshalling.
 * This file is part of openconnect-gui. GPLv2-or-later.
 */
#include "ipcsession.h"
#include "engine/vpnengine.h"
#include "ipc/profile.h"
#include "servicelog.h"

#include <QJsonArray>
#include <QLocalSocket>
#include <QMetaObject>

using namespace oc::ipc;
using oc::engine::AuthForm;
using oc::engine::FormChoice;
using oc::engine::FormOpt;
using oc::engine::VpnEngine;

IpcSession::IpcSession(QLocalSocket* sock, QObject* parent)
    : QObject(parent), m_sock(sock)
{
    m_sock->setParent(this);
    connect(m_sock, &QLocalSocket::readyRead, this, &IpcSession::onReadyRead);
    connect(m_sock, &QLocalSocket::disconnected, this, &IpcSession::onDisconnected);
}

IpcSession::~IpcSession()
{
    if (m_engine)
        m_engine->cancel();
    wakeAllPending(false);
    if (m_engineThread.joinable())
        m_engineThread.join();
    delete m_engine;
    m_engine = nullptr;
}

/* ---- outbound (main thread) ------------------------------------------- */

void IpcSession::send(const Message& m)
{
    if (m_sock && m_sock->state() == QLocalSocket::ConnectedState) {
        m_sock->write(m.toLine());
        m_sock->flush();
    }
}

void IpcSession::sendQueued(const QByteArray& line)
{
    if (m_sock && m_sock->state() == QLocalSocket::ConnectedState) {
        m_sock->write(line);
        m_sock->flush();
    }
}

void IpcSession::postSend(const Message& m)
{
    // Callable from any thread; the actual write happens on the main thread.
    QMetaObject::invokeMethod(this, "sendQueued", Qt::QueuedConnection,
                              Q_ARG(QByteArray, m.toLine()));
}

void IpcSession::sendError(quint64 id, const char* code, const QString& message)
{
    Message m(QString::fromLatin1(msg::Error), id);
    m.json().insert(QStringLiteral("code"), QString::fromLatin1(code));
    m.json().insert(QStringLiteral("message"), message);
    send(m);
}

void IpcSession::onReadyRead()
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
        if (!ok || !m.isValid()) {
            svc::log::warn(QStringLiteral("dropping malformed IPC line"));
            continue;
        }
        dispatch(m);
    }
}

void IpcSession::onDisconnected()
{
    svc::log::info(QStringLiteral("client pid=%1 disconnected").arg(m_clientPid));
    if (m_engine)
        m_engine->cancel();
    wakeAllPending(false);
    emit closed(this);
}

/* ---- dispatch --------------------------------------------------------- */

void IpcSession::dispatch(const Message& m)
{
    const QString t = m.type();
    if (!m.versionCompatible()) {
        sendError(m.id(), err::BadVersion,
                  QStringLiteral("service speaks protocol v%1").arg(kProtocolVersion));
        return;
    }
    if (t == QLatin1String(msg::Hello)) { onHello(m); return; }
    if (!m_helloDone) {
        sendError(m.id(), err::Unauthorized, QStringLiteral("hello required first"));
        return;
    }
    if (t == QLatin1String(msg::Connect)) onConnect(m);
    else if (t == QLatin1String(msg::Disconnect)) onDisconnect(m);
    else if (t == QLatin1String(msg::Status)) onStatus(m);
    else if (t == QLatin1String(msg::PromptResponse)) onPromptResponse(m);
    else svc::log::warn(QStringLiteral("unknown message type '%1'").arg(t));
}

void IpcSession::onHello(const Message& m)
{
    const QString client = m.json().value(QStringLiteral("client")).toString();
    const QString cver = m.json().value(QStringLiteral("clientVersion")).toString();
    svc::log::info(QStringLiteral("hello from %1 v%2 pid=%3").arg(client, cver).arg(m_clientPid));
    m_helloDone = true;
    Message ack(QString::fromLatin1(msg::HelloAck), m.id());
    ack.json().insert(QStringLiteral("serviceVersion"), QStringLiteral("1.5.3"));
    ack.json().insert(QStringLiteral("caps"),
        QJsonArray{ QStringLiteral("vpn"), QStringLiteral("camouflage") });
    send(ack);
}

void IpcSession::onConnect(const Message& m)
{
    if (m_engine) {
        sendError(m.id(), err::Busy, QStringLiteral("a VPN connection is already active"));
        return;
    }
    const Profile p = Profile::fromJson(m.json().value(QStringLiteral("profile")).toObject());
    svc::log::info(QStringLiteral("connect: %1").arg(p.redactedSummary()));
    m_engine = new VpnEngine(p, this);
    m_engineThread = std::thread(&IpcSession::runEngine, this);
}

void IpcSession::onDisconnect(const Message&)
{
    svc::log::info(QStringLiteral("onDisconnect: engine=%1").arg(m_engine ? QStringLiteral("active") : QStringLiteral("none")));
    if (m_engine)
        m_engine->cancel();
    else
        onState(QStringLiteral("disconnected"), QStringLiteral("no active connection"));
}

void IpcSession::onStatus(const Message& m)
{
    Message s(QString::fromLatin1(msg::State), m.id());
    s.json().insert(QStringLiteral("state"), m_state);
    send(s);
    if (m_engine)
        m_engine->requestStats();
}

void IpcSession::onPromptResponse(const Message& m)
{
    const quint64 pid = static_cast<quint64>(m.json().value(QStringLiteral("promptId")).toDouble(0));
    QMutexLocker lock(&m_pendingMutex);
    auto it = m_pending.find(pid);
    if (it == m_pending.end())
        return;
    Pending* pp = it.value();
    QMutexLocker plock(&pp->mutex);
    pp->ok = m.json().value(QStringLiteral("ok")).toBool(false);
    pp->response = m.json();
    pp->done = true;
    pp->cv.wakeAll();
}

/* ---- EngineHost: one-way (engine thread -> queued to main) ------------ */

void IpcSession::onLog(int level, const QString& msg)
{
    Message m(QString::fromLatin1(msg::Log));
    m.json().insert(QStringLiteral("level"), level);
    m.json().insert(QStringLiteral("source"), QStringLiteral("openconnect"));
    m.json().insert(QStringLiteral("msg"), msg);
    postSend(m);
}

void IpcSession::onState(const QString& state, const QString& detail)
{
    Message m(QString::fromLatin1(msg::State));
    m.json().insert(QStringLiteral("state"), state);
    if (!detail.isEmpty())
        m.json().insert(QStringLiteral("detail"), detail);
    const QByteArray line = m.toLine();
    QMetaObject::invokeMethod(this, [this, state, line]() {
        m_state = state;
        sendQueued(line);
    }, Qt::QueuedConnection);
}

void IpcSession::onStats(quint64 rx, quint64 tx, const QString& cstp, const QString& dtls)
{
    Message m(QString::fromLatin1(msg::Stats));
    m.json().insert(QStringLiteral("rxBytes"), static_cast<double>(rx));
    m.json().insert(QStringLiteral("txBytes"), static_cast<double>(tx));
    m.json().insert(QStringLiteral("cstpCipher"), cstp);
    m.json().insert(QStringLiteral("dtlsCipher"), dtls);
    postSend(m);
}

void IpcSession::onIpInfo(const QString& addr, const QString& netmask,
                          const QString& addr6, const QString& dns)
{
    Message m(QString::fromLatin1(msg::IpInfo));
    m.json().insert(QStringLiteral("addr"), addr);
    m.json().insert(QStringLiteral("netmask"), netmask);
    m.json().insert(QStringLiteral("addr6"), addr6);
    m.json().insert(QStringLiteral("dns"), dns);
    postSend(m);
}

void IpcSession::onPersistTrust(const QString& hash, const QString& derBase64)
{
    Message m(QString::fromLatin1(msg::Persist));
    m.json().insert(QStringLiteral("what"), QString::fromLatin1(persist::Trust));
    m.json().insert(QStringLiteral("hash"), hash);
    m.json().insert(QStringLiteral("derBase64"), derBase64);
    postSend(m);
}

void IpcSession::onPersistString(const QString& what, const QString& value)
{
    Message m(QString::fromLatin1(msg::Persist));
    m.json().insert(QStringLiteral("what"), what);
    m.json().insert(QStringLiteral("value"), value);
    postSend(m);
}

/* ---- EngineHost: blocking prompts (engine thread) --------------------- */

bool IpcSession::awaitPrompt(const QString& kind, QJsonObject body, QJsonObject* respOut)
{
    quint64 id;
    Pending* pp = new Pending;
    {
        QMutexLocker l(&m_pendingMutex);
        id = ++m_promptSeq;
        m_pending.insert(id, pp);
    }

    Message m(QString::fromLatin1(msg::Prompt));
    m.json().insert(QStringLiteral("promptId"), static_cast<double>(id));
    m.json().insert(QStringLiteral("kind"), kind);
    for (auto it = body.begin(); it != body.end(); ++it)
        m.json().insert(it.key(), it.value());
    postSend(m);

    bool ok;
    {
        QMutexLocker pl(&pp->mutex);
        while (!pp->done)
            pp->cv.wait(&pp->mutex);
        ok = pp->ok;
        if (respOut)
            *respOut = pp->response;
    }
    {
        QMutexLocker l(&m_pendingMutex);
        m_pending.remove(id);
    }
    delete pp;
    return ok;
}

void IpcSession::wakeAllPending(bool ok)
{
    QMutexLocker l(&m_pendingMutex);
    for (Pending* pp : m_pending) {
        QMutexLocker pl(&pp->mutex);
        pp->ok = ok;
        pp->done = true;
        pp->cv.wakeAll();
    }
}

bool IpcSession::askAuthForm(AuthForm& form)
{
    QJsonObject f;
    f.insert(QStringLiteral("banner"), form.banner);
    f.insert(QStringLiteral("message"), form.message);
    f.insert(QStringLiteral("error"), form.error);
    if (form.hasGroup) {
        QJsonArray ch;
        for (const FormChoice& c : form.groupChoices)
            ch.append(QJsonObject{ { QStringLiteral("name"), c.name }, { QStringLiteral("label"), c.label } });
        f.insert(QStringLiteral("authgroup"), QJsonObject{
            { QStringLiteral("name"), form.groupName },
            { QStringLiteral("label"), form.groupLabel },
            { QStringLiteral("current"), form.groupCurrent },
            { QStringLiteral("choices"), ch } });
    }
    QJsonArray opts;
    for (const FormOpt& o : form.opts) {
        QJsonObject oo{ { QStringLiteral("name"), o.name },
                        { QStringLiteral("label"), o.label },
                        { QStringLiteral("type"), o.type } };
        if (!o.choices.isEmpty()) {
            QJsonArray ch;
            for (const FormChoice& c : o.choices)
                ch.append(QJsonObject{ { QStringLiteral("name"), c.name }, { QStringLiteral("label"), c.label } });
            oo.insert(QStringLiteral("choices"), ch);
        }
        opts.append(oo);
    }
    f.insert(QStringLiteral("opts"), opts);

    QJsonObject resp;
    if (!awaitPrompt(QString::fromLatin1(oc::ipc::prompt::AuthForm),
                     QJsonObject{ { QStringLiteral("form"), f } }, &resp))
        return false;

    const QJsonObject fields = resp.value(QStringLiteral("fields")).toObject();
    if (form.hasGroup)
        form.groupValue = fields.value(QStringLiteral("__group__")).toString();
    for (FormOpt& o : form.opts)
        o.value = fields.value(o.name).toString();
    return true;
}

bool IpcSession::askCert(const QString& reason, const QString& host, const QString& hash,
                         const QString& details, const QString& change)
{
    return awaitPrompt(QString::fromLatin1(oc::ipc::prompt::Cert), QJsonObject{
        { QStringLiteral("reason"), reason }, { QStringLiteral("host"), host },
        { QStringLiteral("hash"), hash }, { QStringLiteral("details"), details },
        { QStringLiteral("change"), change } }, nullptr);
}

bool IpcSession::askBanner(const QString& banner)
{
    return awaitPrompt(QString::fromLatin1(oc::ipc::prompt::Banner),
                       QJsonObject{ { QStringLiteral("banner"), banner } }, nullptr);
}

bool IpcSession::askPin(const QString& tokenUrl, const QString& tokenLabel,
                        unsigned flags, QString& pinOut)
{
    QJsonObject resp;
    if (!awaitPrompt(QString::fromLatin1(oc::ipc::prompt::Pin), QJsonObject{
            { QStringLiteral("tokenUrl"), tokenUrl }, { QStringLiteral("tokenLabel"), tokenLabel },
            { QStringLiteral("flags"), static_cast<double>(flags) } }, &resp))
        return false;
    pinOut = resp.value(QStringLiteral("value")).toString();
    return true;
}

/* ---- engine thread driver --------------------------------------------- */

void IpcSession::runEngine()
{
    if (!m_engine->setup()) {
        onState(QStringLiteral("error"), m_engine->lastError());
        onState(QStringLiteral("disconnected"), QString());
    } else {
        m_engine->run();
    }
    /* Tear down libopenconnect HERE, on the worker thread (mirrors the original
     * GUI's `delete vpninfo` right after mainloop()). vpninfo_free runs the
     * reason=disconnect script + closes Wintun and can block; doing it on the
     * worker thread keeps the service event loop responsive. */
    m_engine->teardown();
    svc::log::info(QStringLiteral("runEngine: teardown done; posting finalizeEngine"));
    QMetaObject::invokeMethod(this, "finalizeEngine", Qt::QueuedConnection);
}

void IpcSession::finalizeEngine()
{
    /* Worker has already torn down openconnect; just reap the thread + object. */
    if (m_engineThread.joinable())
        m_engineThread.join();
    delete m_engine;
    m_engine = nullptr;
    m_state = QStringLiteral("idle");
    svc::log::info(QStringLiteral("finalizeEngine: done; state=idle"));
}
