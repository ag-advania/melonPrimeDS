# DirectX 12 backend

Windows-only 3D renderer, added on `develop_dx12`. It is a port of melonDS's
**OpenGL compute renderer** (`src/GPU3D_Compute.cpp` + `GPU3D_Compute_shaders.h`)
— the GPU version of the software rasterizer — not of the fixed-function OpenGL
renderer.

Everything is behind a hard build gate. With `MELONPRIME_ENABLE_DX12` off, or on
any non-Windows target, no DX12 source, compile definition or library reaches
either build target, and the Software / OpenGL / Vulkan / Metal paths are
untouched.

## Architecture

```
DX12Renderer (: SoftRenderer)        software 2D engines + structured planes
 └── DX12Renderer3D (: Renderer3D)   3D rasterizer + high-resolution compositor
```

The software 2D engines record the same per-pixel structured planes used by the
Vulkan backend. After the DS scanlines are complete, a DX12 compute pass combines
those planes with the high-resolution 3D target and reads back two BGRA screens
at `256*scale x 192*scale`. `RendererOutput` carries those dimensions, and both
the `NativeQt` and `OpenGL` panels accept the variable-size buffers.

The compute composition is submitted from `DX12Renderer::VBlank()` on the
emulation thread. This binds the structured 2D planes and `FinalFB` to the same
DS frame even when software flips `ScreenSwap` every frame. `GetOutput()` only
publishes the already-composed front buffer; presentation timing cannot combine
one frame's screen mapping with another frame's 3D image.

A separate 256x192 resolve remains available for DS display capture and other
core operations that require the `Renderer3D::GetLine()` contract. Presentation
therefore gains real internal-resolution output without duplicating the Custom
HUD, OSD, window-layout, input, or VSync implementations in another screen
panel.

Consequences:

* Internal resolution applies to the composed screen output, not only the 3D
  raster target.
* The presentation path has one high-resolution GPU readback per newly composed
  frame. The native resolve is read back only when display capture calls
  `GetLine()`.
* `Screen.UseGL` still applies: DX12 needs no GL context, so presentation
  resolves to `NativeQt` or `OpenGL` exactly like the Software renderer.

## Files

| File | Role |
| --- | --- |
| `src/DX12Common.h` | ComPtr, runtime loading of `d3d12.dll` / `dxgi.dll` / `d3dcompiler_47.dll` |
| `src/DX12Context.{h,cpp}` | Device, adapter selection, queue, fence, descriptor ring, upload ring, HLSL compile, init logging |
| `src/GPU3D_TexcacheDX12.{h,cpp}` | Texture-array heap behind the shared `Texcache<>` template |
| `src/GPU3D_DX12.{h,cpp}` | The renderer: span setup, dispatch orchestration, readback |
| `src/GPU3D_DX12_shaders.h` | HLSL sources, compiled at runtime |
| `src/GPU_DX12.{h,cpp}` | `DX12Renderer`, pairing the 3D renderer with software 2D |
| `src/frontend/qt_sdl/MelonPrimeDX12FeatureCheck.{h,cpp}` | Runtime availability probe for the settings dialog and renderer normalization |

## Pipeline

Per frame, in one command list:

1. `ClearCoarseBinMask` — reset the coarse tile bitmasks
2. `ClearIndirectWorkCount` — reset the per-variant work counters
3. `InterpSpans` (Z/W) — per-scanline X span interpolation
4. `BinCombined` — coarse + fine binning in one pass
5. `CalcOffsets` — per-variant offsets and split dispatch arguments
6. `SortWork` — sort the work list by variant (indirect dispatch)
7. `Rasterise` — one indirect dispatch per variant, 16 pipeline variants
8. `DepthBlend` (Z/W) — clear plane, depth test, translucency, shadows
9. `FinalPass` — edge marking / fog / anti-aliasing resolve, 8 variants
10. `Resolve` — preserve a 256x192 `Output3D` source for display capture
11. after software 2D scanlines complete, `Compositor` combines the structured
    planes with the high-resolution `FinalFB`
12. copy the two high-resolution BGRA screens to the presentation readback

35 compute pipelines in total. They are compiled incrementally through
`ShaderCompileStep()`, so the OSD shows progress instead of the emulator
hitching, and they are rebuilt whenever the internal resolution changes (tile
geometry is baked in as `#define`s, exactly like the OpenGL renderer).

Pipeline creation, scale-dependent allocation, command submission, descriptor
binding and readback failures are fatal to the DX12 renderer instance. The
emulation thread reports the stored reason, invalidates its published output and
switches to the Software renderer instead of continuing with a partial or stale
DX12 frame.

## Differences from the OpenGL compute renderer

These are the only intentional behavioral deviations. Everything else — the
fixed-point math, binning, span setup, blending and the tile geometry
derivation — is a 1:1 port.

* **Display capture as a texture is not special-cased.** The OpenGL renderer
  samples the GPU-side capture output through `Capture128Texture` /
  `Capture256Texture`. Here the 2D engines are the software ones, so captures
  land in real VRAM and the ordinary texcache lookup already returns them.
* **Texture wrapping is done in the shader.** HLSL cannot `Sample()` a UINT
  texture, so `GL_REPEAT` / `GL_MIRRORED_REPEAT` / `GL_CLAMP_TO_EDGE` are
  reproduced with integer math and `Load()`. Exact, because every cached
  texture is power-of-two sized.
* **Indirect arguments live in their own buffer.** A D3D12 resource cannot be
  in `UNORDERED_ACCESS` and `INDIRECT_ARGUMENT` at once, and the rasterize
  dispatches still read the bin-result buffer as a UAV, so the header is copied
  into a dedicated `IndirectArgsBuffer` after `CalcOffsets`.
* **`umulExtended` / `findMSB` / `findLSB` are hand-written.** `umul` is not
  exposed by the legacy HLSL compiler, and `firstbithigh` / `firstbitlow`
  disagree between shader targets about which end the index is counted from —
  the fixed-point division is exquisitely sensitive to that.
* **Tile memory falls back instead of failing.** The OpenGL heuristic
  (`tiles * 16` work tiles) can ask for more than a GPU will hand out at high
  internal resolutions, so the allocation is halved until it fits. The binning
  shader already trims work to `MaxWorkTiles` and drops excess layers
  gracefully.

## Build and validation

The renderer compiles its HLSL at runtime with `d3dcompiler_47.dll`, so the
MinGW build needs no shader toolchain and no DX12 import libraries — every entry
point is resolved with `GetProcAddress`, and only `dxguid` is linked.

Because a shader error would only show up as a black screen on a machine with a
D3D12 GPU, the shader set has an offline audit:

```bash
python tools/ci/audits/check-dx12-shaders.py
```

It assembles exactly the sources `DX12Renderer3D::BuildPipeline()` builds — same
`#define` prologue, same per-variant defines — and runs `fxc.exe` over all 35
variants at several internal resolutions. A warning is a failure as well as a
compile error: warning-free data flow is required because the same source is
optimized again by the runtime compiler. The audit skips cleanly when the
Windows SDK is not installed.

## Internal resolution

The `3D.GL.ScaleFactor` setting applies to both the 3D scene and the final
composed screens. `SetRenderSettings()` logs the active dimensions, e.g.

```
DX12: internal resolution 4x -> 3D/composed output 1024x768, tiles 128x96 (8px), capture resolve 256x192
```

The structured planes keep 2D layer ordering, alpha coefficients, brightness,
display modes, and the position of the 3D slot at native DS granularity. The
compositor samples those controls at native coordinates while sampling 3D from
`FinalFB` at full resolution. This preserves pixel-authentic 2D behavior while
allowing polygon edges and textures to retain the selected internal resolution.

The final images are CPU-visible because they are shared with the existing
presentation panels. This avoids a DX12-specific swapchain and a third HUD/OSD
implementation, at the cost of readback bandwidth that grows with the square of
the scale factor.

## Verified scope

The Windows Release build and all 175 shader combinations (35 pipelines at
1x, 4x, 5x, 9x and 16x) pass on the repository build path. Runtime validation
on an NVIDIA GeForce RTX 5070 Ti with the D3D12 debug layer enabled has covered:

* Metroid Prime Hunters (USA) boot, title/menu capture sequences and attract
  mode at the default 4x internal resolution;
* actual target/resource creation and first presentation at 1x, 5x, 9x and
  the maximum 16x internal resolution;
* held capture/menu frames and 100-frame sequences without upper/lower screen
  alternation, stale-frame replay, black capture rows or a DX12 fallback;
* the F1 main-menu savestate while MPH alternates `ScreenSwap` every frame,
  compared against Software and OpenGL Compute output at 1x, 4x and 16x; and
* comparison with the Software renderer's physical VRAM capture output. The
  retained structured capture metadata is invalidated at the same CPU/DMA VRAM
  synchronization boundaries used by the OpenGL capture path.

This is Windows/NVIDIA evidence, not a claim about untested AMD or Intel driver
families. The offline shader audit and runtime feature probe remain the gates
for those systems.
