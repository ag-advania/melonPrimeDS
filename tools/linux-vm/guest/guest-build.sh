#!/usr/bin/env bash
# Run inside Ubuntu guest terminal (Ctrl+Alt+T) when Mac-side automation is stuck.
set -euo pipefail

for p in "/media/${USER}/MelonPrimeDS" "/media/sf_MelonPrimeDS" "$HOME/MelonPrimeDS"; do
  if [[ -f "$p/CMakeLists.txt" ]]; then
    REPO="$p"
    break
  fi
done

if [[ -z "${REPO:-}" ]]; then
  echo "MelonPrimeDS shared folder not found." >&2
  echo "Try: ls /media/sf_MelonPrimeDS  or  ls /media/${USER}/MelonPrimeDS" >&2
  echo "If missing: log out/in, or re-run Guest-Finish on the Mac." >&2
  exit 1
fi

echo "==> Using repo: $REPO"
exec "$REPO/tools/linux-vm/guest/guest-setup-and-build.sh" "$REPO"
