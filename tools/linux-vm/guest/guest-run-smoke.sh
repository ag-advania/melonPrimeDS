#!/usr/bin/env bash
# Minimal post-build smoke checklist (no ROM required).
set -euo pipefail

REPO_ROOT="${1:-${REPO_ROOT:-$HOME/MelonPrimeDS}}"
BIN="$REPO_ROOT/build-linux/melonPrimeDS"

if [[ ! -x "$BIN" ]]; then
  echo "Missing binary: $BIN" >&2
  echo "Run tools/linux-vm/guest/guest-setup-and-build.sh first." >&2
  exit 1
fi

echo "==> Locale"
echo "LANG=${LANG:-<unset>}"
echo "LANGUAGE=${LANGUAGE:-<unset>}"
echo "LC_ALL=${LC_ALL:-<unset>}"
if command -v locale >/dev/null 2>&1; then
  locale | grep -E '^(LANG|LANGUAGE|LC_)' || true
fi

echo
echo "==> Display"
echo "XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-<unset>}"
echo "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<unset>}"
echo "DISPLAY=${DISPLAY:-<unset>}"

echo
echo "==> Binary"
file "$BIN"
ldd "$BIN" 2>/dev/null | grep -E 'Qt6|SDL2|not found' || true

echo
echo "Launch manually from a desktop terminal:"
echo "  cd \"$REPO_ROOT/build-linux\" && ./melonPrimeDS"
