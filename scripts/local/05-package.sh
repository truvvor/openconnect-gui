#!/usr/bin/env bash
# Build the NSIS installer (CPack) — validates packaging incl. the service.
export MSYSTEM=MINGW64
source /etc/profile >/dev/null 2>&1 || true
exec > /c/src/openconnect-gui/scripts/local/05-package.log 2>&1
set -eux
cd /c/src/openconnect-gui
cmake --build build-local --target package -j"$(nproc)"
echo "=== installers ==="
find build-local -maxdepth 1 -iname '*.exe'
echo PACKAGE_DONE
