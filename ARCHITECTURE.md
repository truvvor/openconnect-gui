# Keenetic Anti-DPI VPN — openconnect-gui fork architecture

Branch: **`keenetic-camouflage`**
Fork of: [openconnect/openconnect-gui](https://github.com/openconnect/openconnect-gui) (v1.5.3)
Target: **Windows 10/11** only (other platforms dropped from this fork).

This fork drops compatibility with stock Cisco AnyConnect servers and is
exclusively a client for our Keenetic-router anti-DPI VPN stack:
patched `ocserv` server (camouflage Level 2, HMAC-derived CSTP magic, header /
cookie / tunnel-URL rewriting, JA3-shaped TLS, TCP scatter on ClientHello).

## Goals

1. **Full protocol compatibility** with our patched ocserv (server is on
   KN-3710 at gk-msk03.netcraze.pro / gk-msk04.netcraze.pro).
2. **TUN without per-launch UAC** on Windows. WinTun + one-shot privileged
   helper service install.
3. Single-file MSI installer. End user does **not** need to touch
   gnutls/openconnect/libxml2 manually.
4. Cosmetic minimum-surface GUI: profile name, server URL, camouflage-secret,
   tunnel-url, user/password. No certificate auth, no PKCS#11, no TPM,
   no SecurID token UI.

## Component map

```
openconnect-gui/  (this repo, fork)
├── external/
│   └── openconnect-keenetic/         (NEW — git submodule)
│       └── ... infradead/openconnect 9.12 + our 3 patches applied
├── src/                              (Qt GUI, simplified)
│   ├── main.cpp                      keep (drop PKCS#11 PIN callback, drop macOS setuid)
│   ├── dialog/mainwindow.{cpp,h,ui}  trim Anti-DPI defaults, drop cert/token panes
│   ├── dialog/editdialog.{cpp,h,ui}  add fields: camouflage-secret, tunnel-url
│   ├── dialog/NewProfileDialog.{cpp,h,ui}  unchanged for now
│   ├── dialog/logdialog.{cpp,h,ui}   keep as-is
│   ├── vpninfo.{cpp,h}               rework: drop cert auth, add camouflage-secret wiring, replace setup_tun_vfn with WinTun-service RPC
│   ├── server_storage.{cpp,h}        extend schema: camouflage_secret, tunnel_url (default /api/v1/session)
│   ├── wintun_client.{cpp,h}         NEW — named-pipe RPC to wintun-service
│   ├── cert.cpp / key.cpp / keypair.cpp / gtdb.cpp / cryptdata.cpp / MyCertMsgBox.{cpp,h,ui}   REMOVE (no per-user cert auth)
│   └── images/, Resources/           rebrand to Keenetic Anti-DPI VPN
├── wintun-service/                   NEW — privileged Windows service
│   ├── service.cpp / service.h
│   ├── wintun_adapter.cpp/h          uses WinTun.dll (Wireguard's user-mode driver)
│   ├── pipe_server.cpp/h             named-pipe \\.\pipe\KeeneticVpnService
│   ├── installer.cpp/h               sc create / driver staging
│   └── CMakeLists.txt
├── installer/                        NEW — WiX MSI
│   ├── product.wxs
│   ├── bundle.wxs                    optional: bootstrapper for VC++ redist
│   └── CMakeLists.txt
├── CMake/Modules/FindKeeneticOpenConnect.cmake   NEW
├── CMake/Includes/ProjectExternals_openconnect.cmake   REWRITE — build our submodule via cross-compile MinGW64
├── CMake/Includes/ProjectExternals_wintun.cmake        NEW — fetch wintun-0.14.1.zip
├── nsis/                             REMOVE (replaced by WiX)
├── ARCHITECTURE.md                   this file
└── README.md                         rewrite for the fork
```

## Patches we are vendoring

Source: `mike@letta.gk.company:/home/mike/keenetic-ci/patches/openconnect/`

| patch | applies to | what it adds |
|---|---|---|
| `100-camouflage.patch`     | auth.c, cstp.c, main.c, openconnect-internal.h | `camouflage_secret` field, `--camouflage-secret <s>` option, HMAC-SHA256 CSTP magic derivation, `Cookie: session=` (vs webvpn=), `X-S-*` (vs X-CSTP-*), `X-D-*` (vs X-DTLS-*), `CONNECT /api/v1/session`, XML root `auth-request` (vs config-auth), suppress `X-Transcend-Version` |
| `200-xray-scatter.patch`   | gnutls.c | TLS push-function override: first ClientHello write split into 1-3 byte leading fragment + 64-256 byte chunks → defeats SNI-in-first-segment DPI |
| `300-keenetic-passwd.patch`| main.c | `--passwd <pw>` / `-w` option; non-interactive password reuse (no zeroing) |

These three patches plus a bundled `ca-certificates.crt` (Let's Encrypt R10-R14
+ E5-E9 + ISRG X1/X2) constitute the entire client-side protocol.

## Server endpoint expectations

The GUI talks to **our** ocserv build, NOT vanilla AnyConnect.

| field | default | source |
|---|---|---|
| Server URL    | `https://gk-msk04.netcraze.pro/api/v1/session` | per-profile |
| Camouflage secret | `FCuA1B86sXsITTC2Yb5YHnfutaiijfWC` (devel) | per-profile, stored encrypted |
| Username / Password | (empty) | per-profile |
| CA file | bundled `ca-certificates.crt` (LE-only, 16 KB) | shipped in installer |
| Server cert pin | **none** (cafile-only, B50) | — |
| DTLS | **disabled** (`no-dtls` always set) | — |

When the camouflage-secret is empty the GUI refuses to connect — this fork
is single-protocol.

## TUN-without-UAC architecture (WinTun + helper service)

### Why a service

WinTun (WireGuard's user-mode TUN driver for Windows) requires
`Administrators` group to **create** a virtual adapter
(`WintunCreateAdapter`), but once created the adapter handle can be
passed to an unprivileged process. WireGuard solves this with a
**service running as LocalSystem** that owns the adapter and ferries
packets to/from user-mode clients over a named pipe.

We do the same:

```
+----------------------+      named pipe         +-----------------------+
|  openconnect-gui     |  <-------------------> |  KeeneticVpnService    |
|  (Medium IL, user)   |   \\.\pipe\KeeneticVpn |  (LocalSystem)         |
|                      |                        |                        |
| - Qt GUI             |                        | - WinTun adapter       |
| - libopenconnect     |                        | - Routing table edits  |
| - reads packets from |                        | - DNS push             |
|   tun_fd (from svc)  |                        | - Owns WinTun.dll      |
+----------------------+                        +-----------------------+
                                                            |
                                                            v
                                                +-----------------------+
                                                |  WinTun kernel driver |
                                                |  (signed, staged once)|
                                                +-----------------------+
```

### Installation flow (one-time, UAC prompt once)

MSI installer (elevated):

1. Copies `KeeneticVpnService.exe` to `%ProgramFiles%\Keenetic VPN\`.
2. `sc create KeeneticVpnService binPath= … start= demand`.
3. Stages `wintun.cat` + `wintun.inf` into Driver Store
   via `WintunCreateAdapter` first call (or PnpUtil).
4. Adds the installing user to a local group `KeeneticVPNUsers` (so
   future logins inherit pipe ACL access).
5. Sets pipe DACL: `(A;;GA;;;BA)(A;;GRGW;;;S-1-5-21-…-KeeneticVPNUsers)`.

### Runtime flow (per connect)

User launches `openconnect-gui.exe` from Start Menu (no UAC):

1. GUI auto-starts service via `StartService()` (allowed for user
   if SCM ACL grants `SERVICE_START` to `KeeneticVPNUsers` — set in installer).
2. GUI opens `\\.\pipe\KeeneticVpnService`, sends `RPC_CONNECT { profile_id }`.
3. Service:
   - calls `WintunCreateAdapter("KeeneticVPN", "Wintun", ...)` (first time)
     or `WintunOpenAdapter("KeeneticVPN")` (subsequent),
   - starts session, gets `HANDLE` to packet-ring,
   - returns to GUI: tunnel adapter LUID, ring shared-memory section handle.
4. GUI's libopenconnect uses our **custom `setup_tun_handler` callback**
   which:
   - skips `openconnect_setup_tun_device` (the one that calls
     `tap-windows6.sys` and requires admin),
   - reads tunnel adapter metadata from the service,
   - hands openconnect a Win32 socketpair where both ends pipe
     packets into the service's WinTun ring,
5. Service applies routes / DNS / MTU on behalf of GUI via
   `IPHelper` API (`CreateIpForwardEntry2`, `SetInterfaceDnsSettings`).

### Service IPC protocol (named pipe)

Length-prefixed JSON over `\\.\pipe\KeeneticVpnService`:

```
{ "op": "connect",    "profile_id": "uuid", "adapter_name": "KeeneticVPN" }
{ "op": "set_routes", "routes": ["10.0.0.0/8","172.16.0.0/12"] }
{ "op": "set_dns",    "dns": ["10.0.0.53"], "domains": ["lan."] }
{ "op": "send_packet", "data": "<base64>" }   /* if not using shared-mem ring */
{ "op": "disconnect" }
{ "op": "status" }
```

For performance the actual data plane uses a WinTun-native
shared-memory ring (no JSON overhead); JSON is only for control.

## Build chain

| stage | tool | output |
|---|---|---|
| Vendor openconnect | `external/openconnect-keenetic` (submodule, infradead 9.12 + our 3 patches applied via 3 `git am`s on first clone) | source tree |
| Cross-build libopenconnect | WSL Ubuntu-22.04 → MinGW64 toolchain → `mingw-w64-x86_64-{gnutls,nettle,gmp,libxml2,p11-kit,pkg-config}` | `libopenconnect-5.dll`, `openconnect.exe`, headers |
| Bundle into source tree | CMake `ExternalProject_Add` consumes the WSL-built artifacts at `${CMAKE_SOURCE_DIR}/external/openconnect-keenetic_mingw64.zip` | available to GUI |
| Build GUI | MinGW-w64 + Qt 5.15 LTS (kept from upstream — Qt6 would force a port we don't need) | `openconnect-gui.exe` |
| Build service | MinGW-w64 (no Qt) | `KeeneticVpnService.exe` |
| Sign & package | `signtool` (user provides EV cert; bundled bootstrap uses self-signed for dev) → WiX → MSI | `Keenetic-VPN-x.y.z-x64.msi` |

WSL build helper script: `scripts/build-libopenconnect-keenetic.sh` —
runs `git clone infradead/openconnect → git am 100,200,300 → ./configure
--host=x86_64-w64-mingw32 --with-gnutls --without-openssl
--without-stoken --without-libpcsclite --with-vpnc-script=vpnc-script-win.js
--enable-shared --disable-static → make → strip → zip`.

## Removed from upstream openconnect-gui

- macOS support (entire `Resources/`, `+mac/`, `Q_OS_MACOS` branches)
- PKCS#11 / smartcard support (`PROJ_PKCS11`, `pin_callback`, gnutls/pkcs11.h)
- Certificate auth UI (`cert.cpp`, `key.cpp`, `keypair.cpp`,
  `MyCertMsgBox.{cpp,h,ui}`, `cryptdata.cpp`)
- TOFU/gtdb cert pinning (`gtdb.cpp`) — replaced by bundled CA file
- TPM / hardware token UI
- AppVeyor CI (`appveyor.yml`) — replaced with GitHub Actions Windows runner
- NSIS installer (`nsis/`) — replaced with WiX
- `bundle/` directory contents (will replace with our LE bundle)
- DTLS UI (no-dtls is forced)

Net code-size reduction: ~40 % LOC.

## Added by this fork

- `external/openconnect-keenetic/` — submodule (infradead/openconnect 9.12 + 3 patches as commits on a `keenetic-camouflage` branch)
- `wintun-service/` — privileged Win32 service
- `installer/` — WiX MSI
- `scripts/build-libopenconnect-keenetic.sh` — WSL cross-compile helper
- `src/wintun_client.{cpp,h}` — named-pipe RPC client
- `src/dialog/editdialog.ui` — new fields: camouflage-secret (password mask), tunnel-url
- `bundle/ca-certificates.crt` — 16 KB LE-only bundle (rolled in MSI)
- `.github/workflows/windows.yml` — GitHub Actions build + signing

## Repo branches

- `master` / `develop`     — upstream tracking, **no commits from us** (kept clean for upstream merges if ever needed)
- `keenetic-camouflage`    — our work happens here; PRs target this branch
- `release-1.0`            — tagged release branches as we ship

## Architectural decisions (locked 2026-05-19)

| # | Decision | Status |
|---|---|---|
| Q1 | **Qt 5.15 LTS** | locked |
| Q2 | **MinGW-w64** everywhere — GUI, service, libopenconnect.dll (one toolchain end-to-end) | locked |
| Q3 | **Self-signed cert for dev**; production CI signs via `signtool` when env-vars `KEENETIC_SIGN_PFX` + `KEENETIC_SIGN_PASS` are set (EV cert path) | locked |
| Q4 | **Tray icon + main window** — QSystemTrayIcon with Connect / Disconnect / Status / Quit menu; close-window minimises to tray | locked |
| Q5 | **Auto-reconnect ON by default** — 30 s interval, up to 10 attempts, then red Disconnected state. Mirrors `patches/ocserv-files/ocserv-wrapper.c` v14 background-refresh philosophy. | locked |

## Phases

1. **Phase 0** ✅ DONE (commit 663e313). Architecture doc, Q1-Q5 locked.
2. **Phase 1** ✅ DONE. Vendored openconnect 9.12 + 3 keenetic patches build
   under MinGW-w64 from WSL. `external/openconnect-keenetic_mingw64.zip`
   produced — libopenconnect-5.dll (257 KB stripped) + all GnuTLS/libxml2/
   nettle deps bundled (4.6 MB total zip). Build script:
   `scripts/build-libopenconnect-keenetic.sh`.
   - source bootstrap: `external/openconnect-9.12-keenetic.tar.gz` (8 MB) is
     a snapshot from `letta:/data/keenetic-sdk-kn3010/build_dir/.../openconnect-9.12-2`
     where the 3 patches are already applied. The script regenerates autotools
     (libtoolize + autoreconf), cross-builds with mingw-w64 and packages.
   - in-tree fix needed for `gnutls.c`: `#include <netinet/tcp.h>` was wrapped
     in `#ifdef _WIN32` (winsock2.h / ws2tcpip.h on Windows). Patch refresh
     for `200-xray-scatter.patch` recommended (TODO).
   - `main.c` (openconnect.exe CLI) doesn't build under mingw because our
     `300-keenetic-passwd.patch` references POSIX syslog (`openlog`/`LOG_DAEMON`).
     We don't need CLI — GUI links directly with the DLL. When/if the CLI
     becomes needed, add a `400-mingw-cli.patch` wrapping syslog in `#ifndef _WIN32`.
   - **Phase 1 follow-up (deferred to Phase 2):** add a public-API setter
     `openconnect_set_camouflage_secret(vpninfo, secret)` in `library.c` +
     `openconnect.h` (4th patch `400-public-camouflage-api.patch`). Currently
     the field exists internally but the GUI cannot set it without either CLI
     parsing or struct-poking. Phase 2 will add this cleanly.
3. **Phase 2**. Rip out cert/token/PKCS#11 code; add camouflage-secret
   and tunnel-url to profile schema + editdialog. Wire `vpninfo.cpp`
   to call `openconnect_set_camouflage_secret`. Plus the 400-public-api
   patch above.
4. **Phase 3**. `wintun-service` project: skeleton, install/uninstall,
   pipe server, WinTun adapter create/destroy, route table edits.
5. **Phase 4**. Replace `setup_tun_vfn` in `vpninfo.cpp` with RPC call to
   the service. Add reconnect / status / disconnect flow.
6. **Phase 5**. WiX MSI: bundles GUI exe, service exe, WinTun driver,
   LE bundle, Qt deps, GnuTLS DLLs. Registers service with ACL.
7. **Phase 6**. GitHub Actions: matrix build (debug/release), sign,
   upload MSI as artifact. Tag → release.
8. **Phase 7**. Live test against KN-3710 server in MSK. Document
   troubleshooting steps.

Each phase ends with a tag (`phase-1`, `phase-2`, …) so we can roll back.

---

Last updated: 2026-05-19 (initial draft).
