#!/usr/bin/env bash
# Repo-root wrapper → Linux VM incremental build (see guest-build-existing.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec "${ROOT}/tools/linux-vm/guest/guest-build-existing.sh" "$@"
