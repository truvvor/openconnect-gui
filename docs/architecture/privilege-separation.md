# Privilege Separation: Unprivileged GUI + Privileged Service

Status: **Accepted** (design) — 2026-05-29
Branch: `ci/privilege-separation`
Supersedes: the upstream "embed `requireAdministrator` manifest, run the whole GUI elevated" model.

## 1. Context / problem

Today `openconnect-gui.exe` requires Administrator rights **on every launch**. This is by
upstream design, not a build accident:

- `src/openconnect-gui.exe.manifest` declares `requestedExecutionLevel level="requireAdministrator"`.
- `src/openconnect-gui.rc.in:4` embeds that manifest as an `RT_MANIFEST` resource, gated by `@UAC_FLAG@`.
- `CMake/Includes/git_revision_generate.cmake:46-51` sets `UAC_FLAG=""` when `PROJ_ADMIN_PRIV_ELEVATION` is ON.
- `CMake/Includes/ProjectSettings.cmake:3` defaults `PROJ_ADMIN_PRIV_ELEVATION` **ON** ("don't turn it off in production!!").
- Runtime self-elevation (`main.cpp` `relaunch_as_root()`) exists **only for `Q_OS_MACOS`**; on Windows there is none.

The GUI needs admin because it hosts `libopenconnect` **in-process** (`src/vpninfo.cpp`) and performs
privileged operations directly: opening the Wintun adapter, running `vpnc-script-win.js`, editing the
routing table and DNS. Turning the manifest off alone removes the UAC prompt but breaks the VPN, because
the unprivileged process can no longer do any of that.

There is **no service / privilege-separation code** anywhere in the repo (verified: clean git history,
no service/IPC sources). Previous "make it a service" attempts left no trace and could not have worked,
because nothing delegated the privileged work off the GUI process.

## 2. Decision

Split into two processes with an IPC boundary:

| Decision | Choice |
|---|---|
| VPN engine location | **`libopenconnect` in-process inside the service** (no child `openconnect.exe`) |
| Service runtime | **QtCore** (`QLocalServer` + `QProcess` + `QJsonDocument`) |
| IPC transport | **Named pipe**, newline-delimited JSON |
| Authorization | **Any interactive user** may control the service (pipe DACL grants `INTERACTIVE`) |
| GUI manifest | `asInvoker` — built with `-DPROJ_ADMIN_PRIV_ELEVATION=OFF` |
| Admin needed | **once, at install time** (service registration), never at GUI launch |

## 3. Target architecture

```
  user session (unprivileged, asInvoker)          Session 0 (LocalSystem, auto-start)
 ┌─────────────────────────────────────┐        ┌──────────────────────────────────────┐
 │ openconnect-gui.exe                  │        │ openconnect-gui-service.exe          │
 │  - profile store (QSettings, DPAPI)  │        │  - SCM bootstrap (install/run)       │
 │  - ServiceClient (QLocalSocket)      │  pipe  │  - QLocalServer  \\.\pipe\…\svc      │
 │  - dialogs (auth/cert/banner/pin)    │◀──────▶│  - IpcSession (per client)           │
 │    now driven by `prompt` events     │  JSON  │  - VpnEngine (libopenconnect in-proc)│
 │  - connection log / stats view       │        │    Wintun, vpnc-script, routes, DNS  │
 └─────────────────────────────────────┘        └──────────────────────────────────────┘
            no admin rights                              has the privileges
```

- **`openconnect-gui-service.exe`** — Windows service, `LocalSystem`, `Start=Auto`. Registered once with
  admin by the installer. Owns the VPN engine and all privileged network operations. Idle until a client
  connects. Holds **no persistent secrets**.
- **`openconnect-gui.exe`** — unprivileged front-end. Owns the profile store, decrypts secrets from the
  user's DPAPI-protected `QSettings` at connect time, and renders all UI. Talks to the service over the pipe.
- **`oc-ipc`** — small QtCore static library shared by both: protocol message types, (de)serialization, framing.

## 4. Callback inventory → IPC mapping

Every `libopenconnect` callback currently registered in `src/vpninfo.cpp` is classified by whether it
needs the user. Interactive ones today call `MainWindow` / `MyInputDialog` / `MyCertMsgBox` **in-process**;
after the split they fire **inside the service** and must be marshalled to the GUI as `prompt` events.

| Callback (vpninfo.cpp) | Today | After split |
|---|---|---|
| `process_auth_form` | `MyInputDialog` (group/user/pass) | `prompt{kind:auth-form}` → GUI → `prompt-response` |
| `validate_peer_cert` | `MyCertMsgBox` + gtdb pin store | `prompt{kind:cert}`; on accept → `persist{what:trust}` |
| `logVpncScriptOutput` banner | `MyMsgBox` (unless auto-accept) | `prompt{kind:banner}` only when `autoAcceptBanner=false` |
| `pin_callback` (main.cpp, PKCS#11) | `MyInputDialog` password | `prompt{kind:pin}` → GUI → `prompt-response` |
| `progress_vfn` | `Logger` in-proc | `log` event (one-way) |
| `stats_vfn` → `updateStats` | in-proc | `stats` event (one-way) |
| `lock_token_vfn`/`unlock_token_vfn` | reads/writes `ss` token | token config in `connect`; new seed → `persist{what:token}` |
| `setup_tun_vfn` | privileged TUN setup | runs in service, no IPC (privileged side) |
| ip info (`get_info`) | read in-proc | `ipinfo` event on connect |

State that lives in `StoredServer`/`QSettings` today (username, password, groupname, cert/key/ca,
camouflage secret, token seed, **gtdb trusted pubkeys**) stays **owned by the GUI**. The GUI ships a fully
resolved profile in `connect`; the service keeps it only in memory for the connection's lifetime, and
echoes back anything newly learned via `persist` events so the GUI can write it to DPAPI `QSettings`.

## 5. IPC protocol (v1)

- **Transport:** Windows named pipe `\\.\pipe\openconnect-gui\svc`, created by the service via
  `QLocalServer`. Local only (`PIPE_REJECT_REMOTE_CLIENTS`).
- **Framing:** one UTF-8 JSON object per line, terminated by `\n`. No embedded raw newlines (escaped by JSON).
- **Envelope:** every message has `v` (protocol version, int), `type` (string). Client requests carry a
  monotonically increasing `id` (uint64); responses/events echo the originating `id` or use `0`.
- **Roles:** GUI = client, service = server. After handshake, the service may push events unsolicited.
- **Versioning:** service rejects a client whose major `v` differs; `hello-ack` advertises `caps[]`.

### 5.1 Client → service

| type | payload | meaning |
|---|---|---|
| `hello` | `client, clientVersion, pid` | open session; expects `hello-ack` |
| `connect` | `profile{…}` (see §5.3) | start a VPN connection |
| `disconnect` | — | tear down the active connection |
| `status` | — | request a `status` snapshot |
| `prompt-response` | `promptId, ok, fields` | answer a `prompt` event |

### 5.2 Service → client

| type | payload | meaning |
|---|---|---|
| `hello-ack` | `serviceVersion, caps[]` | handshake accepted |
| `state` | `state, detail` | state-machine transition (§6) |
| `log` | `level, source, msg` | replaces in-proc `progress_vfn`/`Logger` |
| `stats` | `rxBytes, txBytes, cstpCipher, dtlsCipher` | replaces `stats_vfn` |
| `ipinfo` | `addr, netmask, addr6, netmask6, dns[]` | assigned tunnel addressing |
| `prompt` | `promptId, kind, …` | needs user input (kinds: `auth-form`, `cert`, `banner`, `pin`) |
| `persist` | `what, …` | GUI should save: `trust`/`username`/`password`/`groupname`/`token` |
| `connected` | — | `connect` succeeded; mainloop running |
| `disconnected` | `reason` | connection ended |
| `error` | `code, message` | request failed / fatal engine error |

### 5.3 `connect.profile` (resolved by the GUI, secrets in plaintext over the local pipe)

```jsonc
{
  "name": "gk-msk03",
  "server": "gk-msk03.netcraze.pro",
  "protocol": "anyconnect",
  "camouflageSecret": "…",            // decrypted by GUI from DPAPI QSettings
  "disableUdp": true,
  "autoAcceptBanner": true,
  "reconnectTimeout": 300,
  "dtlsReconnectTimeout": 60,
  "reportedOs": "win",
  "username": "",                      // optional pre-fill; empty ⇒ prompt
  "password": "",                      // optional pre-fill; empty ⇒ prompt
  "groupname": "",                     // optional authgroup pre-select
  "clientCertPem": null,               // bytes, not a path (see §8.3)
  "clientKeyPem": null,
  "caCertPem": null,                   // null ⇒ service falls back to bundled LE CA
  "tokenMode": "none",                 // none|totp|hotp|stoken
  "tokenSecret": null,
  "trust": [ { "hash": "sha256:…", "derBase64": "…" } ]  // gtdb pinned certs for this server
}
```

### 5.4 `prompt` kinds

- `auth-form`: `{ banner, message, error, authgroup:{name,label,current,choices:[{name,label}]},
  opts:[{name,label,type:"text|password|select|hidden", choices?:[{name,label}]}] }`.
  Response: `fields:{ "<optName>":"<value>", "__group__":"<choiceName>" }`.
- `cert`: `{ reason, host, hash, details, change:"unknown|key-mismatch" }`.
  Response: `ok:true|false`. On `ok` the service stores the pin and emits `persist{what:trust}`.
- `banner`: `{ banner }`. Response: `ok` (accept/disconnect). Sent only when `autoAcceptBanner=false`.
- `pin`: `{ tokenUrl, tokenLabel, flags }`. Response: `value:"<pin>"`.

## 6. Connection state machine (service side)

```
idle ─connect─▶ connecting ─▶ authenticating ─(prompts)─▶ obtaining-cookie ─▶ cstp
   ▲                                                                            │
   │                                                                       setup-tun
   └── disconnected ◀─ disconnecting ◀─ connected ◀── (dtls?) ◀──────────────┘
                            ▲                 │
                            └──── error ◀─────┘  (any failure → error → disconnected)
```

Each transition is reported via a `state` event. **One active tunnel per service instance** (single Wintun
adapter): a second `connect` while active is rejected with `error{code:"busy"}`.

## 7. Security model

- **Pipe DACL.** The pipe is created with an explicit security descriptor (not the default):
  `D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;IU)` — full control for `SYSTEM` and `Administrators`, connect
  (read/write) for `INTERACTIVE`. `QLocalServer::setSocketOptions` plus a native SD applied to the
  underlying pipe handle. This realizes the "any interactive user" decision while denying network and
  service accounts.
- **Local only.** `PIPE_REJECT_REMOTE_CLIENTS`; reject clients whose pipe session ≠ console/RDP interactive.
- **Client identification.** Service calls `GetNamedPipeClientProcessId` and logs the caller's image path.
  Optional hardening (`caps:["verify-peer-image"]`): refuse clients whose image is not the installed,
  correctly-signed `openconnect-gui.exe`.
- **Secrets.** Travel only over the local pipe (a kernel object, never on the wire), only at `connect`
  time. The service never writes them to disk and zeroes buffers after handing them to `libopenconnect`.
  The GUI remains the only component that persists secrets, in DPAPI-encrypted `QSettings`.
- **No ambient elevation.** The GUI is `asInvoker`; compromising it grants only "ask the service to start a
  VPN", not arbitrary SYSTEM code execution — the service's API surface is the small message catalog above.

## 8. File handling across the privilege boundary

- **8.1 vpnc-script + CA bundle.** `setup_tun_vfn` and the CA fallback use
  `QCoreApplication::applicationDirPath()`. In the service that is the **service's** install dir, so the
  installer must place `vpnc-script-win.js` and `ca-certificates.crt` next to the service binary too (not
  only next to the GUI). `%TEMP%\vpnc.log` is written/read in the service's (SYSTEM) temp — consistent
  because the script runs as SYSTEM.
- **8.2 gtdb trust store.** Today `gtdb` reads/writes pinned pubkeys through `StoredServer`/`QSettings` in
  the user hive. The service has no user hive, so trust pins for the target server are passed in
  `connect.profile.trust[]`, and newly accepted pins come back as `persist{what:trust}`.
- **8.3 client cert / key / CA as bytes.** `openconnect_set_client_cert` / `openconnect_set_cafile` take
  **file paths**. A path in the user profile may be unreadable by SYSTEM. The GUI therefore reads the PEM
  bytes and sends them; the service writes them to a `SYSTEM`-only ACL'd temp file under its own data dir,
  passes that path to `libopenconnect`, and deletes it after the handshake.

## 9. Build / CMake

- New `src/ipc/` → static lib **`oc-ipc`** (QtCore only): protocol structs + JSON + framing.
- New target **`openconnect-gui-service`** (`WIN32_EXECUTABLE`, QtCore + the same `openconnect::*` link set
  the GUI uses today) = SCM bootstrap + `QLocalServer` + `VpnEngine` (moved from `vpninfo.cpp`) + `IpcSession`.
- GUI target keeps Qt Widgets but **drops the connect-path `libopenconnect` calls**; adds `ServiceClient`.
  Built with `-DPROJ_ADMIN_PRIV_ELEVATION=OFF` ⇒ `asInvoker`.
- `VpnEngine` is shared source compiled into the service; the GUI no longer compiles `vpninfo.cpp`.

## 10. Installer (NSIS) + CI

- Installer (admin, once): copy both exes; copy `vpnc-script-win.js` + `ca-certificates.crt` beside the
  service; register the service (`Start=Auto`, failure-restart recovery); start it. Uninstall: stop +
  `delete` the service. Wintun driver installed by the service on first adapter creation.
- `build-windows.yml`: build + `fixup_bundle` + package **both** targets; CPack components `App` (GUI) and
  `Service`.

## 11. Phased plan

P0 design (this doc + `src/ipc/protocol.h`) · P1 `oc-ipc` · P2 service skeleton + pipe DACL · P3 extract
`VpnEngine` · P4 wire lifecycle + prompts/persist over IPC · P5 GUI `ServiceClient` + `asInvoker` · P6
NSIS + CI dual-target · P7 E2E vs `gk-msk03` + security review.

## 12. Open risks

- Marshalling `process_auth_form`'s dynamic, multi-round form over IPC (authgroup re-prompt
  `OC_FORM_RESULT_NEWGROUP`) must preserve round-trip semantics.
- `QLocalServer` does not expose a SD parameter directly — we set it on the native pipe handle; verify on
  both x86 and x64 MinGW.
- Service crash must always tear the tunnel down cleanly (no orphaned routes) — recovery + watchdog in P4.
