#!/usr/bin/env bash
# Double-click in Finder (macOS) or run from Terminal.
#
# Builds melonPrimeDS with BOTH the native Metal renderer and the Vulkan
# (MoltenVK) renderer compiled in. They coexist: Settings -> Video -> 3D
# renderer lists Metal, Metal Compute, and Vulkan as separate choices, each
# with its own video settings, and picking one never changes the others.
#
# Missing Homebrew Vulkan dependencies are installed automatically.
# Output: build-mac-vulkan/melonPrimeDS.app
#
# Options are forwarded to build-macos-vulkan.sh next to this file
# (--jobs N, --release, --debug, --build-only, --no-bundle, --open, --help).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec "$SCRIPT_DIR/build-macos-vulkan.sh" \
    --install-deps \
    --with-metal \
    "$@"
