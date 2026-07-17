# PR-5 evidence — capture Full-GPU production cutover

**Date:** 2026-07-17  
**HEAD base:** `87c79f76` (PR-4)  
**Instruction:** §11

## Implemented

- Removed `if (GPU.CaptureCnt & (1u << 31)) return false;` from Full-GPU eligibility
- Marker `MELONPRIME_METAL_CAPTURE_FULLGPU_CUTOVER_V1`
- Segment scheduler always `allowCaptureTextures = true` (2D before per-segment encode)
- Static audit: `audit-metal-capture-fullgpu-cutover.py`
- Updated PR-2/PR-3/PR-4 audits to require exclusion removal

## Still open (§11.5 / §11.6)

- 6000-frame capture Full-GPU counters
- CPU reference diff 0 / real ROM / homebrew / 1–16 scale
- Raster vs Compute parity under capture

## Validation

| Check | Result |
|---|---|
| fullgpu cutover audit | PASS |
| segment / native / experiment audits | PASS |
| macOS Intel Metal ON rebuild | PASS |
| ROM capture play | **not run** |

## Gate decision

PR-5 **code cutover accepted** on compile+static evidence. Runtime M4 acceptance remains open.
