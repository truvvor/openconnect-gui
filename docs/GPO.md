# Configuring openconnect-gui via Group Policy / registry

openconnect-gui reads its machine-wide defaults through Qt's
`QSettings(QSettings::SystemScope, "OpenConnect-GUI Team", "openconnect-gui")`.
On Windows this maps to:

```
HKEY_LOCAL_MACHINE\SOFTWARE\OpenConnect-GUI Team\openconnect-gui
```

(and, on 32-bit clients running on 64-bit Windows, WoW6432Node under that path).

## What the defaults control

For every profile that has **not** already been saved by the user, the
following per-key defaults populate the profile editor at "New profile" time
and, for boolean options, drive the value that gets written to
`HKCU\SOFTWARE\OpenConnect-GUI Team\openconnect-gui\server:<name>\<key>` on
first save. HKCU always wins for saved profiles — GPO only sets the initial
value, so users can still opt out through the UI unless you additionally
lock the HKCU key via a "prevent user override" ACL.

| Registry key (under `HKLM\SOFTWARE\OpenConnect-GUI Team\openconnect-gui\defaults\`) | Type | Compile-time fallback | Effect when set to `1` / `0` |
| --- | --- | --- | --- |
| `batch` | REG_DWORD | `1` (on) | Save password checkbox default. `1` = the password field is remembered per profile. |
| `no-default-route` | REG_DWORD | `1` (on) | Split-tunnel: when `1`, the profile does NOT install a default gateway via the VPN — only CISCO_SPLIT_INC routes are added. |
| `auto-accept-banner` | REG_DWORD | `1` (on) | When `1`, the "Welcome to KN-…" Accept dialog is replaced with a silent log entry. |
| `suppress-cert-change` | REG_DWORD | `1` (on) | When `1`, "peer certificate has changed" prompts are auto-accepted and logged. Recommended when the profile has a CA file configured (validity is already enforced by libopenconnect). |
| `force-password-prompt` | REG_DWORD | `0` (off) | When `1`, always show the password prompt at connect time (pre-filled with the saved value if any). |
| `disable-udp` | REG_DWORD | `0` (off) | When `1`, disable DTLS/UDP transport (TCP only). Also implicitly enabled per-profile when a camouflage secret is set with "Force TCP only" checked. |
| `proxy` | REG_DWORD | `0` (off) | When `1`, use the Windows system proxy for outbound HTTPS. |
| `minimize-on-connect` | REG_DWORD | `0` (off) | When `1`, the main window minimizes to tray after Connect. |

## Registry snippet (.reg)

```reg
Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\OpenConnect-GUI Team\openconnect-gui\defaults]
"batch"=dword:00000001
"no-default-route"=dword:00000001
"auto-accept-banner"=dword:00000001
"suppress-cert-change"=dword:00000001
"force-password-prompt"=dword:00000000
"disable-udp"=dword:00000000
"proxy"=dword:00000000
"minimize-on-connect"=dword:00000000
```

## Rolling this out via GPO

1. Save the `.reg` snippet above (or the ADMX template in `gpo/openconnect-gui.adml/.admx`).
2. In `Group Policy Management Editor` → `Computer Configuration` →
   `Preferences` → `Windows Settings` → `Registry`, add each value pointing to
   the paths above.
3. Target the GPO to the machines / OU that should get the tuned defaults.

## What is intentionally **not** set via GPO

- **`camouflage-secret`** and the client cert / key / password are per-user,
  encrypted at rest with Windows DPAPI via Qt's `CryptData`, and never read
  from the SystemScope. Deploying these across a fleet is a different
  problem (typically: seed HKCU via a login script, or pre-provision a
  profile export/import file).
- **Any of the numeric fields** (reconnect timeout, DTLS attempt period) —
  they are not on the machine-wide defaults path; add them only if you have
  a concrete need. Contact the maintainer to extend the schema.

## Verifying policy application

After a policy refresh (`gpupdate /force` or reboot), start openconnect-gui,
create a **new** profile and open its editor. The three anti-DPI checkboxes
should reflect the machine-wide defaults you set. Existing profiles will keep
their previously-saved values — that's HKCU winning over HKLM by design.

You can also confirm the effective value from an elevated PowerShell:

```powershell
reg query "HKLM\SOFTWARE\OpenConnect-GUI Team\openconnect-gui\defaults" /v batch
```
