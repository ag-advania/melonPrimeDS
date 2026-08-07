# Vulkan display-capture source timing

Display capture can use the 3D output as source A. The Vulkan backend renders 3D
into `ColorImage` on the GPU, so software 2D needs a CPU-side copy of that image
to composite the capture. This document fixes *when* that copy is produced.

## The DS timing

```
VCount 215   GPU.cpp   Rend->Start3DRendering()      -> RenderFrame()
     ...
VCount   0   GPU.cpp   if (CaptureCnt & (1<<31)) { CaptureEnable = true; CheckCaptureStart(); }
VCount 0-191           software 2D runs the capture, calling GetLine()
VCount 192             CheckCaptureEnd()
```

The 3D frame is rendered **before** display capture is armed. A game may write
`DISPCAPCNT` any time after VCount 215 and the arm at the next VCount 0 is still
valid.

## The rule

> Whether this frame's 3D is needed as a capture source is not knowable while
> rendering it. Produce the export when capture asks, not when rendering.

`ensureExactCaptureExportForCurrentFrame()` implements this. It is called from
both consumers — `GetLineActiveBackend()` and
`PrepareCaptureFrameActiveBackend()` — at the point they would otherwise report
"no 3D coverage", and it blits the current `ColorImage` through the existing
`submitGraphicsCaptureExportForCurrentFrame()` path.

This matches the reference renderers:

| Renderer | When the capture source is obtained |
| --- | --- |
| `SoftRenderer3D` | `GetLine()` returns the scanline it just rendered |
| `GLRenderer` | `DoCapture()` picks `OutputTex3D` when the capture runs |
| `DX12Renderer3D` | `GetLine()` calls `EnsureFrameReadback()` on demand |
| `VulkanRenderer3D` | `GetLine()` / `PrepareCaptureFrame()` call `ensureExactCaptureExportForCurrentFrame()` |

## What went wrong

`RenderFrameActiveBackend()` derived `captureNeedsGpuCaptureLineBase` from
`gpu.CaptureCnt` bit 31 at VCount 215 and, when it was clear, produced no export
and reset the capture line state. `PrepareCaptureFrameActiveBackend()` then
refused to make one afterwards:

```cpp
// graphics_hw must not submit a second late capture export ...
if (exactCaptureOnly)
{
    clearLineCache();
    return;
}
```

So for any frame where the game armed `DISPCAPCNT` after VCount 215:

```
3D renders correctly
  -> VCount 215 predicts "capture will not need 3D"
  -> DISPCAPCNT armed
  -> VCount 0: CaptureEnable = true, software 2D runs the capture
  -> Vulkan has no 3D source and refuses to make one
  -> the capture writes a 3D-less image into VRAM
  -> a later frame displays that VRAM as a background
```

The prediction is the defect. Reading `CaptureCnt` at VCount 215 answers a
question about VCount 0.

## Why a late export is safe here

The refusal was not arbitrary — exporting the *wrong* frame is worse than
exporting nothing, and an earlier bug did exactly that, leaking a previous
frame's 3D into the capture-backed screen. What makes the late export safe is
attribution, tracked by `ColorImageHasCurrentFrame3D`:

- cleared in `RenderFrameActiveBackend()` before a render overwrites `ColorImage`
- set once that render completes
- kept set on the identical-frame reuse path, where nothing is redrawn
- cleared wherever `ColorImageInitialized` is cleared

Between VCount 0 and 191, `ColorImage` still holds this frame's render, because
the next render does not start until VCount 215. The late export therefore reads
this frame's 3D or refuses. It never substitutes an older frame or the clear
colour.

`ExactCaptureLineCacheFresh` continues to gate reuse of the line cache, so a
cache latched for an earlier frame is still never exposed.

## Observability

The developer performance log reports `lateCapExport=<produced>/<failed>`. A
non-zero produced count means frames really are arming capture after VCount 215 —
which is the condition this exists for. A rising failure count means the export
was needed but could not be attributed to the current frame, and should be
investigated rather than ignored.

## Enforcement

`tools/ci/audits/audit-vulkan-capture-export-timing.py` checks the DS timing
premise in `GPU.cpp`, DX12's on-demand readback contract, the existence and
attribution guard of the late export, that neither consumer clears the line cache
without attempting it, that the flag is set on the render-completion path
specifically, and that every `ColorImageInitialized = false` clears the
attribution with it.
