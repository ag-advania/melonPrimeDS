#!/usr/bin/env bash
# Fast rebuild inside Ubuntu VM from Mac host (configure skipped).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=vbox-guest-common.sh
source "${SCRIPT_DIR}/lib/vbox-guest-common.sh"

GUEST_ARGS=()

run_build() {
  echo "==> Locating shared MelonPrimeDS folder in guest..."
  REPO="$(find_or_mount_repo)"
  if [[ "$REPO" == "MISSING" || -z "$REPO" ]]; then
    echo "Shared folder still not found." >&2
    print_manual_build_instructions
    exit 1
  fi
  echo "==> Repo: ${REPO}"

  echo "==> Incremental build (skipping CMake configure)..."
  local extra=""
  if ((${#GUEST_ARGS[@]} > 0)); then
    extra="$(printf ' %q' "${GUEST_ARGS[@]}")"
  fi
  guest_sudo "bash '${REPO}/tools/linux-vm/guest/guest-build-existing.sh' '${REPO}'${extra}"

  cat <<EOF

Build finished.

  cd ${REPO}/build-linux
  ./melonPrimeDS

EOF
}

main() {
  GUEST_ARGS=("$@")
  prepare_guest_session
  run_build
}

main "$@"
