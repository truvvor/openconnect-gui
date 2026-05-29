#!/usr/bin/env bash
# Local build machine — MSYS2 dependency install (replaces exhausted CI).
# Run from an MSYS2 shell:  bash -l scripts/local/00-deps.sh
# Mirrors .github/workflows/build-openconnect.yml deps + adds Qt5/NSIS/cmake
# so the whole project (openconnect + GUI + service) builds in one MINGW64 ABI.
exec > "/c/src/openconnect-gui/scripts/local/00-deps.log" 2>&1
set -eux

rm -f /var/lib/pacman/db.lck || true

# Fresh winget MSYS2: make sure the package signing keyring is usable.
pacman-key --init   || true
pacman-key --populate msys2 || true

# The default MSYS2 mirrors crawl (<1 B/s) from here; PIN to fast mirrors only
# (Yandex RU first, MSYS2 origin fallback) so pacman never touches the slow ones.
for f in mingw64 mingw32 ucrt64 clang64; do
  cat > "/etc/pacman.d/mirrorlist.$f" <<EOF
Server = https://mirror.yandex.ru/mirrors/msys2/mingw/$f
Server = https://repo.msys2.org/mingw/$f
EOF
done
cat > /etc/pacman.d/mirrorlist.msys <<'EOF'
Server = https://mirror.yandex.ru/mirrors/msys2/msys/$arch
Server = https://repo.msys2.org/msys/$arch
EOF

# Robust downloader: curl with retries instead of pacman's built-in "<1 B/s for
# 10s -> abort" which trips on transient slow spots and stale fallback hosts.
grep -q '^XferCommand' /etc/pacman.conf || \
  sed -i '/^\[options\]/a XferCommand = /usr/bin/curl -fL -C - -o %o %u --retry 6 --retry-delay 3 --connect-timeout 30 --speed-time 60 --speed-limit 2048' /etc/pacman.conf

# Two-step: core upgrade first (msys2-runtime/pacman), then the package set.
pacman -Syu --noconfirm || true
pacman -S --needed --noconfirm \
  base-devel git tar wget patch autoconf automake libtool pkgconf make python \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-make \
  mingw-w64-x86_64-qt5-base \
  mingw-w64-x86_64-gnutls \
  mingw-w64-x86_64-libxml2 \
  mingw-w64-x86_64-nettle \
  mingw-w64-x86_64-gmp \
  mingw-w64-x86_64-p11-kit \
  mingw-w64-x86_64-stoken \
  mingw-w64-x86_64-zlib \
  mingw-w64-x86_64-lz4 \
  mingw-w64-x86_64-libiconv \
  mingw-w64-x86_64-gettext \
  mingw-w64-x86_64-libidn2 \
  mingw-w64-x86_64-libarchive \
  mingw-w64-x86_64-nsis

echo "DEPS_DONE"
