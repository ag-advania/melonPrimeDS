#!/usr/bin/env bash
# Repo-root wrapper -> Linux configure + Vulkan-enabled build.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
exec "${ROOT}/tools/linux-vm/guest/guest-build-only.sh" "${ROOT}" "$@"
