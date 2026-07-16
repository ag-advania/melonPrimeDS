#!/usr/bin/env bash
set -euo pipefail

# Build-only variant of build-macos-metal-test.sh (no configure).

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

cd "$REPO_ROOT"

if [[ ! -f build-mac-metal/build.ninja ]]; then
    echo "[melonprime-build] Missing build-mac-metal/build.ninja" >&2
    echo "[melonprime-build] Run build-macos-metal-test.sh first." >&2
    exit 1
fi

cmake --build build-mac-metal --parallel 4 2>&1

echo "[melonprime-build] Metal test build ready: build-mac-metal/melonPrimeDS.app"
echo "[melonprime-build] Launch: open build-mac-metal/melonPrimeDS.app"
