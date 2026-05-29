#!/usr/bin/env bash
exec > /c/src/openconnect-gui/scripts/local/diag.log 2>&1
set -x
echo '--- mirrorlist.mingw64 ---'; cat /etc/pacman.d/mirrorlist.mingw64
echo '--- mirrorlist.msys ---'; cat /etc/pacman.d/mirrorlist.msys
echo '--- yandex mingw64 dir ---'; curl -sSI --max-time 20 https://mirror.yandex.ru/mirrors/msys2/mingw/mingw64/ 2>&1 | head -3
echo '--- yandex nsis pkg ---'; curl -sSI --max-time 20 https://mirror.yandex.ru/mirrors/msys2/mingw/mingw64/mingw-w64-x86_64-nsis-3.12-1-any.pkg.tar.zst 2>&1 | head -3
echo '--- tuna nsis pkg ---'; curl -sSI --max-time 20 https://mirrors.tuna.tsinghua.edu.cn/msys2/mingw/mingw64/mingw-w64-x86_64-nsis-3.12-1-any.pkg.tar.zst 2>&1 | head -3
echo DIAG_DONE
