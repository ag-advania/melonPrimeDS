#!/usr/bin/env bash
set -euo pipefail

# Canonical macOS dev configure + build (fixed flags, parallel 4).
# Equivalent to:
#   cd <repo-root> && cmake -B build-mac ... ON ... && cmake --build build-mac --parallel 4 2>&1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

cd "$REPO_ROOT"

cmake -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix libarchive)" \
  -DUSE_QT6=ON && \
cmake --build build-mac --parallel 4 2>&1
