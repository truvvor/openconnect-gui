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
