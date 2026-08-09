#!/usr/bin/env bash
# Start the configured VirtualBox Ubuntu VM and prepare the shared folder.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
export HOST_REPO="${HOST_REPO:-${REPO_ROOT}}"
source "${SCRIPT_DIR}/lib/vbox-guest-common.sh"

require_vbox

if ! VBoxManage list vms 2>/dev/null | grep -F "\"${VM_NAME}\"" >/dev/null 2>&1; then
  echo "VM not found: ${VM_NAME}" >&2
  echo "Run ${SCRIPT_DIR}/01-install-ubuntu.command first, or set VM_NAME." >&2
  exit 1
fi

state="$(vm_state)"
case "$state" in
  running)
    echo "==> VM already running: ${VM_NAME}"
    ;;
  paused)
    echo "==> Resuming VM: ${VM_NAME}"
    VBoxManage controlvm "$VM_NAME" resume
    ;;
  poweredoff|poweroff|saved|aborted)
    echo "==> Starting VM: ${VM_NAME} (${state})"
    VBoxManage startvm "$VM_NAME" --type gui
    ;;
  *)
    echo "VM is in state '${state}'. Fix it in VirtualBox first." >&2
    exit 1
    ;;
esac

ensure_host_shared_folder || true

if test_guest_auth; then
  repo="$(mount_guest_share_from_host || true)"
  if [[ "$repo" != "MISSING" && -n "$repo" ]]; then
    echo "==> Shared folder ready: ${repo}"
  else
    echo "Shared folder mount failed." >&2
    print_manual_mount_instructions
  fi
else
  cat <<EOF
==> VM started. Log in to Ubuntu, then mount the shared folder:

  bash ~/mount-mp.sh

If ~/mount-mp.sh is missing, run:

  ${SCRIPT_DIR}/05-mount-share.command

EOF
fi

echo "Done."
