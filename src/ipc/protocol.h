/*
 * openconnect-gui — privilege-separation IPC contract (protocol v1)
 *
 * Shared by openconnect-gui (unprivileged client) and openconnect-gui-service
 * (LocalSystem server). Transport: local named pipe, newline-delimited UTF-8 JSON.
 * See docs/architecture/privilege-separation.md.
 *
 * This file is part of openconnect-gui. GPLv2-or-later (see LICENSE.txt).
 */
#pragma once

#include <QJsonObject>
#include <QString>
#include <cstdint>

namespace oc::ipc {

/* Bump MAJOR when the wire format changes incompatibly. hello-ack advertises caps[]. */
inline constexpr int kProtocolVersion = 1;

/* Service-created pipe. QLocalServer name "openconnect-gui/svc" maps to
 * \\.\pipe\openconnect-gui\svc on Windows. */
inline constexpr char kPipeName[] = "openconnect-gui/svc";

/* Explicit security descriptor applied to the native pipe handle:
 * full control for SYSTEM + Administrators, connect for INTERACTIVE. */
inline constexpr wchar_t kPipeSddl[] = L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)";

/* ---- Message types (envelope "type" field) ---------------------------- */
namespace msg {
/* client -> service */
inline constexpr char Hello[]          = "hello";
inline constexpr char Connect[]        = "connect";
inline constexpr char Disconnect[]     = "disconnect";
inline constexpr char Status[]         = "status";
inline constexpr char PromptResponse[] = "prompt-response";
/* service -> client */
inline constexpr char HelloAck[]       = "hello-ack";
inline constexpr char State[]          = "state";
inline constexpr char Log[]            = "log";
inline constexpr char Stats[]          = "stats";
inline constexpr char IpInfo[]         = "ipinfo";
inline constexpr char Prompt[]         = "prompt";
inline constexpr char Persist[]        = "persist";
inline constexpr char Connected[]      = "connected";
inline constexpr char Disconnected[]   = "disconnected";
inline constexpr char Error[]          = "error";
} // namespace msg

/* ---- prompt kinds (Prompt.kind) --------------------------------------- */
namespace prompt {
inline constexpr char AuthForm[] = "auth-form";
inline constexpr char Cert[]     = "cert";
inline constexpr char Banner[]   = "banner";
inline constexpr char Pin[]      = "pin";
} // namespace prompt

/* ---- persist kinds (Persist.what) ------------------------------------- */
namespace persist {
inline constexpr char Trust[]    = "trust";     // newly accepted gtdb pin
inline constexpr char Username[] = "username";
inline constexpr char Password[] = "password";
inline constexpr char Groupname[]= "groupname";
inline constexpr char Token[]    = "token";     // updated stoken/hotp seed
} // namespace persist

/* ---- connection state (State.state) ----------------------------------- */
enum class ConnState {
    Idle,
    Connecting,
    Authenticating,
    ObtainingCookie,
    Cstp,
    SetupTun,
    Connected,
    Reconnecting,
    Disconnecting,
    Disconnected,
    Error,
};
QString  toString(ConnState s);
ConnState connStateFromString(const QString& s);

/* ---- Message: thin typed wrapper over the JSON envelope --------------- *
 * Construction stamps v=kProtocolVersion and type. Requests carry a nonzero
 * id; responses/events echo it (or 0). Body fields live alongside the
 * envelope keys. Serialization/framing live in oc-ipc (protocol.cpp, P1).   */
class Message {
public:
    Message() = default;
    explicit Message(QJsonObject obj) : m_obj(std::move(obj)) {}
    Message(const QString& type, quint64 id = 0);

    QString  type() const;
    int      version() const;
    quint64  id() const;
    bool     isValid() const;          // has v + non-empty type
    bool     versionCompatible() const; // same major as kProtocolVersion

    QJsonObject& json()             { return m_obj; }
    const QJsonObject& json() const { return m_obj; }

    /* newline-framed serialization (one object per line, '\n'-terminated) */
    QByteArray toLine() const;
    static Message fromLine(const QByteArray& line, bool* ok = nullptr);

private:
    QJsonObject m_obj;
};

/* error codes (Error.code) */
namespace err {
inline constexpr char Busy[]            = "busy";            // tunnel already active
inline constexpr char BadVersion[]      = "bad-version";
inline constexpr char Unauthorized[]    = "unauthorized";
inline constexpr char AuthFailed[]      = "auth-failed";
inline constexpr char Internal[]        = "internal";
} // namespace err

} // namespace oc::ipc
