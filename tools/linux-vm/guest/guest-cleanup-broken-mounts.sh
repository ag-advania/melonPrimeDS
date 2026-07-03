#!/usr/bin/env bash
# Remove broken VirtualBox automount icons ("4.1 KB Volume") inside Ubuntu.
# Run in VM terminal. Safe to re-run.
set -euo pipefail

SHARE="${1:-MelonPrimeDS}"
MNT="${2:-/mnt/mp}"

echo "==> Unmounting stale vboxsf / media mounts..."
for target in \
  "$MNT" \
  "/media/sf_${SHARE}" \
  "/media/${USER}/${SHARE}" \
  "/run/media/${USER}/${SHARE}"; do
  if mountpoint -q "$target" 2>/dev/null; then
    echo "    umount ${target}"
    sudo umount -l "$target" 2>/dev/null || sudo umount "$target" 2>/dev/null || true
  fi
done

echo "==> Removing empty automount dirs (desktop '4.1 KB Volume' junk)..."
shopt -s nullglob
for dir in "/media/${USER}/"* "/media/sf_${SHARE}"; do
  [[ -e "$dir" ]] || continue
  if mountpoint -q "$dir" 2>/dev/null; then
    continue
  fi
  if [[ -d "$dir" ]] && [[ "$(sudo ls -A "$dir" 2>/dev/null | wc -l | tr -d ' ')" -le 1 ]]; then
    echo "    rmdir ${dir}"
    sudo rmdir "$dir" 2>/dev/null || sudo rm -rf "$dir" 2>/dev/null || true
  fi
done
shopt -u nullglob

echo "==> Eject Guest Additions CD if stuck..."
if mountpoint -q /mnt 2>/dev/null && [[ -f /mnt/VBoxLinuxAdditions.run ]]; then
  sudo umount /mnt 2>/dev/null || true
fi

echo
echo "Done. Desktop ghost volumes should disappear after a few seconds."
echo "Use manual mount only (no automount icons):"
echo
echo "  sudo modprobe vboxsf"
echo "  sudo mkdir -p ${MNT}"
echo "  sudo mount -t vboxsf ${SHARE} ${MNT}"
echo "  ls ${MNT}"
