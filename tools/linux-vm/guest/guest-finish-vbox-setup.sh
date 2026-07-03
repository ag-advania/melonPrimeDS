#!/usr/bin/env bash
# Run inside the Ubuntu guest after VirtualBox Guest Additions ISO is mounted.
# MelonPrimeDS shared folder must be configured on the host VM.
set -euo pipefail

SHARE_NAME="${SHARE_NAME:-MelonPrimeDS}"
MOUNT="/media/${USER}/${SHARE_NAME}"

echo "==> Installing Guest Additions build deps..."
sudo apt-get update
sudo apt-get install -y build-essential dkms linux-headers-"$(uname -r)" perl

if mountpoint -q /mnt 2>/dev/null; then
  sudo umount /mnt || true
fi
sudo mkdir -p /mnt

if [[ -b /dev/cdrom ]] || [[ -b /dev/sr0 ]]; then
  CD_DEV="/dev/cdrom"
  [[ -b /dev/sr0 ]] && CD_DEV="/dev/sr0"
  echo "==> Running VirtualBox Guest Additions from ${CD_DEV}..."
  sudo mount "$CD_DEV" /mnt
  sudo /mnt/VBoxLinuxAdditions.run
  sudo umount /mnt
else
  echo "No virtual CD found — trying apt packages..."
  sudo apt-get install -y virtualbox-guest-utils virtualbox-guest-x11
fi

echo "==> Adding user to vboxsf group..."
sudo usermod -aG vboxsf "$USER"

echo "==> Reboot required. After login (Ubuntu on Xorg), run:"
cat <<EOF

  ls "${MOUNT}"
  cd "${MOUNT}"
  bash tools/linux-vm/guest/guest-build-only.sh

EOF

if [[ "${REBOOT:-}" == "1" ]] || [[ ! -t 0 ]]; then
  echo "==> Rebooting..."
  sudo reboot
  exit 0
fi

read -r -p "Reboot now? [Y/n] " ans
if [[ "${ans:-Y}" =~ ^[Yy]?$ ]]; then
  sudo reboot
fi
