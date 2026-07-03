#!/usr/bin/env bash
# Run Guest Additions setup inside the Ubuntu VM from the Mac host.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=vbox-guest-common.sh
source "${SCRIPT_DIR}/lib/vbox-guest-common.sh"

GUEST_SCRIPT="${REPO_GUEST}/tools/linux-vm/guest/guest-finish-noninteractive.sh"
GUEST_SCRIPT_ALT="${REPO_GUEST_ALT}/tools/linux-vm/guest/guest-finish-noninteractive.sh"
GA_ISO="/Applications/VirtualBox.app/Contents/MacOS/VBoxGuestAdditions.iso"

attach_guest_additions_iso() {
  [[ -f "$GA_ISO" ]] || return 0
  echo "==> Attaching Guest Additions ISO..."
  VBoxManage storageattach "$VM_NAME" --storagectl SATA --port 1 --device 0 \
    --type dvddrive --medium emptydrive 2>/dev/null || true
  VBoxManage storageattach "$VM_NAME" --storagectl SATA --port 1 --device 0 \
    --type dvddrive --medium "$GA_ISO"
}

run_finish() {
  guest_sudo "
    for s in '${GUEST_SCRIPT}' '${GUEST_SCRIPT_ALT}'; do
      if [[ -f \"\$s\" ]]; then exec bash \"\$s\"; fi
    done
    echo 'guest-finish script not found on shared folder' >&2
    exit 1
  "
}

require_vbox
ensure_vm_running
attach_guest_additions_iso

echo
echo "VirtualBox should show Ubuntu. Log in if needed."
wait_for_guest_login

echo "==> Running guest finish setup (may reboot VM at end)..."
run_finish || true

cat <<EOF

If the VM rebooted, log in again (**Ubuntu on Xorg**), then build:

  ${SCRIPT_DIR}/04-guest-build.command

Or inside Ubuntu:

  bash /mnt/mp/tools/linux-vm/guest/guest-build-only.sh

EOF
