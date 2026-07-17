# PR-3 evidence — native canonical capture storage (R16Uint)

**Date:** 2026-07-17  
**HEAD base:** `f83aeea5` (PR-2)  
**Instruction:** §9

## Implemented

- `Capture128`/`Capture256`/`Snapshot128`/`Snapshot256` → native `MTLPixelFormatR16Uint` (128×128×16 / 256×256×4)
- Scale change updates `CaptureState->Scale` only (no texture recreate)
- Capture kernel writes native RGB5551; samples hi-res Engine A/3D at scale mid-texel
- CPU upload: native RGB5551 blit (scratch ≤128 KiB; staging cap 128 KiB)
- CPU readback: direct texture→buffer blit (decode kernel removed)
- 2D BG/OBJ + Compute 3D capture sampling via `ushort` `.read()` + unpack
- Dummy capture textures + `SetCaptureTextures` fail-closed on `R16Uint`
- Static audit: `tools/ci/audits/audit-metal-native-capture-storage.py`

## Explicitly unchanged

- CaptureCnt Full-GPU exclusion (PR-5)
- Enhanced/LRU scale cache (optional later; correctness without it)
- Segmented capture scheduler (PR-4)
- SoftRenderer inheritance / presenter CpuBgra policy

## Validation

| Check | Result |
|---|---|
| native capture storage audit | PASS |
| capture experiment scaffold audit | PASS |
| output-state publication audit | PASS |
| macOS Intel Metal ON rebuild | PASS |
| scale 1–16 canonical identity / ROM pixel diff | **not run** |

## Gate decision

PR-3 **accepted as storage cutover** for continuing to PR-4 (segment scheduler). Runtime/ROM parity remains open.
