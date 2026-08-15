#!/usr/bin/env bash
# Repo-root wrapper -> Linux VM incremental build of a Vulkan-enabled tree.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

case "$(uname -s)" in
  Darwin)
    HOST_REPO="$ROOT" exec "${ROOT}/tools/linux-vm/04-guest-build-existing-from-host.sh" "$@"
    ;;
  Linux)
    exec "${ROOT}/tools/linux-vm/guest/guest-build-existing.sh" "${ROOT}" "$@"
    ;;
  *)
    echo "Unsupported host for Linux build wrapper: $(uname -s)" >&2
    exit 1
    ;;
esac
