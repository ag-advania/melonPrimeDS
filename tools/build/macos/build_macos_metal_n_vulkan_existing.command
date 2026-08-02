#!/usr/bin/env bash
# Incremental rebuild of an already configured build-mac-vulkan tree
# (Metal + Vulkan). Skips the CMake configure step.
#
# Use build_macos_metal_n_vulkan.command instead when CMake options,
# dependencies, or the toolchain changed.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "$SCRIPT_DIR/build-macos-vulkan.sh" \
    --build-only \
    --with-metal \
    "$@"
