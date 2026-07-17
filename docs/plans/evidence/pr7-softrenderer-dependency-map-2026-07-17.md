# SoftRenderer inheritance dependency map (PR-7 prep)

Status: **blocked** until M4/M5 runtime acceptance (§13.1).  
Code cutover for capture Full-GPU (PR-5) and readback accounting (PR-6) exists, but ROM/diff/strict counters are not yet green.

## Current

```cpp
class MetalRenderer : public SoftRenderer
```

## SoftRenderer:: call sites in Metal path (must become 0)

| Site | File | Role today |
|---|---|---|
| `SoftRenderer::DrawScanline` | `GPU_MetalFullGpuMethods.inc` | Non-FullGpu frames + Soft compositor |
| `SoftRenderer::DrawSprites` | same | Non-FullGpu frames |
| `SoftRenderer::VBlank` / `VBlankEnd` | `GPU_Metal.mm` | Soft fallback VBlank |
| `SoftRenderer::GetOutput` | `GPU_Metal.mm` | CpuBgra fallback / GetOutput |
| `Output3D` / `Output2D` friend access | `GPU_MetalCaptureExperiment.inc` | Experiment source A |
| 3D host `SoftRenderer&` | `GPU3D_Metal*.h/mm` | RasterReference / Delegate |

## Replacement targets (§13.4)

| Soft duty | Metal replacement |
|---|---|
| scanline/sprite state | MetalRenderer2D segment snapshots |
| final FB | final texture array + lease |
| 3D line | native/compute texture (no GetLine) |
| capture | segment scheduler + R16Uint |
| brightness / swap | MetalFullGpuState arrays |
| output | RendererOutputLease MetalTexture |

## Exit criteria before inheritance flip

1. `IsMetalFullGpuFrameEligible` covers gameplay frames without Soft fallback (except explicit unsupported modes)
2. `normalReadbackBytes == 0` over 6000 frames
3. capture Full-GPU strict counters green
4. `MetalRendererHost` interface replaces SoftRenderer& in 3D constructors
5. Software/OpenGL builds still PASS

Do not flip `public SoftRenderer` until the above are evidenced.
