#!/usr/bin/env bash
set -euo pipefail

# Build-only variant of build-macos-dev.sh (no configure).

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

cd "$REPO_ROOT"

if [[ ! -f build-mac/build.ninja ]]; then
    echo "[melonprime-build] Missing build-mac/build.ninja" >&2
    echo "[melonprime-build] Run build-macos-dev.sh first." >&2
    exit 1
fi

cmake --build build-mac --parallel 4 2>&1
