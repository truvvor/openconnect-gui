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

# Code-signing of the staged exes (consumed by sign-staged.cmake via CPack
# pre-build hook) and of the final MSI below. No-op if OCG_SIGN_THUMB is unset.
export OCG_SIGN_THUMB='7585D0C01F4BA44DA052E2C7088869AB332758AC'
export OCG_SIGNTOOL='C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe'
export OCG_SIGN_TS='http://timestamp.digicert.com'

cd /c/src/openconnect-gui
# Reconfigure so CPackConfig.cmake picks up the CPACK_WIX_* settings.
cmake -S . -B build-local -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_MAKE_PROGRAM=mingw32-make.exe \
  -DPROJ_ADMIN_PRIV_ELEVATION=OFF \
  -Dopenconnect-TAG=9.12 >/dev/null

cd build-local
rm -f *.msi 2>/dev/null || true
cpack -G WIX -V || true   # cpack's final copy to build-local can fail if AV-locked; recovered below

# The authoritative MSI always lands under _CPack_Packages/.../WIX/, with its
# embedded exes signed by the pre-build hook. CPack also tries to copy it to
# build-local/, but that copy intermittently fails when an AV briefly locks the
# destination -- in which case build-local/ may hold a STALE (unsigned-exe) MSI
# from a prior run. So we source the fresh _CPack copy directly and only use the
# build-local copy if it can be refreshed.
SRC="$(find _CPack_Packages -path '*/WIX/*' -name 'openconnect-gui-*.msi' 2>/dev/null | head -n1)"
MSI="$SRC"
if [ -n "$SRC" ]; then
  for i in 1 2 3 4 5; do
    if cp -f "$SRC" "./$(basename "$SRC")" 2>/dev/null; then MSI="./$(basename "$SRC")"; break; fi
    echo "build-local copy attempt $i failed (locked?), retrying..."; sleep 3
  done
fi
echo "=== MSI === $MSI"
ls -la "$MSI" 2>/dev/null || true

# Sign the MSI itself (Authenticode + RFC3161 timestamp). The embedded exes were
# already signed by the CPack pre-build hook (sign-staged.cmake).
if [ -n "${OCG_SIGN_THUMB:-}" ] && [ -n "$MSI" ]; then
  # MSYS converts bare "/flag" args into Windows paths (e.g. /pa -> C:/msys64/pa);
  # MSYS2_ARG_CONV_EXCL='*' disables that so signtool's flags pass through literally.
  for i in 1 2 3 4 5; do
    MSYS2_ARG_CONV_EXCL='*' "$OCG_SIGNTOOL" sign /sha1 "$OCG_SIGN_THUMB" /fd sha256 /tr "$OCG_SIGN_TS" /td sha256 "$MSI" && break
    echo "msi sign attempt $i failed (locked?), retrying..."; sleep 3
  done
  echo "=== MSI signature ==="
  MSYS2_ARG_CONV_EXCL='*' "$OCG_SIGNTOOL" verify /pa "$MSI" || true
fi
echo "=== generated wxs (for inspection) ==="
find _CPack_Packages -name '*.wxs' 2>/dev/null
echo "SIGNED_MSI=$MSI"
echo MSI_DONE
