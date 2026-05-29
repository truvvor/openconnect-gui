#!/usr/bin/env bash
# Unattended local pipeline: wait for deps -> build openconnect -> build project.
# Launch once:  bash -l scripts/local/99-pipeline.sh   (poll 99-pipeline.log)
export MSYSTEM=MINGW64
source /etc/profile >/dev/null 2>&1 || true
exec > /c/src/openconnect-gui/scripts/local/99-pipeline.log 2>&1
set -ux
L=/c/src/openconnect-gui/scripts/local

# 1) Wait until the deps install (00-deps.sh) reports DEPS_DONE (≤ ~40 min).
for i in $(seq 1 160); do
  grep -q DEPS_DONE "$L/00-deps.log" && { echo ">>> deps ready"; break; }
  grep -q 'не удалось завершить транзакцию' "$L/00-deps.log" && { echo ">>> DEPS_FAILED"; exit 1; }
  sleep 15
done
grep -q DEPS_DONE "$L/00-deps.log" || { echo ">>> DEPS_TIMEOUT"; exit 1; }

# 2) Build patched openconnect 9.12 -> external/ zips.
echo ">>> building openconnect"
bash "$L/01-openconnect.sh" || { echo ">>> OPENCONNECT_FAILED rc=$?"; exit 2; }
grep -q OPENCONNECT_DONE "$L/01-openconnect.log" || { echo ">>> OC_INCOMPLETE"; exit 2; }

# 3) Configure + build GUI + service + oc-ipc.
echo ">>> building project"
bash "$L/02-build.sh" || { echo ">>> BUILD_FAILED rc=$?"; exit 3; }
grep -q BUILD_DONE "$L/02-build.log" || { echo ">>> BUILD_INCOMPLETE"; exit 3; }

echo ">>> PIPELINE_DONE"
