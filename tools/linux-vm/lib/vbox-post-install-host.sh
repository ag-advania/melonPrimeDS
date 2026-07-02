#!/usr/bin/env bash
# Run on the Mac when Ubuntu is already at the login/desktop screen.
set -euo pipefail

VM_NAME="${VM_NAME:-MelonPrimeDS-Ubuntu2204}"
GUEST_USER="${GUEST_USER:-melon}"
GUEST_PASS="${GUEST_PASS:-melon}"
SHARE_NAME="${SHARE_NAME:-MelonPrimeDS}"
GA_ISO="/Applications/VirtualBox.app/Contents/MacOS/VBoxGuestAdditions.iso"

if ! VBoxManage list vms >/dev/null 2>&1; then
  echo "Open VirtualBox.app first." >&2
  exit 1
fi

echo "==> Ejecting installer ISO and attaching Guest Additions..."
VBoxManage storageattach "$VM_NAME" --storagectl SATA --port 1 --device 0 \
  --type dvddrive --medium emptydrive
VBoxManage storageattach "$VM_NAME" --storagectl SATA --port 1 --device 0 \
  --type dvddrive --medium "$GA_ISO"

cat <<EOF

Done. In the Ubuntu VM:

  1. Sign in: ${GUEST_USER} / ${GUEST_PASS}
  2. At login, choose **Ubuntu on Xorg** (gear icon)
  3. Guest terminal:

     bash /media/${GUEST_USER}/${SHARE_NAME}/tools/linux-vm/guest/guest-finish-vbox-setup.sh

EOF
