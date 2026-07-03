#!/usr/bin/env bash
# Reset VirtualBox shared folder: ONE share, NO automount (stops "4.1 KB Volume" spam).
# Run on Mac in Terminal.app. VM should be powered OFF for a clean permanent fix.
set -euo pipefail

VM_NAME="${VM_NAME:-MelonPrimeDS-Ubuntu2204}"
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

state="$(VBoxManage showvminfo "$VM_NAME" --machinereadable 2>/dev/null | grep '^VMState=' | cut -d= -f2 | tr -d '"')"
if [[ "$state" == "running" ]]; then
  echo "WARNING: VM is running. Shutting down for a clean permanent share config..." >&2
  VBoxManage controlvm "$VM_NAME" acpipowerbutton 2>/dev/null || true
  for _ in $(seq 1 60); do
    state="$(VBoxManage showvminfo "$VM_NAME" --machinereadable | grep '^VMState=' | cut -d= -f2 | tr -d '"')"
    [[ "$state" == "poweroff" || "$state" == "poweredoff" ]] && break
    sleep 2
  done
fi

echo "==> Removing ALL MelonPrimeDS shared folder entries (machine + transient)..."
VBoxManage sharedfolder remove "$VM_NAME" --name "$SHARE_NAME" 2>/dev/null || true
VBoxManage sharedfolder remove "$VM_NAME" --name "$SHARE_NAME" --transient 2>/dev/null || true

echo "==> Adding ONE shared folder (automount OFF — use /mnt/mp manually in guest)..."
VBoxManage sharedfolder add "$VM_NAME" \
  --name "$SHARE_NAME" \
  --hostpath "$HOST_REPO"

echo
VBoxManage showvminfo "$VM_NAME" | grep -A5 "Shared folders:" || true

cat <<EOF

Done on Mac.

1. Start the VM and log in (Ubuntu on Xorg).
2. Remove desktop junk (Ubuntu terminal) — type by hand if no share mounted yet:

   sudo umount -l /media/melon/${SHARE_NAME} 2>/dev/null; sudo rmdir /media/melon/${SHARE_NAME} 2>/dev/null

3. Install / fix Guest Additions (ISO):
   VirtualBox menu → Devices → Insert Guest Additions CD image...
   Then in Ubuntu:

   sudo mount /dev/cdrom /mnt
   sudo /mnt/VBoxLinuxAdditions.run
   sudo reboot

4. After reboot — manual mount ONLY (no desktop volume icons):

   sudo modprobe vboxsf
   sudo mkdir -p /mnt/mp
   sudo mount -t vboxsf ${SHARE_NAME} /mnt/mp
   ls /mnt/mp

Do NOT run 05-mount-share repeatedly — Guest Additions must work first.

EOF
