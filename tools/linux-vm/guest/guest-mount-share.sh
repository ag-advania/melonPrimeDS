#!/usr/bin/env bash
# Run inside Ubuntu guest when shared folder icon won't open.
set -euo pipefail

SHARE="${1:-MelonPrimeDS}"
MNT="${2:-/mnt/mp}"

echo "==> vboxsf module..."
if ! lsmod | grep -q vboxsf; then
  sudo modprobe vboxsf || {
    echo "vboxsf not loaded — Guest Additions may be missing." >&2
    echo "On Mac: run 02-guest-finish.command" >&2
    exit 1
  }
fi

echo "==> add user to vboxsf..."
sudo usermod -aG vboxsf "$USER"

echo "==> mount ${SHARE} -> ${MNT}..."
sudo mkdir -p "$MNT"
if mountpoint -q "$MNT" 2>/dev/null; then
  sudo umount "$MNT" || true
fi
sudo mount -t vboxsf "$SHARE" "$MNT"

echo "==> contents:"
ls "$MNT" | head -20

cat <<EOF

OK: repo is at ${MNT}

Build:
  bash ${MNT}/tools/linux-vm/guest/guest-build-only.sh

Automount after logout/login may appear at:
  /media/sf_${SHARE}

EOF
