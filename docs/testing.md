# Live testing procedure — Phase 7

This document is the manual checklist for verifying a release MSI against
the production KN-3710 / KN-3010 server stack in MSK.

## 1. Build the MSI

Local:
```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target installer
# Produces build/Keenetic-VPN-1.0.0-x64.msi
```

Or download the artefact from a GitHub Actions run on `keenetic-camouflage`.

## 2. First-time install (UAC once)

1. Right-click `Keenetic-VPN-1.0.0-x64.msi` → **Install**. Accept UAC.
2. Verify:
   * `services.msc` lists "Keenetic anti-DPI VPN — WinTun service" (manual).
   * `net localgroup KeeneticVPNUsers` lists your user.
   * `%ProgramFiles%\Keenetic VPN\` contains `keenetic-vpn-gui.exe`,
     `KeeneticVpnService.exe`, `wintun.dll`, `libopenconnect-5.dll` + deps,
     `keenetic-ca.crt`.

## 3. No-UAC launch

3.1. Start Menu → **Keenetic anti-DPI VPN**. The icon should appear in the
     system tray with a red traffic-light.

3.2. Open Task Manager → Details → confirm `keenetic-vpn-gui.exe` is
     running as **the regular user** (not SYSTEM, not Administrator).
     This is the core no-UAC requirement.

3.3. `KeeneticVpnService.exe` should appear briefly when you press Connect
     (auto-started by SCM via `KeeneticVPNUsers` ACL).

## 4. Profile setup

4.1. Click **New...** → enter:
     * Profile name: `gk-msk04`
     * Server URL: `https://gk-msk04.netcraze.pro`
     * Protocol: AnyConnect

4.2. **Edit...** the new profile:
     * Username: your ocserv user
     * Password: your ocserv pass
     * **Camouflage** checkbox: ✅ ON (default)
     * Camouflage secret: `FCuA1B86sXsITTC2Yb5YHnfutaiijfWC`
     * Tunnel URL: `/api/v1/session` (default)
     * Save.

4.3. (Optional) **File → Open camouflage config in editor** opens
     `%APPDATA%\Keenetic anti-DPI Team\Keenetic VPN\camouflage-configs\gk-msk04.conf`
     for manual editing of advanced fields.

## 5. Connect test — camouflage path

5.1. Press **Connect**. Within 5–10 s the traffic-light should turn green
     and the log should show:
     ```
     Camouflage envelope enabled (secret hash 32)
     Anti-DPI: TCP scatter enabled (first frag 1-3 bytes)
     WinTun adapter open via KeeneticVpnService — no UAC required
     Connected: 172.16.5.X
     ```

5.2. On the server side, ssh `admin@87.228.71.67` → `show vpn server`
     should list a new client.

5.3. From the client run:
     ```powershell
     ping 8.8.8.8
     tracert google.com    # first hop should be 172.16.5.1
     ```

## 6. Compatibility-mode test

6.1. In the GUI menu **Connection → Compatibility mode (camouflage OFF)**.
     Re-Connect.

6.2. The log should show:
     ```
     Compatibility mode ON — next connect will use stock AnyConnect protocol
     ```

6.3. Wireshark on the egress interface — confirm the HTTP CONNECT line
     is `CONNECT /CSCOSSLC/tunnel HTTP/1.1` (NOT `/api/v1/session`).
     Confirm the TCP ClientHello SNI is in a single segment (NOT scattered).

6.4. The server will refuse the connection (ocserv has `camouflage = 2`
     which rejects non-camouflaged clients). This is expected and proves
     the toggle actually changes the wire protocol.

6.5. Turn the toggle off again.

## 7. Reconnect / DPD test

7.1. While connected, kill the route to the server:
     `route delete 87.228.71.67`.

7.2. Within 60 s the GUI should log:
     ```
     Reconnecting (attempt 1/10)...
     ```
     and either reconnect (when route is restored) or finally surrender
     at attempt 10/10 with red status.

## 8. Uninstall

8.1. **Programs and Features → Keenetic anti-DPI VPN → Uninstall**.
     Accept UAC.

8.2. Verify:
     * `services.msc` no longer lists `KeeneticVpnService`.
     * `net localgroup KeeneticVPNUsers` returns "no such group".
     * `%ProgramFiles%\Keenetic VPN\` directory is gone.
     * Profile data under `%APPDATA%\Keenetic anti-DPI Team\` survives
       (intentional — re-install preserves the profiles).

## 9. Known issues / TODOs

* The pipe-based packet path between libopenconnect and the service is
  currently a stub (`setRoutes`/`setDns` return ok but don't actually
  edit the route table). For Phase 7 we need to wire `CreateIpForwardEntry2`
  / `SetInterfaceDnsSettings` in `wintun-service/wintun_adapter.cpp`.
* `openconnect_setup_tun_fd` currently receives the WinTun session HANDLE
  numerically. The handle is in the **service**'s process — we must
  cross-process duplicate it via `DuplicateHandle()` before returning to
  the client. Phase 7 fix.
* WiX `AddCurrentUserToGroup` uses `net localgroup` — works but lazy.
  Replace with a proper `NetLocalGroupAdd` / `NetLocalGroupAddMembers`
  custom action in `installer_helper.dll` to avoid spawning cmd.

These items are flagged in `ARCHITECTURE.md` under Phase 7.
