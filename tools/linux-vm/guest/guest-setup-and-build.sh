#!/usr/bin/env bash
# Run inside an Ubuntu 22.04+ guest (Desktop, X11 session recommended).
# Usage:
#   ./tools/linux-vm/guest/guest-setup-and-build.sh
#   ./tools/linux-vm/guest/guest-setup-and-build.sh /path/to/MelonPrimeDS
set -euo pipefail

REPO_ROOT="${1:-${REPO_ROOT:-$HOME/MelonPrimeDS}}"

if [[ ! -f "$REPO_ROOT/CMakeLists.txt" ]]; then
  echo "MelonPrimeDS repo not found at: $REPO_ROOT" >&2
  echo "Clone first, e.g.: git clone <url> ~/MelonPrimeDS" >&2
  exit 1
fi

echo "==> Installing build dependencies (matches CI Ubuntu workflow)..."
sudo apt update
sudo apt install -y \
  git cmake ninja-build extra-cmake-modules pkgconf \
  libpcap0.8-dev libsdl2-dev libenet-dev \
  qt6-base-dev qt6-base-private-dev qt6-multimedia-dev qt6-wayland-dev libqt6svg6-dev \
  libarchive-dev libzstd-dev libfuse2 libfaad-dev libxi-dev

echo "==> Configuring..."
cd "$REPO_ROOT"

CMAKE_BUILD_ARGS=(
  -DCMAKE_INSTALL_PREFIX=/usr
  -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON
)

if git -C "$REPO_ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  GIT_BRANCH="$(git -C "$REPO_ROOT" rev-parse --abbrev-ref HEAD)"
  GIT_HASH="$(git -C "$REPO_ROOT" rev-parse HEAD)"
  BUILD_PROVIDER="${MELONDS_BUILD_PROVIDER:-LinuxVM}"
  CMAKE_BUILD_ARGS+=(
    -DMELONDS_EMBED_BUILD_INFO=ON
    "-DMELONDS_GIT_BRANCH=${GIT_BRANCH}"
    "-DMELONDS_GIT_HASH=${GIT_HASH}"
    "-DMELONDS_BUILD_PROVIDER=${BUILD_PROVIDER}"
  )
  echo "    Build info: ${GIT_BRANCH} @ ${GIT_HASH:0:12} (${BUILD_PROVIDER})"
  rm -rf build-linux
else
  CMAKE_BUILD_ARGS+=(-DMELONDS_EMBED_BUILD_INFO=OFF)
  echo "    No .git metadata — build info embedding disabled."
fi

cmake -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release "${CMAKE_BUILD_ARGS[@]}"

echo "==> Building..."
cmake --build build-linux --parallel "$(nproc)"

echo
echo "Done. Binary:"
echo "  $REPO_ROOT/build-linux/melonPrimeDS"
echo
echo "Quick checks:"
echo "  echo \"LANG=\$LANG LANGUAGE=\$LANGUAGE\""
echo "  cd \"$REPO_ROOT/build-linux\" && ./melonPrimeDS"
echo
echo "Notes:"
echo "  - Log in with \"Ubuntu on Xorg\" for XInput2 raw aim (not Wayland-only)."
echo "  - Japanese UI: export LANG=ja_JP.UTF-8 before launch if needed."
