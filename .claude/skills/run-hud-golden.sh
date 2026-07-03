#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
binary="${1:-"$repo_root/build-mac/melonPrimeDS.app/Contents/MacOS/melonPrimeDS"}"
expected="${2:-"$repo_root/src/frontend/qt_sdl/tests/melonprime-hud-golden.txt"}"
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

"$binary" --melonprime-hud-golden "$tmp"
diff -u "$expected" "$tmp"
