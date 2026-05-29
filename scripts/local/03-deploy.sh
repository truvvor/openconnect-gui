#!/usr/bin/env bash
# Stage a runnable deployment (GUI + service + all runtime deps) for local E2E.
# Run in MINGW64:  bash -l scripts/local/03-deploy.sh
export MSYSTEM=MINGW64
source /etc/profile >/dev/null 2>&1 || true
exec > /c/src/openconnect-gui/scripts/local/03-deploy.log 2>&1
set -eux

REPO=/c/src/openconnect-gui
STAGE=/c/oc-stage
rm -rf "$STAGE"
mkdir -p "$STAGE"

# fixup_bundle (GUI) gathers Qt + transitive DLLs; openconnect DLLs, ca-bundle,
# vpnc-script-win.js and the service exe come from their install() rules. Both
# exes share the one directory (and therefore the Qt5Core/Network DLLs).
cmake --install "$REPO/build-local" --prefix "$STAGE"

# wintun.dll: the runtime driver libopenconnect loads to create the adapter.
# (CI only fetched wintun.h for the build, never shipped the dll.)
if [ ! -f "$STAGE/wintun.dll" ]; then
  W="$(mktemp -d)"
  wget -q https://www.wintun.net/builds/wintun-0.14.1.zip -O "$W/w.zip"
  bsdtar -xf "$W/w.zip" -C "$W"
  cp "$W/wintun/bin/amd64/wintun.dll" "$STAGE/wintun.dll"
fi

echo "=== staged files ==="
ls -la "$STAGE"
echo "=== key artifacts ==="
for f in openconnect-gui.exe openconnect-gui-service.exe libopenconnect-5.dll \
         Qt5Core.dll Qt5Network.dll vpnc-script-win.js ca-certificates.crt wintun.dll; do
  [ -e "$STAGE/$f" ] && echo "OK   $f" || echo "MISS $f"
done
echo DEPLOY_DONE
