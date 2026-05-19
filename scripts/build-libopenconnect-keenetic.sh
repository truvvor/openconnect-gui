#!/usr/bin/env bash
# build-libopenconnect-keenetic.sh
# -----------------------------------------------------------------------------
# Cross-build libopenconnect with the Keenetic anti-DPI patches under MinGW-w64.
#
# Source bootstrap: external/openconnect-9.12-keenetic.tar.gz
#   (snapshot of openconnect 9.12 with the three keenetic-camouflage
#    patches already applied; pulled from letta SDK build_dir.)
#
# Build deps come from horar/openconnect's mingw64 release zip
# (gnutls/nettle/gmp/hogweed/p11-kit/libxml2 .dll + .dll.a + headers).
#
# Output:  external/openconnect-keenetic_mingw64.zip
#          containing bin/libopenconnect-5.dll, bin/openconnect.exe,
#                     include/openconnect.h, lib/libopenconnect.dll.a,
#                     bin/lib{gnutls,nettle,gmp,hogweed,p11-kit,xml2}*.dll
#
# Usage:   scripts/build-libopenconnect-keenetic.sh [-j N]
# -----------------------------------------------------------------------------
set -euo pipefail

JOBS="${JOBS:-$(nproc)}"
case "${1:-}" in
    -j) JOBS="$2"; shift 2 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

OC_TARBALL="$REPO_ROOT/external/openconnect-9.12-keenetic.tar.gz"
OC_SRC_DIR="$REPO_ROOT/external/openconnect-keenetic"
OC_DEPS_DIR="$REPO_ROOT/external/openconnect-deps-mingw64"
OUT_DIR="$REPO_ROOT/external"
OUT_ZIP="$OUT_DIR/openconnect-keenetic_mingw64.zip"

DEPS_RELEASE_TAG="v8.02"
DEPS_DEV_URL="https://github.com/horar/openconnect/releases/download/${DEPS_RELEASE_TAG}/openconnect-devel-${DEPS_RELEASE_TAG}_mingw64.zip"
DEPS_RUNTIME_URL="https://github.com/horar/openconnect/releases/download/${DEPS_RELEASE_TAG}/openconnect-${DEPS_RELEASE_TAG}_mingw64.zip"

HOST=x86_64-w64-mingw32

log()  { printf '\e[36m[build]\e[0m %s\n' "$*"; }
fail() { printf '\e[31m[build] FATAL:\e[0m %s\n' "$*" >&2; exit 1; }

# ---- 0. preconditions ------------------------------------------------------
command -v "${HOST}-gcc" >/dev/null || fail "${HOST}-gcc not in PATH — sudo apt install mingw-w64 g++-mingw-w64-x86-64"
[[ -f "$OC_TARBALL" ]] || fail "missing tarball $OC_TARBALL (refresh from letta:/data/keenetic-sdk-kn3010/build_dir/target-mipsel-linux-musl_musl/openconnect-9.12-2)"

# ---- 1. extract openconnect source ----------------------------------------
if [[ ! -d "$OC_SRC_DIR/src" ]]; then
    log "Extracting $OC_TARBALL -> $OC_SRC_DIR ..."
    rm -rf "$OC_SRC_DIR"
    mkdir -p "$OC_SRC_DIR"
    tar -xzf "$OC_TARBALL" -C "$OC_SRC_DIR" --strip-components=1
fi
[[ -f "$OC_SRC_DIR/configure.ac" || -f "$OC_SRC_DIR/configure" ]] || fail "extracted tree has no configure"

# ---- 2. fetch mingw64 dep DLLs + import libs ------------------------------
mkdir -p "$OC_DEPS_DIR/bin" "$OC_DEPS_DIR/lib" "$OC_DEPS_DIR/include"
if [[ ! -f "$OC_DEPS_DIR/lib/libgnutls.dll.a" ]]; then
    log "Fetching mingw64 devel pack (${DEPS_RELEASE_TAG})..."
    TMP=$(mktemp)
    curl -fSL --retry 3 -o "$TMP" "$DEPS_DEV_URL"
    ( cd "$OC_DEPS_DIR" && unzip -q -o "$TMP" )
    rm -f "$TMP"
fi
if [[ ! -f "$OC_DEPS_DIR/bin/libgnutls-30.dll" ]]; then
    log "Fetching mingw64 runtime pack (${DEPS_RELEASE_TAG})..."
    TMP=$(mktemp)
    curl -fSL --retry 3 -o "$TMP" "$DEPS_RUNTIME_URL"
    ( cd "$OC_DEPS_DIR/bin" && unzip -q -o "$TMP" "*.dll" )
    rm -f "$TMP"
fi

# ---- 3. stage pkg-config files pointing at our deps -----------------------
PCDIR="$OC_DEPS_DIR/lib/pkgconfig"
mkdir -p "$PCDIR"
cat > "$PCDIR/gnutls.pc" <<EOF
prefix=$OC_DEPS_DIR
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: GnuTLS
Description: GnuTLS (cross-supplied from horar/openconnect $DEPS_RELEASE_TAG)
Version: 3.6.16
Libs: -L\${libdir} -lgnutls
Cflags: -I\${includedir}
EOF
cat > "$PCDIR/libxml-2.0.pc" <<EOF
prefix=$OC_DEPS_DIR
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: libxml2
Description: libxml2 (cross-supplied)
Version: 2.9.12
Libs: -L\${libdir} -lxml2
Cflags: -I\${includedir}/libxml2
EOF
cat > "$PCDIR/p11-kit-1.pc" <<EOF
prefix=$OC_DEPS_DIR
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: p11-kit-1
Description: p11-kit (cross-supplied)
Version: 0.23.22
Libs: -L\${libdir} -lp11-kit
Cflags: -I\${includedir}/p11-kit-1
EOF

# ---- 3.5. Cross-platform fixups (in-place patches we don't ship as quilt) -
# (a) gnutls.c #include <netinet/tcp.h> -> wrap with #ifdef _WIN32.
#     This belongs to 200-xray-scatter.patch but is easier to keep here for
#     idempotency; the patch file in patches/ also has the cross-platform form.
if grep -q '^#include <netinet/tcp.h>$' "$OC_SRC_DIR/gnutls.c"; then
    log "guarding netinet/tcp.h include for mingw ..."
    python3 - "$OC_SRC_DIR/gnutls.c" <<'PYEOF'
import sys
p = sys.argv[1]
src = open(p).read().replace(
    '#include <netinet/tcp.h>',
    '#ifdef _WIN32\n# include <winsock2.h>\n# include <ws2tcpip.h>\n#else\n# include <netinet/tcp.h>\n#endif',
    1)
open(p,'w').write(src)
PYEOF
fi

# (b) 400-public-camouflage-api: openconnect_set_camouflage_secret as a public
#     symbol. We add the declaration to openconnect.h, the definition to
#     library.c, and export it via libopenconnect.map.in.
log "applying 400-public-camouflage-api in-place (idempotent) ..."
SRC="$OC_SRC_DIR" python3 - <<'PYEOF'
import os
SRC = os.environ['SRC']

hpath = os.path.join(SRC, "openconnect.h")
h = open(hpath).read()
if "openconnect_set_camouflage_secret" not in h:
    needle = "#ifdef __cplusplus\n}\n#endif\n\n#endif /* __OPENCONNECT_H__ */"
    add = (
        "/* Keenetic anti-DPI camouflage public setter (400-patch).\n"
        " * When `secret` is non-NULL non-empty, the camouflage envelope is\n"
        " * enabled inside libopenconnect: HMAC-SHA256 CSTP magic, X-S-/X-D-\n"
        " * header renaming, cookie session=, /api/v1/session, XML auth-request,\n"
        " * suppressed X-Transcend-Version, TCP-scatter on TLS ClientHello.\n"
        " * Passing NULL or empty string disables it.  String is duplicated. */\n"
        "void openconnect_set_camouflage_secret(struct openconnect_info *vpninfo,\n"
        "                                       const char *secret);\n\n"
    )
    h = h.replace(needle, add + needle, 1)
    open(hpath,"w").write(h)

cpath = os.path.join(SRC, "library.c")
c = open(cpath).read()
if "openconnect_set_camouflage_secret" not in c:
    impl = (
        "\n/* Keenetic anti-DPI camouflage public setter (400-patch). */\n"
        "void openconnect_set_camouflage_secret(struct openconnect_info *vpninfo,\n"
        "                                       const char *secret)\n"
        "{\n"
        "\tfree(vpninfo->camouflage_secret);\n"
        "\tvpninfo->camouflage_secret = (secret && *secret) ? strdup(secret) : NULL;\n"
        "}\n"
    )
    c = c.rstrip() + "\n" + impl
    open(cpath,"w").write(c)

mpath = os.path.join(SRC, "libopenconnect.map.in")
m = open(mpath).read()
if "openconnect_set_camouflage_secret" not in m:
    insert = "\nOPENCONNECT_5_10 {\n global:\n\topenconnect_set_camouflage_secret;\n} OPENCONNECT_5_9;\n"
    idx = m.find("OPENCONNECT_PRIVATE {")
    assert idx > 0
    m = m[:idx] + insert + "\n" + m[idx:]
    open(mpath,"w").write(m)
PYEOF

# ---- 4. configure + build -------------------------------------------------
(
    cd "$OC_SRC_DIR"
    # Keenetic SDK tarball strips install-sh / missing / depcomp and the
    # libtool m4 macros; regenerate them via libtoolize + autoreconf.
    log "libtoolize + autoreconf -fiv (force) ..."
    libtoolize --copy --force 2>&1 | tail -5
    autoreconf -fiv 2>&1 | tail -15

    export PKG_CONFIG_LIBDIR="$PCDIR"
    export PKG_CONFIG_PATH=""
    export CPPFLAGS="-I$OC_DEPS_DIR/include"
    export LDFLAGS="-L$OC_DEPS_DIR/lib"

    log "configure (host=$HOST) ..."
    # NOTE: --without-gnutls-version-check is safe because we force no-dtls
    # in client config — server is anti-DPI (TCP-only), DTLS path never exercised.
    ./configure \
        --host="$HOST" \
        --prefix=/keenetic-install \
        --with-gnutls \
        --without-gnutls-version-check \
        --without-openssl \
        --without-stoken \
        --without-libpcsclite \
        --without-libpskc \
        --without-gssapi \
        --without-libproxy \
        --without-lz4 \
        --disable-nls \
        --enable-shared \
        --disable-static \
        --with-vpnc-script="C:\\Program Files\\Keenetic VPN\\vpnc-script-win.js" \
        2>&1 | tail -40

    log "make -j$JOBS libopenconnect.la ..."
    # Build only the library — the CLI (openconnect.exe) references POSIX
    # syslog from 300-keenetic-passwd.patch and won't build on mingw. The
    # GUI links against the .dll directly, so we don't need the .exe.
    make -j"$JOBS" libopenconnect.la 2>&1 | tail -25
)

# ---- 5. stage final zip layout --------------------------------------------
STAGE="$OC_SRC_DIR/_stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/lib" "$STAGE/include"
cp -p "$OC_SRC_DIR/.libs/libopenconnect-5.dll"  "$STAGE/bin/"
cp -p "$OC_SRC_DIR/.libs/libopenconnect.dll.a"  "$STAGE/lib/"
cp -p "$OC_SRC_DIR/openconnect.h"               "$STAGE/include/"
cp -p "$OC_DEPS_DIR/bin/"*.dll                  "$STAGE/bin/"     2>/dev/null || true

"${HOST}-strip" "$STAGE/bin/"*.dll "$STAGE/bin/"*.exe 2>/dev/null || true

log "Building zip..."
rm -f "$OUT_ZIP"
( cd "$STAGE" && zip -qr "$OUT_ZIP" . )
log "Done: $OUT_ZIP ($(stat -c %s "$OUT_ZIP") bytes)"
unzip -l "$OUT_ZIP" | head -60
