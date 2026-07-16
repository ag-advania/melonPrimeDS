#!/usr/bin/env bash
set -euo pipefail

# Metal-enabled test build, separate from the canonical build-mac tree so it
# never disturbs whatever MELONPRIME_ENABLE_METAL setting that tree has
# cached. Configures + builds build-mac-metal with Metal explicitly ON.
# Equivalent to:
#   cd <repo-root> && cmake -B build-mac-metal ... -DMELONPRIME_ENABLE_METAL=ON ... && cmake --build build-mac-metal --parallel 4 2>&1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

cd "$REPO_ROOT"

cmake -B build-mac-metal -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON \
  -DMELONPRIME_ENABLE_METAL=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libarchive)" \
  -DUSE_QT6=ON && \
cmake --build build-mac-metal --parallel 4 2>&1

echo "[melonprime-build] Metal test build ready: build-mac-metal/melonPrimeDS.app"
echo "[melonprime-build] Launch: open build-mac-metal/melonPrimeDS.app"
