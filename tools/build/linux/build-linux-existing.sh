#!/usr/bin/env bash
# Repo-root wrapper -> Linux VM incremental build of a Vulkan-enabled tree.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
exec "${ROOT}/tools/linux-vm/guest/guest-build-existing.sh" "${ROOT}" "$@"
