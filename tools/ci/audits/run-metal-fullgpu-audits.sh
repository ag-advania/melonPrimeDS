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
)

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
