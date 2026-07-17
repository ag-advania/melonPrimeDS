# PR-4 evidence — per-segment capture scheduler

**Date:** 2026-07-17  
**HEAD base:** `f75ba9f4` (PR-3)  
**Instruction:** §10 (minimal viable)

## Implemented

- `GPU_MetalSegmentScheduler.inc`: merge capture-config + 2D boundaries → segments
- VBlank Full-GPU path: per segment { render A, render B, encode capture }
- `EncodeMetalDisplayCapture(startLine,endLine)` + kernel `lineOffset`
- `RenderSegmentedGpuFrame(... startLine, endLine)` with one-time Output/OBJ clear
- Selective Capture→Snapshot blit retained per segment (§10.6 B)
- Static audit: `audit-metal-capture-segment-scheduler.py`

## Explicitly deferred

- True per-scanline dispatch / CaptureWriteTicket generation counters
- Ping-pong texture pairs (§10.6 A)
- CaptureCnt Full-GPU exclusion removal (PR-5)
- Experimental same-frame feedback diff 0 / ROM validation

## Validation

| Check | Result |
|---|---|
| segment scheduler audit | PASS |
| native capture storage audit | PASS |
| macOS Intel Metal ON rebuild | PASS |
| multi-segment same-frame ROM | **not run** (exclusion still blocks capture Full-GPU) |

## Gate decision

PR-4 **accepted as segment-loop scaffold**. Production behavior unchanged while CaptureCnt exclusion remains; PR-5 can cut over on this ordering.
