# Keenetic anti-DPI VPN client

Fork of [openconnect/openconnect-gui](https://github.com/openconnect/openconnect-gui) 1.5.3.

**Branch:** `keenetic-camouflage`
**Status:** working scaffold (Phase 1–6 complete), live test pending (Phase 7).
**License:** GPL v2.

## What it is

A Windows-only Qt client that talks to our patched ocserv server
(KN-3710 router stack) with camouflage Level 2 — HMAC-SHA256 CSTP magic,
header rewriting (X-CSTP-* → X-S-*, X-DTLS-* → X-D-*), cookie session=,
`/api/v1/session` tunnel URL, suppressed `X-Transcend-Version`, plus
xray-style TCP-scatter on the TLS ClientHello.

Designed so that **end users never see UAC at every launch**: a one-time
MSI install registers a privileged service (`KeeneticVpnService`,
LocalSystem) which owns the WinTun adapter. The GUI itself runs as a
normal user and asks the service for a tunnel over a named pipe.

## Features

* **Camouflage toggle** — main window menu has *Connection → Compatibility
  mode (camouflage OFF)* to switch to vanilla AnyConnect for testing
  against stock servers.
* **Import / Export / Edit camouflage config** — `File` menu reads and
  writes `.conf` files with `camouflage-secret =` / `camouflage-tunnel-url`
  /  `server` / `username` lines. Imports applied to the active profile.
* **System tray** — minimise to tray, Connect/Disconnect/Quit from
  context menu, status icon (green = up, red = down).
* **Auto-reconnect** — default 30s × 10 attempts, mirrors the ocserv-wrapper
  v14 server-side philosophy.
* **No per-user PKI** — bundled Let's Encrypt R10–R14 + E5–E9 + ISRG X1/X2
  trust store; certificate validation is chain-only, no TOFU pin.

## Layout

```
.
├── ARCHITECTURE.md                 — full design document (read this first)
├── README.md                       — this file
├── CMakeLists.txt                  — root: GUI + service + installer
├── src/                            — Qt GUI (about 12 .cpp/.h + 4 .ui)
├── wintun-service/                 — privileged Win32 service
│   ├── service.cpp                 — SCM dispatcher + install/uninstall
│   ├── pipe_server.cpp             — named-pipe JSON protocol
│   └── wintun_adapter.cpp          — wintun.dll dynamic loader
├── installer/                      — WiX MSI
│   ├── product.wxs                 — main installer manifest
│   └── README.md                   — build/sign instructions
├── patches/openconnect-keenetic/   — 4 patches vendored against openconnect 9.12
│   ├── 100-camouflage.patch
│   ├── 200-xray-scatter.patch
│   ├── 300-keenetic-passwd.patch
│   └── 400-public-camouflage-api.patch  (applied in-place by build script)
├── scripts/build-libopenconnect-keenetic.sh  — WSL/MinGW cross-compile
├── docs/testing.md                 — Phase 7 manual test checklist
└── .github/workflows/windows.yml   — Windows CI (MinGW + WiX MSI)
```

## Build (from scratch)

Prereqs:

* Windows 10/11 + WSL Ubuntu 22.04
* In WSL: `sudo apt install mingw-w64 g++-mingw-w64-x86-64 autoconf automake libtool pkg-config gettext win-iconv-mingw-w64-dev libz-mingw-w64-dev patch zip unzip curl git`
* On Windows: Qt 5.15.x (MinGW build), MSYS2 + mingw64 toolchain, WiX 3.11+, plus WinTun.dll (auto-fetched by CI from wintun.net)

Build:

```bash
# Step 1 — cross-build libopenconnect with our 4 patches (one-time, ~3 min)
bash scripts/build-libopenconnect-keenetic.sh
# produces external/openconnect-keenetic_mingw64.zip

# Step 2 — build GUI + service + MSI
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target installer
# produces build/Keenetic-VPN-1.0.0-x64.msi
```

For an automated build, push to `keenetic-camouflage` — GitHub Actions
(`.github/workflows/windows.yml`) builds, signs (if KEENETIC_SIGN_PFX
+ KEENETIC_SIGN_PASS are set as repo secrets) and uploads the MSI as
an artefact.

## Camouflage envelope

The single CLI option `--camouflage-secret <s>` (and now the public API
`openconnect_set_camouflage_secret(vpninfo, s)` added by patch 400)
activates the entire anti-DPI envelope inside libopenconnect:

| layer | stock AnyConnect              | camouflage = 2 |
|---|---|---|
| CSTP magic     | `STF\x01`                       | first 4 bytes of HMAC-SHA256(secret, "cstp-magic") |
| header prefix  | `X-CSTP-*`, `X-DTLS-*`           | `X-S-*`, `X-D-*` |
| cookie name    | `webvpn=`                       | `session=` |
| XML root       | `<config-auth>`                 | `<auth-request>` |
| tunnel URL     | `/CSCOSSLC/tunnel`              | `/api/v1/session` |
| `X-Transcend-Version` | sent                     | omitted |
| TLS ClientHello | one segment, SNI exposed       | first segment 1–3 bytes, rest in 64–256 byte chunks |
| DTLS           | optionally enabled             | forced off |
| trust          | system store + cert pin (TOFU) | bundled LE CA, no pin |

All of the above either travels with the patched libopenconnect-5.dll
(patches 100/200/300) or is set up at runtime by the GUI (`vpninfo.cpp`).

## License

Upstream openconnect-gui: GPL v2 — preserved.
Fork additions (wintun-service, installer, patches, build scripts): GPL v2.
WinTun.dll (bundled): MIT (WireGuard LLC).
