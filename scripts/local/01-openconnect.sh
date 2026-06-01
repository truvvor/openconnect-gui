#!/usr/bin/env bash
# Build patched openconnect 9.12 (camouflage) locally into external/ zips.
# Faithful local replica of .github/workflows/build-openconnect.yml (MINGW64).
# Run in MINGW64:  MSYSTEM=MINGW64 bash -l scripts/local/01-openconnect.sh
export MSYSTEM=MINGW64
source /etc/profile >/dev/null 2>&1 || true
exec > "/c/src/openconnect-gui/scripts/local/01-openconnect.log" 2>&1
set -eux
which gcc cmake pkg-config bsdtar

# CI builds openconnect WITHOUT NSIS in PATH, so openconnect's own Windows
# installer subtree (file-list.txt <- Makefile.dlldeps, which has no rule in the
# release tarball) is never built. We DO have NSIS installed (for the GUI
# packaging), so openconnect's configure detects makensis and then make fails.
# Hide makensis only for this build; restore on exit (even on error).
MAKENSIS_BIN="$(command -v makensis || true)"
if [ -n "$MAKENSIS_BIN" ] && [ -f "$MAKENSIS_BIN" ]; then
  mv "$MAKENSIS_BIN" "$MAKENSIS_BIN.hidden"
  trap 'mv "$MAKENSIS_BIN.hidden" "$MAKENSIS_BIN" 2>/dev/null || true' EXIT
fi

REPO=/c/src/openconnect-gui
OCVER=9.12
WORK="$(mktemp -d)"
cd "$WORK"

wget -q "https://www.infradead.org/openconnect/download/openconnect-$OCVER.tar.gz" -O oc.tar.gz
tar xf oc.tar.gz
mv "openconnect-$OCVER" oc
cd oc

for p in "$REPO"/patches/openconnect/*.patch; do
  echo "=== applying $(basename "$p") ==="
  patch -p1 < "$p"
done

# wintun.h — openconnect 9.x unconditionally #includes it on _WIN32
wget -q https://www.wintun.net/builds/wintun-0.14.1.zip -O "$WORK/wintun.zip"
mkdir -p "$WORK/wintun-pkg"
bsdtar -xf "$WORK/wintun.zip" -C "$WORK/wintun-pkg"
cp "$WORK/wintun-pkg/wintun/include/wintun.h" ./wintun.h

wget -q "https://gitlab.com/openconnect/vpnc-scripts/-/raw/master/vpnc-script-win.js" -O vpnc-script-win.js

test -x ./configure || ./autogen.sh
./configure \
  --host=x86_64-w64-mingw32 --prefix=/mingw64 \
  --disable-nls --without-openssl --with-gnutls \
  --enable-shared --disable-static \
  --with-default-vpncscript=vpnc-script-win.js \
  CFLAGS="-O2 -pipe -Wno-error=incompatible-pointer-types -Wno-incompatible-pointer-types -Wno-error=int-conversion -Wno-int-conversion -Wno-error=implicit-function-declaration -Wno-error"
make -j"$(nproc)"

# ---- stage runtime + devel exactly like CI ----
STAGE_RT="$WORK/stage-runtime"; STAGE_DEV="$WORK/stage-devel"
mkdir -p "$STAGE_RT" "$STAGE_DEV/include" "$STAGE_DEV/lib"
cp .libs/libopenconnect-5.dll "$STAGE_RT/"
cp .libs/openconnect.exe       "$STAGE_RT/"
cp vpnc-script-win.js          "$STAGE_RT/"
cp openconnect.h               "$STAGE_DEV/include/"
cp .libs/libopenconnect.dll.a  "$STAGE_DEV/lib/"
MINGW_INC=/mingw64/include
for d in gnutls libxml2 libxml stoken nettle gmp p11-kit-1 p11-kit; do
  [ -d "$MINGW_INC/$d" ] && cp -r "$MINGW_INC/$d" "$STAGE_DEV/include/" || true
done
for h in gmp.h gmpxx.h zlib.h zconf.h lz4.h lzma.h; do
  [ -f "$MINGW_INC/$h" ] && install -D "$MINGW_INC/$h" "$STAGE_DEV/include/$(basename "$h")" || true
done
cp /mingw64/bin/*.dll "$STAGE_RT/" 2>/dev/null || true
# wintun.dll: runtime driverless adapter (used on machines without a TAP adapter)
cp "$WORK/wintun-pkg/wintun/bin/amd64/wintun.dll" "$STAGE_RT/" 2>/dev/null || true
for g in libopenconnect libgnutls libgnutls-openssl libgnutlsxx libgmp libhogweed \
         libnettle libp11-kit libtasn1 libxml2 libxml libstoken libidn2 libunistring \
         libffi libz zlib liblz4 liblzma libiconv libintl libcharset libpsl \
         libwinpthread libssp; do
  for f in /mingw64/lib/$g.dll.a /mingw64/lib/$g.a; do
    [ -f "$f" ] && cp "$f" "$STAGE_DEV/lib/" || true
  done
done

mkdir -p "$REPO/external"
ART_RT="$REPO/external/openconnect-v$OCVER-camouflage_MINGW64.zip"
ART_DEV="$REPO/external/openconnect-devel-v$OCVER-camouflage_MINGW64.zip"
rm -f "$ART_RT" "$ART_DEV"
( cd "$STAGE_RT"  && bsdtar -caf "$ART_RT"  . )
( cd "$STAGE_DEV" && bsdtar -caf "$ART_DEV" . )
ls -la "$REPO/external/"
echo "OPENCONNECT_DONE"
