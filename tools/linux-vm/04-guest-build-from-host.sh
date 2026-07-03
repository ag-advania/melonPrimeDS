#!/usr/bin/env bash
# Build MelonPrimeDS inside the Ubuntu VM from the Mac host.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=vbox-guest-common.sh
source "${SCRIPT_DIR}/lib/vbox-guest-common.sh"

run_build() {
  echo "==> Locating shared MelonPrimeDS folder in guest..."
  REPO="$(find_or_mount_repo)"
  if [[ "$REPO" == "MISSING" || -z "$REPO" ]]; then
    echo "Shared folder still not found." >&2
    print_manual_build_instructions
    exit 1
  fi
  echo "==> Repo: ${REPO}"

  echo "==> Building (this takes several minutes)..."
  guest_sudo "bash '${REPO}/tools/linux-vm/guest/guest-build-only.sh' '${REPO}'"

  cat <<EOF

Build finished.

In the Ubuntu VM terminal:

  cd ${REPO}/build-linux
  ./melonPrimeDS

Japanese UI test:
  export LANG=ja_JP.UTF-8
  ./melonPrimeDS

EOF
}

main() {
  if [[ "${MANUAL:-0}" == "1" ]]; then
    print_manual_build_instructions
    exit 0
  fi
  prepare_guest_session
  run_build
}

main "$@"
