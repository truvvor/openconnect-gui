# Keenetic anti-DPI patches for openconnect 9.12

These three quilt-style patches are vendored from
`mike@letta.gk.company:/home/mike/keenetic-ci/patches/openconnect/`.
They are applied in order by `scripts/build-libopenconnect-keenetic.sh`
to a fresh checkout of `infradead/openconnect` at tag `v9.12`.

| # | file | files touched | what it does |
|---|---|---|---|
| 1 | `100-camouflage.patch`    | `auth.c`, `cstp.c`, `main.c`, `openconnect-internal.h` | Adds `--camouflage-secret <s>`. When set, derives CSTP magic bytes from HMAC-SHA256, renames `X-CSTP-*`→`X-S-*`, `X-DTLS-*`→`X-D-*`, cookie `webvpn=`→`session=`, XML root `config-auth`→`auth-request`, CONNECT path `/CSCOSSLC/tunnel`→`/api/v1/session`, suppresses `X-Transcend-Version`. Single switch — all changes activate together. |
| 2 | `200-xray-scatter.patch`  | `gnutls.c` | Installs a `gnutls_transport_set_push_function` that fragments the first TLS write into a 1-3 byte leading TCP segment followed by 64-256 byte random chunks for ~8 writes. Defeats SNI-in-first-segment DPI inspection. Only activated when `camouflage_secret` is set. |
| 3 | `300-keenetic-passwd.patch` | `main.c` | Adds `--passwd <pw>` / `-w` short option, retains password across reconnect attempts (no post-use zeroing). Allows non-interactive deployment from the GUI without dragging stdin. |

## Versioning rules

- Bumping the openconnect tag (`OC_REF` in `build-libopenconnect-keenetic.sh`)
  may require refreshing these patches. Refresh procedure:

```bash
ssh mike@letta.gk.company       # password [jxeljvjq
cd /home/mike/keenetic-ci/patches/openconnect/
# apply to a fresh openconnect checkout; if hunks fail, re-do the patch
# from the new source, copy back into here, push.
```

- The patches MUST stay byte-identical to the keenetic-ci canonical copies.
  When CLAUDE.md/WORKLOG.md is touched server-side, sync these too.

## Activation envelope

The single CLI option `--camouflage-secret "<32-char shared secret>"` enables
the whole camouflage envelope inside libopenconnect. From the GUI we'll use a
short helper `openconnect_set_camouflage_secret(vpninfo, secret)` (added in
the GUI's `vpninfo.cpp` via `openconnect_set_loglevel`-style call into the
struct field that the patch introduces).

If the future libopenconnect API changes the struct layout, this fork can
add a 4th patch `400-camouflage-public-api.patch` exposing
`openconnect_set_camouflage_secret()` as part of `openconnect.h`. Currently
the GUI sets it through the existing `openconnect_parse_url`+CLI shim path.
