#!/usr/bin/env bash
# Double-click in Finder (macOS) or run from Terminal to rebuild an existing
# build-mac-metal tree.
cd "$(dirname "$0")"
exec ./build-macos-metal-test-existing.sh "$@"
