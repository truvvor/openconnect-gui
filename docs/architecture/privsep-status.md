# Privilege-separation — build/resume status

Branch: `ci/privilege-separation` (off `d9ce509`). Track of the P0–P7 plan in
`privilege-separation.md`. Update this file as phases land.

## Done (committed, locally syntax-checked with g++ 13.1 + Qt 6.8.3)

| Commit | Phase | Contents | Verified |
|---|---|---|---|
| `fb31a4b` | P0 | architecture doc + `src/ipc/protocol.h` contract | — |
| `943106d` | P1 | `oc-ipc` lib: `protocol.cpp`, `profile.{h,cpp}`, framing/JSON | `-fsyntax-only` OK |
| `ce79060` | P2 | service skeleton: SCM install/uninstall/run, `QLocalServer` pipe, hello handshake, file log | `-fsyntax-only` OK |

The repo still builds as before: P1/P2 only **add** the `oc-ipc` lib and the
`openconnect-gui-service` target; the GUI is untouched and still works (elevated).

## Remaining

- **P3** port `vpninfo.cpp` engine logic into a headless `VpnEngine` in the
  service, driven by a Profile + an `EngineHost` callback interface (log/stats/
  state/ipinfo + interactive: authForm/cert/banner/pin + persist). GUI keeps its
  own `VpnInfo` for now ⇒ repo still compiles after P3.
- **P4** implement `EngineHost` in `IpcSession`: marshal prompts/persist, stream
  log/stats/ipinfo, run connect/disconnect/status on the engine.
- **P5 (cutover)** GUI `ServiceClient` + rewrite `mainwindow` connect path to use
  it; render the 4 prompt kinds from events; write `persist` to QSettings; build
  GUI with `-DPROJ_ADMIN_PRIV_ELEVATION=OFF`. Remove `VpnInfo` from the GUI.
- **P6** NSIS service registration + `asInvoker` GUI + `build-windows.yml` builds
  both targets and stages the service's Qt/openconnect deps.
- **P7** E2E vs `gk-msk03` (needs network reachability) + security review.

## Important constraints discovered

- From **P3 on, local syntax-check is not possible**: the engine needs
  `openconnect.h` + `gnutls/*` headers that exist only in the MSYS2 build
  (`external/` zip in CI). P3+ are verified by GitHub Actions `build-windows.yml`.
- **P3+P4 are buildable without touching the GUI** (engine added to service only).
  The GUI cutover (P5) is the single step where `mainwindow.cpp`/`vpninfo` change;
  keep it last so every prior push yields a green-compilable tree.
- CI trigger map (handoff §4.1): pushing this branch triggers both workflows;
  cancel `build-openconnect` (patches unchanged — last good artifact is reused)
  and let `build-windows` run, or `gh workflow run build-windows.yml --ref ci/privilege-separation`.

## Resume

```
cd C:\src\openconnect-gui && git checkout ci/privilege-separation
git log --oneline -5     # head should be the latest P# commit
# next: implement src/engine/vpnengine.{h,cpp} (P3), add to service target,
#       push, watch build-windows, iterate to green.
```


---

## RESULT — privilege separation complete & E2E verified (2026-05-29)

Built locally (MSYS2 MINGW64, Qt 5.15.18) — CI was over quota, so a local build
machine was stood up (`scripts/local/*.sh`). All phases P0–P7 done:

| Phase | Commit | State |
|---|---|---|
| P0 design + protocol | `fb31a4b` | done |
| P1 oc-ipc | `943106d` | done |
| P2 service skeleton | `ce79060` | done |
| P3 VpnEngine | `f9f021a` | done |
| P4 IPC lifecycle + prompts | `5006767` | done |
| P5 GUI ServiceClient + asInvoker | `6f04a11` | done |
| P7 fix + E2E client | `90ad3fb` | done |
| P6 NSIS service registration | (this) | done |

**Binaries (asInvoker verified on the PE):** `openconnect-gui.exe`
requireAdministrator=**False**, asInvoker=**True** → no UAC at launch. Service
runs as LocalSystem via SCM.

**Live E2E (unprivileged `octest` → service → camouflaged VPN):**
connected to `7b0c.gk-msk05.netcraze.pro` with `camouflage-secret` + `no-dtls`,
cert prompt marshalled to the client, auth `admin`, CSTP tunnel up, assigned
**172.16.10.3**, TUN opened by the LocalSystem service. The unprivileged client
never touched the adapter/routes.

**Installer:** `openconnect-gui-1.5.3-win64.exe` (~50 MB) registers the service
on install (the one elevated moment) and deregisters on uninstall.

### Security review notes / follow-ups
- **Pipe ACL.** Implemented with `QLocalServer::WorldAccessOption` (Qt cannot set
  an arbitrary SDDL on its pipe). This is broader than the documented
  INTERACTIVE-only `kPipeSddl`. Mitigation in place: service identifies the client
  PID; hardening TODO: reimplement the accept loop with `CreateNamedPipe` + the
  SDDL, or verify the client token is INTERACTIVE and the image is the signed GUI.
- **Secrets** travel plaintext over the local kernel pipe only at connect time;
  the service persists none (GUI keeps them DPAPI-encrypted). Buffers not yet zeroed.
- **Trust persistence** (`persist{what:trust}`) is logged but not yet re-imported
  into the GUI's gtdb store, so the cert prompt re-appears each first connect per
  server — functional, not yet polished.
- Single active tunnel per service instance (second connect rejected `busy`).
