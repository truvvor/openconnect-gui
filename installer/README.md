# Installer — WiX MSI for Keenetic anti-DPI VPN

`product.wxs` builds a single MSI that:

* drops the GUI exe + service exe + libopenconnect.dll + GnuTLS/libxml2 deps + bundled LE CA bundle + WinTun.dll into `%ProgramFiles%\Keenetic VPN\`
* registers **KeeneticVpnService** (LocalSystem, demand-start)
* creates local group **KeeneticVPNUsers** and adds the installing user so the GUI can `sc start` the service without UAC at every launch
* on uninstall: stops + removes the service, deletes the group, removes the files

## Build prerequisites

* WiX 3.11+ on `PATH` (`choco install wixtoolset` or [wix3 releases](https://github.com/wixtoolset/wix3/releases))
* Built artefacts in `${CMAKE_BINARY_DIR}/src/keenetic-vpn-gui.exe` and `${CMAKE_BINARY_DIR}/wintun-service/KeeneticVpnService.exe`
* `external/openconnect-keenetic_mingw64.zip` unpacked (Phase 1)
* `bundle/keenetic-ca.crt` — the Let's Encrypt R10-R14 + E5-E9 + ISRG X1/X2 bundle (Phase 7 to refresh)
* `external/wintun-bin/wintun.dll` — official WireGuard WinTun build for x64 ([wintun.net](https://www.wintun.net/), MIT)

## Signing

Set `KEENETIC_SIGN_PFX` + `KEENETIC_SIGN_PASS` env vars before calling
`cmake --build .` and `signtool` will sign the MSI with that PFX. Without
those variables the MSI is unsigned (SmartScreen will warn — expected on
dev machines).

## Local test

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --target installer
.\build\Keenetic-VPN-1.0.0-x64.msi    # interactive install
```

After install:

* Start Menu → "Keenetic anti-DPI VPN" launches the GUI **without UAC**.
* `services.msc` shows `Keenetic anti-DPI VPN — WinTun service` (manual start; GUI starts it as needed).
* `net localgroup KeeneticVPNUsers` lists the installing user.
