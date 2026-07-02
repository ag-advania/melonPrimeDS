#!/usr/bin/env bash
# Run inside Ubuntu guest with: echo 'PASSWORD' | sudo -S bash guest-finish-noninteractive.sh
set -euo pipefail

SHARE_NAME="${SHARE_NAME:-MelonPrimeDS}"
GUEST_USER="${GUEST_USER:-melon}"
MOUNT="/media/${GUEST_USER}/${SHARE_NAME}"

export DEBIAN_FRONTEND=noninteractive

echo "==> Installing Guest Additions build deps..."
apt-get update
apt-get install -y build-essential dkms linux-headers-"$(uname -r)" perl

mkdir -p /mnt
if mountpoint -q /mnt 2>/dev/null; then umount /mnt || true; fi

if [[ -b /dev/sr0 ]] || [[ -b /dev/cdrom ]]; then
  CD_DEV="/dev/sr0"
  [[ -b /dev/cdrom ]] && CD_DEV="/dev/cdrom"
  echo "==> Running VirtualBox Guest Additions from ${CD_DEV}..."
  mount "$CD_DEV" /mnt
  /mnt/VBoxLinuxAdditions.run --nox11
  umount /mnt || true
else
  echo "==> No virtual CD — installing guest utils from apt..."
  apt-get install -y virtualbox-guest-utils virtualbox-guest-x11
fi

echo "==> Adding ${GUEST_USER} to vboxsf group..."
usermod -aG vboxsf "$GUEST_USER"

echo "==> Guest Additions setup complete."
echo "After reboot: cd ${MOUNT} && bash tools/linux-vm/guest/guest-build-only.sh"

if [[ "${REBOOT:-1}" == "1" ]]; then
  echo "==> Rebooting..."
  reboot
fi
