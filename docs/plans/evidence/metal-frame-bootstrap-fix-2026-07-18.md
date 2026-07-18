# Metal frame bootstrap fix evidence (2026-07-18)

**Base HEAD:** `056e9851`  
**Branch:** `develop_metal`  
**Symptom:** ROM open left Metal presenter stuck on splash (no DS MetalTexture).

## Root cause

`IsMetalFullGpuFrameEligible()` required `SegmentedSnapshotReady()`, but snapshot
slots were only reserved from `DrawScanline(0)` / `DrawSprites(0)` after
`FrameActive=true`. After Soft/CpuBgra removal, failed Full-GPU frames publish
`None` and the splash drawable remained.

## Fix commits

| Commit | Summary |
|---|---|
| `7fa6bc1b` | Reserve A/B snapshot slots with renderer-owned epoch before eligibility |
| `84640007` | Required Init stages return false; session OpenGL fallback + OSD |
| `f481fdad` | Presenter black clear when emulation starts without MetalTexture |
| (this) | Static bootstrap audit + evidence |

## Static verification

```text
bash tools/ci/audits/run-metal-fullgpu-audits.sh
cmake --build build-mac-metal -j8
```

Real-ROM / TSan / multi-platform verification remains open (see指示書 §13–16).
