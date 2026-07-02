#!/usr/bin/env bash
# Create an Ubuntu 22.04 VM in VirtualBox for MelonPrimeDS Linux testing.
#
# Prerequisite: open VirtualBox once from Finder so the background service starts.
# If VirtualBox.app will not open, run: sudo installer -pkg /Applications/VirtualBox.pkg -target /
#
# Usage:
#   ./tools/linux-vm/lib/vbox-create-ubuntu.sh ~/Downloads/ubuntu-22.04.5-desktop-amd64.iso
#
# After Ubuntu is installed inside the guest:
#   - Devices → Insert Guest Additions CD → run the installer in the guest
#   - Reboot, then mount the shared folder (see printed instructions)
#   - Run: ./tools/linux-vm/guest/guest-setup-and-build.sh /media/$USER/MelonPrimeDS
set -euo pipefail

VM_NAME="${VM_NAME:-MelonPrimeDS-Ubuntu2204}"
VM_RAM_MB="${VM_RAM_MB:-4096}"
VM_CPUS="${VM_CPUS:-2}"
VM_DISK_GB="${VM_DISK_GB:-40}"
HOST_REPO="${HOST_REPO:-/Users/admin/git/MelonPrimeDS}"
SHARE_NAME="${SHARE_NAME:-MelonPrimeDS}"

ISO_PATH="${1:-}"
if [[ -z "$ISO_PATH" || ! -f "$ISO_PATH" ]]; then
  echo "Usage: $0 /path/to/ubuntu-22.04-desktop-amd64.iso" >&2
  echo "Example: $0 ~/Downloads/ubuntu-22.04.5-desktop-amd64.iso" >&2
  exit 1
fi

if ! command -v VBoxManage >/dev/null 2>&1; then
  echo "VBoxManage not found. Install VirtualBox first:" >&2
  echo "  https://www.virtualbox.org/wiki/Downloads" >&2
  exit 1
fi

if ! VBoxManage list vms >/dev/null 2>&1; then
  echo "VirtualBox service is not running." >&2
  echo "  1. Open /Applications/VirtualBox.app from Finder (approve macOS security prompts)" >&2
  echo "  2. Install Extension Pack: VirtualBox → Settings → Extensions" >&2
  echo "  3. Re-run this script" >&2
  exit 1
fi

if [[ ! -d "$HOST_REPO" ]]; then
  echo "Host repo not found: $HOST_REPO" >&2
  exit 1
fi

VM_DIR="${HOME}/VirtualBox VMs/${VM_NAME}"
DISK_PATH="${VM_DIR}/${VM_NAME}.vdi"

if VBoxManage list vms 2>/dev/null | grep -F "\"${VM_NAME}\"" >/dev/null 2>&1; then
  echo "VM already exists: ${VM_NAME}"
else
  echo "==> Creating VM: ${VM_NAME}"
  mkdir -p "$VM_DIR"
  VBoxManage createvm --name "$VM_NAME" --ostype Ubuntu_64 --register
  VBoxManage modifyvm "$VM_NAME" \
    --memory "$VM_RAM_MB" \
    --cpus "$VM_CPUS" \
    --vram 128 \
    --graphicscontroller vmsvga \
    --nic1 nat \
    --audio none \
    --usb off \
    --clipboard-mode bidirectional \
    --draganddrop bidirectional \
    --ioapic on \
    --pae on \
    --longmode on \
    --largepages on \
    --rtcuseutc on

  VBoxManage createmedium disk --filename "$DISK_PATH" --size $((VM_DISK_GB * 1024)) --format VDI
  VBoxManage storagectl "$VM_NAME" --name SATA --add sata --controller IntelAhci --portcount 2 --bootable on
  VBoxManage storageattach "$VM_NAME" --storagectl SATA --port 0 --device 0 --type hdd --medium "$DISK_PATH"
  VBoxManage storageattach "$VM_NAME" --storagectl SATA --port 1 --device 0 --type dvddrive --medium emptydrive
fi

echo "==> Attaching installer ISO"
VBoxManage storageattach "$VM_NAME" --storagectl SATA --port 1 --device 0 --type dvddrive --medium "$ISO_PATH"

echo "==> Configuring shared folder: ${SHARE_NAME} -> ${HOST_REPO}"
if VBoxManage showvminfo "$VM_NAME" --machinereadable 2>/dev/null | grep -F "SharedFolderNameMachineMapping" | grep -F "${SHARE_NAME}" >/dev/null 2>&1; then
  VBoxManage sharedfolder remove "$VM_NAME" --name "$SHARE_NAME" || true
fi
VBoxManage sharedfolder add "$VM_NAME" --name "$SHARE_NAME" --hostpath "$HOST_REPO" --automount

cat <<EOF

==> VM ready: ${VM_NAME}

Next steps (host):
  1. Open VirtualBox and start "${VM_NAME}"
  2. Install Ubuntu 22.04 Desktop (amd64)
     - For MelonPrime aim testing, log in with "Ubuntu on Xorg" (not Wayland-only)
  3. In the running guest menu: Devices → Insert Guest Additions CD image
     Install Guest Additions, then reboot

Next steps (guest, after Guest Additions):
  # Shared folder (if automount did not appear):
  sudo usermod -aG vboxsf "\$USER"
  # log out/in, then:
  ls "/media/sf_${SHARE_NAME}"

  cd "/media/sf_${SHARE_NAME}"
  ./tools/linux-vm/guest/guest-setup-and-build.sh "\$(pwd)"
  ./tools/linux-vm/guest/guest-run-smoke.sh "\$(pwd)"
  cd build-linux && ./melonPrimeDS

Japanese UI test:
  export LANG=ja_JP.UTF-8
  ./melonPrimeDS

Start VM from terminal:
  VBoxManage startvm "${VM_NAME}" --type gui

EOF
