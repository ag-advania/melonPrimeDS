#!/usr/bin/env bash
# Mount the MelonPrimeDS shared folder in the Ubuntu guest (no build).
# Run from Terminal.app on the Mac host.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=vbox-guest-common.sh
source "${SCRIPT_DIR}/lib/vbox-guest-common.sh"

run_mount() {
  echo "==> Mounting shared folder in guest (build skipped)..."
  REPO="$(mount_guest_share_from_host || true)"
  if [[ "$REPO" == "MISSING" || -z "$REPO" ]]; then
    echo "Shared folder mount failed." >&2
    print_manual_mount_instructions
    exit 1
  fi

  echo "==> Repo: ${REPO}"
  guest_run "ls '${REPO}' | head -10"

  guest_sudo "
    if [[ -f '${REPO}/tools/linux-vm/guest/guest-mount-share.sh' ]]; then
      install -m 755 '${REPO}/tools/linux-vm/guest/guest-mount-share.sh' /usr/local/sbin/melon-mount-mp.sh
      cp /usr/local/sbin/melon-mount-mp.sh '${HOME}/mount-mp.sh'
      chown '${GUEST_USER}:${GUEST_USER}' '${HOME}/mount-mp.sh'
      chmod +x '${HOME}/mount-mp.sh'
    fi
  " 2>/dev/null || true

  echo "==> Adding Desktop shortcut and Files bookmark..."
  guest_run "bash '${REPO}/tools/linux-vm/guest/guest-install-desktop-shortcut.sh' '${REPO}'" 60000 || true

  cat <<EOF

OK — shared folder is at ${REPO}

Desktop: double-click "MelonPrimeDS (Mac shared)"
Files app: sidebar bookmark "MelonPrimeDS"

Inside Ubuntu (after reboot, when /mnt/mp is gone):

  bash ~/mount-mp.sh

Build later (optional):

  bash ${REPO}/tools/linux-vm/guest/guest-build-only.sh

Run binary:

  ${REPO}/build-linux/melonPrimeDS

EOF
}

main() {
  if [[ "${MANUAL:-0}" == "1" ]]; then
    print_manual_mount_instructions
    exit 0
  fi
  export LOGIN_WAIT_SEC="${LOGIN_WAIT_SEC:-90}"
  prepare_guest_session_for_mount
  run_mount
}

main "$@"
