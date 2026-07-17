# PR-6 evidence — normal readback accounting + explicit reasons

**Date:** 2026-07-17  
**HEAD base:** `9b256370` (PR-5)  
**Instruction:** §12 (scaffold)

## Implemented

- `GPU_MetalReadback.h`: `MetalReadbackReason` + normal/explicit byte counters
- `SyncVRAMCapture` → `CpuVramRead` explicit bytes
- Soft `GetLine` readback → counted as `normal` + `SoftCompositorGetLine`
- Periodic stderr summary every 600 frames (`MetalReadbackStatsNoteFrame`)

## Not yet deleted (requires PR-7 Full-GPU-only)

- `ReadbackNativeColorTargetToLineBuffer` itself
- `SoftRenderer::VBlank` / Soft compositor path
- `UploadCpuCompletedCaptures` / `ComposeMetalVisibleOutput` CPU upload

## Validation

| Check | Result |
|---|---|
| macOS Intel Metal ON rebuild | PASS |
| 6000-frame normalReadbackBytes=0 | **not run** |

## Gate decision

PR-6 **accounting scaffold accepted**. Driving normal bytes to 0 is gated on Full-GPU coverage (PR-7 eligibility expansion).
