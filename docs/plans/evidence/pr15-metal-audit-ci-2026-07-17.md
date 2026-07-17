# PR-15 evidence — Metal audit CI gate (partial)

**Date:** 2026-07-17  
**Instruction:** §15 / release gate scaffold

## Implemented

- `tools/ci/audits/run-metal-fullgpu-audits.sh` runs:
  - output-state publication
  - capture experiment scaffold
  - native capture storage
  - segment scheduler
  - fullgpu cutover
- Wired into `.github/workflows/build-macos.yml` after checkout

## Not yet

- Windows/Linux CI metal audit jobs
- TSan / ROM / release binary gates
- SoftRenderer-removal grep gate (blocked on PR-7)

## Local result

```text
PASS: all metal full-Metal-ification audits
```
