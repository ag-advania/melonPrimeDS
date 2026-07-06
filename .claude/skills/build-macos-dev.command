#!/usr/bin/env bash
# Double-click in Finder (macOS) or run from Terminal to configure + build build-mac.
cd "$(dirname "$0")"
exec ./build-macos-dev.sh "$@"
