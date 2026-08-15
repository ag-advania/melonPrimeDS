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

VULKAN_HEADERS_COMMIT="${VULKAN_HEADERS_COMMIT:-b5c8f996196ba4aa6d8f97e52b5d3b6e70f7e4e2}"
MELONPRIME_VULKAN_HEADERS_DIR="${MELONPRIME_VULKAN_HEADERS_DIR:-$REPO_ROOT/Vulkan-Headers/include}"
VULKAN_HEADERS_REPO="$(dirname "$MELONPRIME_VULKAN_HEADERS_DIR")"

echo "==> Installing pinned Vulkan headers..."
if [[ ! -d "$VULKAN_HEADERS_REPO/.git" ]]; then
  rm -rf "$VULKAN_HEADERS_REPO"
  git init "$VULKAN_HEADERS_REPO"
  git -C "$VULKAN_HEADERS_REPO" remote add origin https://github.com/KhronosGroup/Vulkan-Headers.git
fi
git -C "$VULKAN_HEADERS_REPO" fetch --depth 1 origin "$VULKAN_HEADERS_COMMIT"
git -C "$VULKAN_HEADERS_REPO" checkout --detach FETCH_HEAD
grep -F 'VK_NV_LOW_LATENCY_2_EXTENSION_NAME' "$MELONPRIME_VULKAN_HEADERS_DIR/vulkan/vulkan_core.h" >/dev/null
grep -F 'VK_AMD_ANTI_LAG_EXTENSION_NAME' "$MELONPRIME_VULKAN_HEADERS_DIR/vulkan/vulkan_core.h" >/dev/null

cmake -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON \
  -DMELONPRIME_ENABLE_VULKAN=ON \
  -DMELONPRIME_FORCE_DISABLE_VULKAN=OFF \
  "-DMELONPRIME_VULKAN_INCLUDE_DIR=${MELONPRIME_VULKAN_HEADERS_DIR}" \
  -DMELONDS_EMBED_BUILD_INFO=ON \
  "-DMELONDS_GIT_BRANCH=${GIT_BRANCH}" \
  "-DMELONDS_GIT_HASH=${GIT_HASH}" \
  "-DMELONDS_BUILD_PROVIDER=${BUILD_PROVIDER}"

cmake --build build-linux --parallel "$(nproc)"

echo "Done: $REPO_ROOT/build-linux/melonPrimeDS"
