#!/usr/bin/env bash
# Build the unprivileged IPC test client into the staged deploy dir.
export MSYSTEM=MINGW64
source /etc/profile >/dev/null 2>&1 || true
exec > /c/src/openconnect-gui/scripts/local/04-octest.log 2>&1
set -eux
g++ -std=c++17 \
  /c/src/openconnect-gui/scripts/local/octest.cpp \
  /c/src/openconnect-gui/src/ipc/protocol.cpp \
  /c/src/openconnect-gui/src/ipc/profile.cpp \
  -I/c/src/openconnect-gui/src \
  -I/mingw64/include -I/mingw64/include/QtCore -I/mingw64/include/QtNetwork \
  -lQt5Core -lQt5Network \
  -o /c/oc-stage/octest.exe
echo OCTEST_BUILT
