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

MOUNT_HELPER="/home/${GUEST_USER}/mount-mp.sh"
MOUNT_SRC="/usr/local/sbin/melon-mount-mp.sh"
# Minimal copy until host share is available; replaced when guest-mount-share runs.
cat > "$MOUNT_HELPER" << 'MOUNTEOF'
#!/usr/bin/env bash
set -euo pipefail
SHARE="${1:-MelonPrimeDS}"
MNT="${2:-/mnt/mp}"
if [[ -x /usr/local/sbin/melon-mount-mp.sh ]]; then
  exec /usr/local/sbin/melon-mount-mp.sh "$@"
fi
sudo systemctl start vboxadd-service 2>/dev/null || true
sudo modprobe vboxsf
for p in "/media/sf_${SHARE}" "/media/${USER}/${SHARE}"; do
  if [[ -f "${p}/CMakeLists.txt" ]]; then
    sudo mkdir -p "$(dirname "$MNT")"
    sudo ln -sfn "$p" "$MNT" 2>/dev/null || sudo mount --bind "$p" "$MNT"
    ls "$MNT" | head -5
    exit 0
  fi
done
sudo mkdir -p "$MNT"
if mountpoint -q "$MNT" 2>/dev/null; then
  sudo umount "$MNT" 2>/dev/null || true
fi
for i in 1 2 3 4 5; do
  sudo mount -t vboxsf "$SHARE" "$MNT" && break
  sleep 2
done
test -f "${MNT}/CMakeLists.txt"
ls "$MNT" | head -5
MOUNTEOF
cp "$MOUNT_HELPER" "$MOUNT_SRC"
chown "${GUEST_USER}:${GUEST_USER}" "$MOUNT_HELPER"
chmod +x "$MOUNT_HELPER" "$MOUNT_SRC"

echo "==> Guest Additions setup complete."
echo "After reboot: bash ~/mount-mp.sh   (shared folder only, no build)"

if [[ "${REBOOT:-1}" == "1" ]]; then
  echo "==> Rebooting..."
  reboot
fi
