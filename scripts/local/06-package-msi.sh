#!/usr/bin/env bash
# Build an MSI via CPack's WIX generator (for GPO mass-deployment).
# Requires WiX v3 (candle/light) at C:\src\wix314.
export MSYSTEM=MINGW64
source /etc/profile >/dev/null 2>&1 || true
exec > /c/src/openconnect-gui/scripts/local/06-package-msi.log 2>&1
set -ux

export WIX='C:\src\wix314'
export PATH="/c/src/wix314:$PATH"
which candle light || true

cd /c/src/openconnect-gui
# Reconfigure so CPackConfig.cmake picks up the CPACK_WIX_* settings.
cmake -S . -B build-local -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_MAKE_PROGRAM=mingw32-make.exe \
  -DPROJ_ADMIN_PRIV_ELEVATION=OFF \
  -Dopenconnect-TAG=9.12 >/dev/null

cd build-local
rm -f *.msi
cpack -G WIX -V
echo "=== MSI ==="
ls -la *.msi 2>/dev/null || true
echo "=== generated wxs (for inspection) ==="
find _CPack_Packages -name '*.wxs' 2>/dev/null
echo MSI_DONE
