#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="${0:A:h}"
REPO_ROOT="${SCRIPT_DIR:h:h}"
cd "$REPO_ROOT"

APP="$REPO_ROOT/build-mac-metal-test/melonPrimeDS.app/Contents/MacOS/melonPrimeDS"

if [[ ! -x "$APP" ]]; then
  echo "Metal test binary not found: $APP"
  echo "Run tools/macos/build_metal_test.command first."
  exit 1
fi

export MELONPRIME_METAL_PERF="${MELONPRIME_METAL_PERF:-1}"

# Do not force Metal by default. The tester should select
# "Metal (Experimental)" / "Video quality: Metal Test" from the UI.
if [[ "${MELONPRIME_ALLOW_FORCE_METAL:-0}" != "1" ]]; then
  unset MELONPRIME_FORCE_METAL_RENDERER
  unset MELONPRIME_FORCE_METAL_PRESENTER
fi

# Developers can still opt in manually:
# MELONPRIME_ALLOW_FORCE_METAL=1 \
# MELONPRIME_FORCE_METAL_RENDERER=1 \
# MELONPRIME_FORCE_METAL_PRESENTER=1 \
# tools/macos/run_metal_test.command /path/to/rom.nds

exec "$APP" "$@"
