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
DX12Renderer (: SoftRenderer)        software 2D engines + framebuffers
 └── DX12Renderer3D (: Renderer3D)   tile-binned compute rasterizer on the GPU
```

Unlike the Vulkan backend, there is **no DX12 2D compositor and no DX12 screen
panel**. The 3D scene is rasterized at the configured internal resolution,
downscaled on the GPU to the DS's native 256x192 in the exact word format
`Renderer3D::GetLine()` must return, read back, and handed to the existing
software 2D compositor.

That choice is what keeps display capture, savestates, the Custom HUD, the OSD
and both Qt presentation backends (`NativeQt` and `OpenGL`) working with no
DX12-specific code, and it is only possible because the compute renderer's
internal color encoding is already identical to the software compositor's
`Output3D`: `r6 | g6<<8 | b6<<16 | a5<<24`.

Consequences:

* Internal resolution behaves as **supersampling**: the resolve pass box-filters
  each `ScaleFactor x ScaleFactor` block with alpha weighting, so the DS output
  stays 256x192 but geometry edges are anti-aliased.
* There is one CPU/GPU sync per frame. It is deferred to the first
  `GetLine()` call rather than the end of `RenderFrame()`, so the GPU overlaps
  with whatever the emulation thread does in between.
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
10. `Resolve` — downscale to 256x192 in the software `Output3D` encoding
11. copy to a readback buffer

34 compute pipelines in total. They are compiled incrementally through
`ShaderCompileStep()`, so the OSD shows progress instead of the emulator
hitching, and they are rebuilt whenever the internal resolution changes (tile
geometry is baked in as `#define`s, exactly like the OpenGL renderer).

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
`#define` prologue, same per-variant defines — and runs `fxc.exe` over all 34
variants at several internal resolutions. It skips cleanly when the Windows SDK
is not installed.

## Not yet verified

The renderer builds cleanly and every shader variant compiles, but the DX12
output has **not** been observed running: no ROM has been rendered through this
path, and no comparison against the OpenGL/Software renderers has been made.
Treat visual correctness, performance and stability as untested.
