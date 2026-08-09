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
those planes with the high-resolution 3D target into one of three GPU-resident
BGRA buffer slots at `256*scale x 192*scale`. A leased `DX12PresentedFrame`
carries that resource to `ScreenPanelDX12`; the native presenter copies each
screen into a sampled texture and performs layout, filtering, HUD, radar and OSD
composition directly into a flip-model DXGI swapchain.

The compute composition is submitted from `DX12Renderer::VBlank()` on the
emulation thread. This binds the structured 2D planes and `FinalFB` to the same
DS frame even when software flips `ScreenSwap` every frame. `GetOutput()` only
publishes the already-composed slot once one exists; presentation timing
cannot combine one frame's screen mapping with another frame's 3D image. During
incremental pipeline compilation at ROM startup, it temporarily publishes the
initialized software buffers so Qt never paints an uninitialized cached image.

Each composition slot owns its own command allocator, descriptors and staging
buffer. `ComposeStructuredOutput()` opens a slot only when both its GPU fence and
presenter lease are free; otherwise it keeps the preceding completed output and
returns without blocking VBlank. Renderer and presenter use the same process-wide
device and queue, so queue ordering plus explicit UAV/copy transitions replace a
CPU fence wait.

A separate 256x192 resolve remains available for DS display capture and other
core operations that require the `Renderer3D::GetLine()` contract. Its deferred
readback is deliberately independent from presentation. CPU-authored Custom HUD
and OSD bitmaps use small layer uploads; game screens, screen layout, rotation,
filtering and DPI scaling stay on the GPU. Qt never queues or paints active
emulation frames; the native child HWND is owned by the DXGI swapchain.

When `GPU3D.RenderFrameIdentical` is set and the texture/clear state is unchanged,
DX12 reuses the preceding valid 3D target. Software 2D and the structured
compositor still run for the current DS frame, so this skips only redundant 3D
raster work and never replays an earlier two-screen composition.

Texture uploads normally use the 32 MiB linear ring. If it fills, the cache now
records the copy from a dedicated retained upload resource and releases that
resource after the submission retires; it no longer submits and synchronously
waits in the middle of `BuildPolygons()`. The five frame-static SRVs are built
once per scale-dependent resource generation and copied into each transient
table; only the decoded texture SRV remains dynamic per texture switch.

Consequences:

* Internal resolution applies to the composed screen output, not only the 3D
  raster target.
* Normal presentation performs no high-resolution GPU readback, CPU full-frame
  composition or window-sized re-upload. The native resolve is read back only
  when display capture calls `GetLine()`.
* DX12 selection owns presentation regardless of `Screen.UseGL`; no OpenGL
  context or Qt frame mailbox is involved.
* The swapchain uses `DXGI_SWAP_EFFECT_FLIP_DISCARD`, two buffers, maximum frame
  latency 1 and a frame-latency waitable object. VSync-off uses tearing when the
  platform reports `DXGI_FEATURE_PRESENT_ALLOW_TEARING`.

### Presentation while paused

`ScreenPanelDX12::drawScreen()` normally returns as soon as emulation is not
running: nothing new is emulated, and the swapchain keeps showing the last
presented image even while a modal dialog is exposed, because the native child
HWND is not Qt's to erase.

The Custom HUD on-screen editor is the one paused state that must keep
composing. The settings dialog pauses emulation *before* handing the panel to
the editor, so `EmuThread`'s paused loop (`drawScreen()` every 75 ms) is the
only thing that can put the editor overlay on screen. `setHudEditModeActive()`
therefore latches `hudEditLivePresentation`, which lets the paused draw pass
through; without it the panel just re-presents the frozen pre-pause frame and
the editor is invisible. `ScreenPanelVulkan` carries the same flag for the same
reason (it additionally drops its modal-pause freeze overlay, which DX12 does
not have). The software and OpenGL panels gate on `emuIsActive()` instead and
keep drawing on their own.

## NVIDIA Reflex Low Latency

On a supported NVIDIA GPU, the video settings dialog exposes the standard
three Reflex modes while DirectX 12 or Vulkan is selected:

* **Off** — low-latency mode and boost are disabled;
* **On** — NVAPI low-latency mode is enabled; and
* **On + Boost** — low-latency mode is enabled and the driver is asked to keep
  the GPU at maximum clocks, which can reduce CPU-bound latency at the cost of
  additional power use.

`DX12NvidiaReflex` loads `nvapi64.dll` at runtime and probes the active D3D12
device with `NvAPI_D3D_GetSleepStatus`. Non-NVIDIA devices, unsupported drivers,
or missing NVAPI entry points leave DX12 available and disable only the Reflex
control with a diagnostic tooltip. No NVIDIA library is linked or shipped.

The Vulkan path uses the native `VK_NV_low_latency2` device extension. It
requires `VK_KHR_present_id` and timeline semaphore support, enables latency
mode on each native Vulkan swapchain, associates the final graphics submission
and `vkQueuePresentKHR` with the same monotonically increasing Present ID, and
publishes the corresponding latency markers. Unsupported GPUs or drivers leave
Vulkan available and disable only the Reflex control.

The emulation thread calls the backend sleep operation once at the beginning of
every Reflex-capable frame, immediately before late input polling. DX12 uses
`NvAPI_D3D_Sleep`; Vulkan uses `vkLatencySleepNV` followed by a host wait on the
extension's timeline semaphore. Both paths publish Input Sample, Simulation,
Render Submit and Present latency markers. Sleep and markers remain active in
Off mode as NVIDIA recommends; the mode flags alone control whether low latency
and boost are enabled. The minimum interval is always zero, so Reflex does not
add a frame-rate cap.

Render Submit markers bracket the D3D12 render/composition work. Present Start
and Present End are emitted immediately around the real
`IDXGISwapChain::Present` call on the same D3D12 device and queue used by the
renderer. GPU layout/HUD/OSD composition is submitted before Present Start, so
NVIDIA receives an accurate native presentation boundary.

Vulkan presents through `MelonPrimeVulkanSurfacePresenter`, so its Present
markers and Present ID cover the real native swapchain submission and present.
Reflex state belongs to each presenter/emulator instance; no process-global
frame counter or cross-instance pacing state is introduced. The persisted
configuration path remains `3D.DX12.NvidiaReflexMode` for compatibility, but
the value is shared by both native backends.

## AMD Radeon Anti-Lag 2

On a supported AMD Radeon GPU, the same video-settings area exposes an
independent **AMD Radeon Anti-Lag 2** Off/On option for DirectX 12 and Vulkan.
It is enabled by default when the driver reports support. Unsupported hardware
or drivers leave the selected renderer available and disable only this option
with a diagnostic tooltip.

The DirectX 12 path uses the public AMD Anti-Lag 2 SDK driver ABI without
shipping or linking an AMD library. `DX12AmdAntiLag2` obtains the interface from
the active D3D12 device through `AmdExtD3DCreateInterface`, keeps it scoped to
the renderer, and performs AMD's per-frame null update immediately before late
input polling. State updates are sent only when On/Off changes, and `maxFPS` is
always zero so Anti-Lag 2 never adds a frame-rate cap.

The Vulkan path enables the native `VK_AMD_anti_lag` device extension and its
`antiLag` feature. Every emulated frame receives one monotonically increasing
frame ID: an INPUT update is issued immediately before late input polling and a
matching PRESENT update immediately before `vkQueuePresentKHR`. As on DX12,
`maxFPS` remains zero. Feature state and frame IDs belong to each emulator
instance; no process-global latency state is introduced.

The persisted configuration path is `3D.AMD.AntiLag2Enabled`. This option does
not emulate Anti-Lag 2 on NVIDIA or Intel hardware and does not alter the NVIDIA
Reflex setting.

## Files

| File | Role |
| --- | --- |
| `src/DX12Common.h` | ComPtr, runtime loading of `d3d12.dll` / `dxgi.dll` / `d3dcompiler_47.dll` |
| `src/DX12Context.{h,cpp}` | Device, adapter selection, queue, fence, descriptor ring, upload ring, HLSL compile, init logging |
| `src/DX12NvidiaReflex.{h,cpp}` | Runtime NVAPI loading, support probe, low-latency/boost modes, sleep and latency markers |
| `src/DX12AmdAntiLag2.{h,cpp}` | Runtime AMD Anti-Lag 2 driver-ABI loading, support probe and per-frame input insertion point |
| `src/GPU3D_TexcacheDX12.{h,cpp}` | Texture-array heap behind the shared `Texcache<>` template |
| `src/GPU3D_DX12.{h,cpp}` | The renderer: span setup, dispatch orchestration, GPU presentation ring and capture readback |
| `src/DX12PresentedFrame.h` | Opaque GPU-resource handoff descriptor shared by renderer and presenter |
| `src/GPU3D_DX12_shaders.h` | HLSL sources, compiled at runtime |
| `src/GPU_DX12.{h,cpp}` | `DX12Renderer`, pairing the 3D renderer with software 2D |
| `src/frontend/qt_sdl/MelonPrimeDX12FeatureCheck.{h,cpp}` | Runtime availability probe for the settings dialog and renderer normalization |
| `src/frontend/qt_sdl/MelonPrimeDX12SurfacePresenter.{h,cpp}` | Native child HWND, GPU layer composition, CPU overlay uploads, flip-model DXGI swapchain and actual Present boundary |

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
12. publish the GPU-resident two-screen buffer through a leased ring slot

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

The high-resolution screen images remain device-local. The presenter copies
them, queue-ordered, from the renderer's leased buffer to sampled textures and
draws the existing layout as transformed quads. QImage remains only for
CPU-authored premultiplied HUD and OSD layers. The colour-keyed radar is drawn
after the HUD backing SVG/outline, preserving its foremost layer order without
making the bottom screen CPU-visible.

The DX12 HUD upload accepts a source subrectangle of the retained full HUD
image. It copies those rows directly into the D3D12 upload resource, avoiding
the former intermediate `hudPatch` QImage allocation/copy while keeping the
same dirty rectangle, texture dimensions, UVs and draw order.

## Performance telemetry

Set `MELONPRIME_PERF=1` to emit one-second `[DX12Perf]` windows. CPU timers cover
renderer and presenter fence waits, texture-cache update, polygon/span setup,
span staging copy, descriptor work, structured compositor packing/recording,
conditional capture wait/map, HUD upload, command submission and presentation
recording. Counters include frame/identical-frame counts, geometry sizes,
structured/span/texture/HUD bytes, descriptor writes, compositor drops,
capture reads, screen-copy bytes, upload overflows/spills and HUD texture
recreation. The gate is runtime-only and disabled by default.

The 4x Rev 1 F7 audit scene on the RTX 5070 Ti measured zero capture reads,
zero texture-ring overflows, zero compositor drops and zero steady-state HUD
texture recreations. That evidence keeps the conditional capture redesign and
grow-only HUD allocation out of the hot-path change. Directly generating spans
into mapped upload memory was also tested and rejected: median
`build_polygons` increased from about 150 us to about 1.09 ms, while the
existing contiguous staging copy costs about 19 us. The CPU scratch plus bulk
copy is therefore intentional on this path.

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
  synchronization boundaries used by the OpenGL capture path; and
* NVIDIA Reflex support probing plus successful Off/On/On+Boost mode calls,
  sleep and all latency-marker calls on the RTX 5070 Ti. The On request was
  accepted as `lowLatency=1, boost=0`, and On + Boost as
  `lowLatency=1, boost=1`, both with `minimumIntervalUs=0`.
* native `DXGI_SWAP_EFFECT_FLIP_DISCARD` swapchain creation and live ROM
  presentation on the RTX 5070 Ti, both while the runtime renderer is Software
  outside a match and while DX12 is forced for the complete boot sequence.
* the GPU-native handoff at 4x after loading the F7 savestate, including a
  2560x1344 presentation, warning-free runtime presenter-shader compilation,
  and a 20-switch DX12/Vulkan/Software stress cycle with every transition and
  subsequent DX12 `Present` completing without a runtime fallback or D3D12
  debug-layer error; and
* one same-savestate, same-window, 4x `MELONPRIME_PERF=1` comparison against the
  preceding readback/upload binary. The median of the 1 Hz `Draw` section
  averages fell from 1.727 ms to 0.254 ms. The run was frame-rate limited, and
  its whole-frame percentiles were noisy, so this is evidence for the removed
  CPU presentation work rather than a broader FPS claim.

This is Windows/NVIDIA evidence, not a claim about untested AMD or Intel driver
families. The offline shader audit and runtime feature probe remain the gates
for those systems.
