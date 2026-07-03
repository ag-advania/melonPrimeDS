#!/usr/bin/env bash
# Eject the Ubuntu installer ISO, then attach VirtualBox Guest Additions.
# Fixes: VERR_PDM_MEDIA_LOCKED when Insert Guest Additions is clicked while
# the installer ISO is still in the virtual DVD drive.
#
# Usage:
#   ./tools/linux-vm/lib/vbox-mount-guest-additions.sh
#   ./tools/linux-vm/lib/vbox-mount-guest-additions.sh MyOtherVmName
set -euo pipefail

VM_NAME="${1:-MelonPrimeDS-Ubuntu2204}"
GA_ISO="/Applications/VirtualBox.app/Contents/MacOS/VBoxGuestAdditions.iso"
STORCTL="${STORCTL:-SATA}"
PORT="${PORT:-1}"
DEVICE="${DEVICE:-0}"

if ! VBoxManage list vms >/dev/null 2>&1; then
  echo "VirtualBox is not running. Open /Applications/VirtualBox.app first." >&2
  exit 1
fi

if [[ ! -f "$GA_ISO" ]]; then
  echo "Guest Additions ISO not found: $GA_ISO" >&2
  exit 1
fi

STATE="$(VBoxManage showvminfo "$VM_NAME" --machinereadable | grep '^VMState=' | cut -d= -f2 | tr -d '"')"
if [[ "$STATE" == "running" ]]; then
  echo "VM is running. Ejecting installer ISO from the guest side first is safest."
  echo "In Ubuntu: open Files → eject 'Ubuntu 22.04.5 LTS amd64', or run:"
  echo "  sudo eject /dev/sr0 2>/dev/null || true"
  if [[ -t 0 ]]; then
    read -r -p "Press Enter after ejecting in the guest (or Ctrl-C to abort)..."
  else
    echo "(non-interactive: continuing in 5s...)"
    sleep 5
  fi
fi

echo "==> Removing media from ${STORCTL} port ${PORT}..."
VBoxManage storageattach "$VM_NAME" \
  --storagectl "$STORCTL" --port "$PORT" --device "$DEVICE" \
  --type dvddrive --medium emptydrive

echo "==> Attaching Guest Additions ISO..."
VBoxManage storageattach "$VM_NAME" \
  --storagectl "$STORCTL" --port "$PORT" --device "$DEVICE" \
  --type dvddrive --medium "$GA_ISO"

cat <<EOF

Done.

If the VM is running, Ubuntu should show a new CD in Files.
Install inside the guest:

  sudo mount /dev/cdrom /mnt
  sudo /mnt/VBoxLinuxAdditions.run
  sudo reboot

If the CD does not appear, power off the VM completely, run this script again,
then start the VM.

Fallback (no ISO mount):
  sudo apt update
  sudo apt install -y virtualbox-guest-utils virtualbox-guest-x11
  sudo reboot

EOF
