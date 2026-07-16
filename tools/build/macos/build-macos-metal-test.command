#!/usr/bin/env bash
# Double-click in Finder (macOS) or run from Terminal to configure + build the
# Metal-enabled test tree at build-mac-metal.
cd "$(dirname "$0")"
exec ./build-macos-metal-test.sh "$@"
