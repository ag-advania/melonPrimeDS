#!/usr/bin/env bash
# Fix VirtualBox shared folder for MelonPrimeDS (run on Mac in Terminal.app).
set -euo pipefail

VM_NAME="${VM_NAME:-MelonPrimeDS-Ubuntu2204}"
GUEST_USER="${GUEST_USER:-melon}"
SHARE_NAME="${SHARE_NAME:-MelonPrimeDS}"
HOST_REPO="${HOST_REPO:-/Users/admin/git/MelonPrimeDS}"

if ! VBoxManage list vms >/dev/null 2>&1; then
  echo "Open /Applications/VirtualBox.app first." >&2
  exit 1
fi

if [[ ! -d "$HOST_REPO" ]]; then
  echo "Host repo not found: $HOST_REPO" >&2
  exit 1
fi

echo "==> Removing old shared folder config..."
VBoxManage sharedfolder remove "$VM_NAME" --name "$SHARE_NAME" 2>/dev/null || true

echo "==> Adding shared folder (default mount: /media/sf_${SHARE_NAME})..."
VBoxManage sharedfolder add "$VM_NAME" \
  --name "$SHARE_NAME" \
  --hostpath "$HOST_REPO" \
  --automount

echo
VBoxManage showvminfo "$VM_NAME" | grep -A3 "Shared folders:" || true

cat <<EOF

Done on Mac side.

In Ubuntu (short commands — type by hand if paste fails):

  sudo usermod -aG vboxsf ${GUEST_USER}
  sudo modprobe vboxsf
  sudo mkdir -p /mnt/mp
  sudo mount -t vboxsf ${SHARE_NAME} /mnt/mp
  ls /mnt/mp

If ls shows files: log out and log in again, then also check:

  ls /media/sf_${SHARE_NAME}

Build:

  bash /mnt/mp/tools/linux-vm/guest/guest-build-only.sh

EOF
