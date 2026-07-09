#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
REPO_ROOT="${SCRIPT_DIR:h:h}"
cd "$REPO_ROOT"

BUILD_DIR="build-mac-metal-test"

cmake -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMELONPRIME_ENABLE_METAL=ON \
  -DMELONPRIME_FORCE_DISABLE_METAL=OFF

cmake --build "$BUILD_DIR" --parallel "$(sysctl -n hw.ncpu)"

echo ""
echo "Metal test build complete."
echo "Build dir: $REPO_ROOT/$BUILD_DIR"
echo ""
