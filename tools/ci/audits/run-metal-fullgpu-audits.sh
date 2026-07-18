#!/usr/bin/env bash
# Run standing Metal full-Metal-ification static audits (PR-15 scaffold).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"

audits=(
  tools/ci/audits/audit-metal-output-state-publication.py
  tools/ci/audits/audit-metal-capture-experiment-scaffold.py
  tools/ci/audits/audit-metal-native-capture-storage.py
  tools/ci/audits/audit-metal-capture-segment-scheduler.py
  tools/ci/audits/audit-metal-capture-fullgpu-cutover.py
  tools/ci/audits/audit-metal-compute-raster-reference-removal.py
  tools/ci/audits/audit-metal-presenter-metaltexture-only.py
  tools/ci/audits/audit-metal-radar-native.py
  tools/ci/audits/audit-metal-hud-command-list.py
  tools/ci/audits/audit-metal-osd-splash-native.py
  tools/ci/audits/audit-metal-shader-asset-metallib.py
  tools/ci/audits/audit-metal-forbidden-paths.py
  tools/ci/audits/audit-metal-frame-bootstrap.py
)

# All of the above are pure-Python static text/regex checks over tracked
# source files (no compilation, no Metal.framework, no `xcrun`), so this
# script has no macOS/Metal-SDK dependency itself and can run unmodified on
# Windows/Linux CI runners with `python3` and `bash` on PATH -- only the
# actual Metal build (a separate CI step) requires macOS.

failed=0
for audit in "${audits[@]}"; do
  echo "==> python3 ${audit}"
  if ! python3 "${audit}"; then
    failed=1
  fi
done

if [[ "${failed}" -ne 0 ]]; then
  echo "FAIL: one or more metal audits failed"
  exit 1
fi
echo "PASS: all metal full-Metal-ification audits"
