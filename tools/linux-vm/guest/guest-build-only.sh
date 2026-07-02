#!/usr/bin/env bash
# Re-run cmake + build only (skip apt). For use inside guest or via guestcontrol.
set -euo pipefail

REPO_ROOT="${1:-${REPO_ROOT:-/mnt/mp}}"

if [[ ! -f "$REPO_ROOT/CMakeLists.txt" ]]; then
  for p in /mnt/mp "/media/${USER}/MelonPrimeDS" /media/sf_MelonPrimeDS; do
    [[ -f "$p/CMakeLists.txt" ]] && REPO_ROOT="$p" && break
  done
fi

if [[ ! -f "$REPO_ROOT/CMakeLists.txt" ]]; then
  echo "Repo not found. Mount shared folder first." >&2
  exit 1
fi

echo "==> guest-build-only.sh (cmake + build, no apt)"
cd "$REPO_ROOT"

GIT_BRANCH="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo local)"
GIT_HASH="$(git rev-parse HEAD 2>/dev/null || echo unknown)"
BUILD_PROVIDER="${MELONDS_BUILD_PROVIDER:-LinuxVM}"

echo "    Build info: ${GIT_BRANCH} @ ${GIT_HASH:0:12} (${BUILD_PROVIDER})"
rm -rf build-linux

cmake -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON \
  -DMELONDS_EMBED_BUILD_INFO=ON \
  "-DMELONDS_GIT_BRANCH=${GIT_BRANCH}" \
  "-DMELONDS_GIT_HASH=${GIT_HASH}" \
  "-DMELONDS_BUILD_PROVIDER=${BUILD_PROVIDER}"

cmake --build build-linux --parallel "$(nproc)"

echo "Done: $REPO_ROOT/build-linux/melonPrimeDS"
