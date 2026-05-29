#!/usr/bin/env bash
# Configure + build openconnect-gui (GUI + service + oc-ipc) locally in MINGW64.
# Compiles & links only (no packaging/fixup_bundle — that is P6/NSIS).
# Run in MINGW64:  MSYSTEM=MINGW64 bash -l scripts/local/02-build.sh
export MSYSTEM=MINGW64
source /etc/profile >/dev/null 2>&1 || true
exec > "/c/src/openconnect-gui/scripts/local/02-build.log" 2>&1
set -eux
which gcc g++ cmake mingw32-make pkg-config || true

# Local CMake is 4.x (newer than CI). Old bundled ExternalProjects (spdlog 1.3.1
# etc.) declare cmake_minimum_required < 3.5, which CMake 4 rejects. This env var
# is inherited by every ExternalProject sub-cmake and restores the old minimum.
export CMAKE_POLICY_VERSION_MINIMUM=3.5

REPO=/c/src/openconnect-gui
cd "$REPO"

# Use the MINGW64 cmake/make so Qt5 + openconnect resolve from /mingw64.
cmake -S . -B build-local -G "MinGW Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_MAKE_PROGRAM=mingw32-make.exe \
  -DPROJ_ADMIN_PRIV_ELEVATION=OFF \
  -Dopenconnect-TAG=9.12

# force the resource file to regenerate so the UAC manifest flip is picked up
rm -f build-local/src/openconnect-gui.rc build-local/src/openconnect-gui.rc.tmp

cmake --build build-local -j"$(nproc)"

echo "=== built binaries ==="
find build-local -maxdepth 3 -name 'openconnect-gui*.exe' -o -name 'liboc-ipc*' 2>/dev/null | sort
echo "BUILD_DONE"
