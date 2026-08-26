# DirectX 12 backend

Windows-only 3D renderer, originally added on `develop_dx12` and currently maintained on
`develop_hud` at `94f08caf0` (2026-08-27). It is a port of melonDS's
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
 └── DX12Renderer3D (: Renderer3D)   3D rasterizer + composition orchestration
     └── DX12Gpu2DComposer            structured/native 2D composition + output ring
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

### Presenting software-renderer frames

`3D.ForceSoftwareOutsideMatch` ("Use software renderer outside matches") swaps
the *3D renderer* to `SoftRenderer` between the pre-match and post-match
blackouts, but leaves the *presentation backend* alone: `3D.Renderer` still says
DX12, so `ResolvePresentationBackend()` still returns DX12 and this panel keeps
owning the swapchain. `EmuThread::run()`'s `applyPendingVideoSettings` block
deliberately forces only `videoRenderer`, never `videoBackend`.

The panel therefore has to present two shapes of frame, and both arrive as
`RendererOutputKind::CpuBgra`. The **live renderer** is what tells them apart:

* `dynamic_cast<DX12Renderer*>` is null — the software renderer genuinely owns
  output. It flattens its own 3D layer (`UseStructuredVulkan2D()` is false
  without a structured-metadata 3D renderer), so the frame is a complete
  picture and goes on screen through `UploadLayer()`, the presenter's CPU path.
* `dynamic_cast<DX12Renderer*>` is non-null — a native renderer is live but its
  pipeline is not ready, so its output is a Software-2D plus placeholder-3D
  hybrid. That one must never become visible; the panel keeps the last native
  frame, or the Qt splash if there is none yet.

A CPU frame carries no renderer frame identity, so the panel publishes serial 0
(`SetPresentedFrameIdentity`), which skips the monotonic gate for that frame and
leaves the last accepted GPU epoch/serial untouched — the native renderer that
resumes at match start is still compared against its own predecessor.
`NativeVisibilityState::AcceptWithoutIdentity()` marks the surface visible on
the same terms. `ScreenPanelVulkan::drawScreenFrame()` implements the identical
rule against `VulkanRenderer`.

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
Render Submit and Present latency markers. Both paths keep Sleep, marker and
frame-correlation calls active in Off mode as NVIDIA requires; the mode flags
only control low-latency pacing and boost. The minimum interval is always zero,
so Reflex does not add a frame-rate cap.

The two backends use the same engine boundaries: Sleep completes first,
Input Sample is emitted immediately before `inputRefreshJoystickState()` (the
first late input read), and Simulation Start is emitted only after
`RunFrameHook()` and `SetKeyMask()` have captured that input for the emulated
frame. The Reflex helpers reject duplicate or out-of-order Input Sample and
Simulation markers, so a future call-site move cannot silently create an
invalid marker timeline.

Render Submit markers bracket the D3D12 render/composition work. Present Start
and Present End are emitted immediately around the real
`IDXGISwapChain::Present` call on the same D3D12 device and queue used by the
renderer. GPU layout/HUD/OSD composition is submitted before Present Start, so
NVIDIA receives an accurate native presentation boundary.

`DX12NvidiaReflex::QueryTimings()` reads the driver's own latency reports back
through `NvAPI_D3D_GetLatency` (interface id `0x1A587F9C`, resolved at runtime
and optional). This is the DX12 counterpart of `vkGetLatencyTimingsNV`, and the
only observable proof that the markers and the Sleep call were correlated by the
driver into one frame: every marker entry point returns void, so without it an
inert integration is indistinguishable from a working one. Developer builds log
the newest report every 600 frames. `DX12NvidiaReflexLatencyReportStatus`
distinguishes a missing entry point from a rejected query and from a
successful-but-empty ring, because only the last of those says anything about
the driver.

Measured on 2026-08-24, DX12 Reflex is engaged and correct: the driver returns
frame reports whose `frameID` matches the logical frame ids the markers carried.
It costs nothing because `gpuActiveRenderTimeUs` is ~34 us against a
`gpuFrameTimeUs` of ~1804 us -- the GPU is busy about 2% of the frame, no render
queue accumulates, and the correct sleep is zero. `NvAPI_D3D_Sleep` measures
p50 1.2 us (`reflex_sleep_us`), against p50 1247 us for `vkLatencySleepNV` under
the same workload. Details:
`docs/audit/dx12_reflex_latency_verification_2026-08-24.md`.

Vulkan presents through `MelonPrimeVulkanSurfacePresenter`, so its Present
markers and Present ID cover the real native swapchain submission and present.
Reflex state belongs to each presenter/emulator instance; no process-global
frame counter or cross-instance pacing state is introduced. The persisted
configuration path remains `3D.DX12.NvidiaReflexMode` for compatibility, but
the value is shared by both native backends.

## Intel Xe Low Latency (XeLL)

On a supported Intel Arc GPU, selecting DirectX 12 exposes an independent
**Intel Xe Low Latency (XeLL)** Off/On option. It is disabled by default and
must be explicitly enabled by the user. The
control is disabled with the exact probe failure reason on non-Intel adapters,
unsupported Intel drivers, or when the XeLL runtime is missing. Cross-vendor
XeLL is deliberately not offered: Intel requires active XeSS Frame Generation
for that mode, and MelonPrimeDS does not implement XeSS-FG.

`DX12IntelXeLL` dynamically loads the unmodified `libxell.dll` shipped beside
the executable and creates one context for the active D3D12 device. The ABI and
runtime are pinned to Intel XeSS SDK 3.0.2 / XeLL 1.3.2.10 at commit
`8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0`. CMake verifies the runtime SHA-256
and copies the DLL plus Intel's license and third-party notices into the build
output. No MSVC import library is linked, so the integration works in the
project's supported MinGW build.

Each emulated frame calls `xellSleep` before late input polling, then publishes
Simulation Start and Input Sample. Render Submit markers follow the D3D12 render
and high-resolution composition boundary; Present markers directly bracket the
flip-model `IDXGISwapChain::Present` call. All calls for a frame share one
monotonically increasing per-renderer frame ID. Calls remain active in Off mode,
as Intel recommends, while `xellSetSleepMode` controls latency reduction.

Compatibility mode keeps `minimumIntervalUs` at zero. MelonPrime's existing
limiter runs before `xellSleep`, matching Intel's required order without adding
a second limiter. Developer builds additionally expose four hardware-validation
experiments: bypass only the DXGI frame-latency wait, bypass only the host
limiter, transfer the host frame cap to XeLL, or Intel-recommended mode (XeLL
owns the cap and both generic waits are bypassed). These paths take effect only
after the runtime reports XeLL actually enabled. Compatibility remains value
zero and the release default; release builds ignore a stored experimental value.
The pure host-limiter bypass is restricted to normal speed; policies that give
XeLL the frame cap instead update `minimumIntervalUs` on fast-forward and
slow-motion transitions without calling `xellSetSleepMode` in steady state.
Before changing sleep mode or destroying the context, the renderer inserts and
waits for a queue-wide fence that also retires native-presenter work. The saved
configuration keys are `3D.Intel.XeLLEnabled` and the developer-only
`3D.Intel.XeLLPacingPolicy`.

The runtime wrapper exposes an injectable XeLL API table. The Windows DX12
build runs the production lifecycle against a fake backend on every build,
covering normal, 3D-work-free, skipped-present, present-failure, symbol/version,
sleep-mode, marker-failure, cleanup, monotonically increasing frame IDs and
pacing-policy cases without requiring Intel hardware. The low-latency CI audit
also verifies the DLL hash, PE exports, ABI assertions, license/notices and
default-Off configuration.

Logs distinguish requested state, runtime presence, vendor probe support,
context creation, applied sleep mode, actual enabled state, minimum interval,
frame ID, runtime version, adapter IDs and driver version. They deliberately
report `hardwareValidation=pending`: fake-backend and static tests prove the
integration contract, not Intel Arc runtime behavior or a latency improvement.
The Arc acceptance procedure is documented in
[`intel-arc-xell-validation.md`](../../development/testing/intel-arc-xell-validation.md).

Reflex and Anti-Lag 2 remain vendor-gated, so only XeLL can be active on the
supported Intel path. This also satisfies Intel's requirement not to combine
XeLL with another latency-reduction implementation.

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

The local ABI is pinned to AMD's public `ffx_antilag2_dx12.h` at commit
`390aa4a8c8655d0ae6e90079db2c85e103a96da3`. It was rechecked on 2026-08-11
against AMD's v2.0.4 publication: the interface GUID, 32-byte v1 state
structure, version/mode values, null per-frame update, initialization and
shutdown contracts are unchanged. The newer v2 structure concerns frame
generation signalling, which this emulator does not use.

The minimal DX12 Reflex ABI is likewise pinned to NVIDIA's public NVAPI commit
`cd6918f60b3c9a0476fdfe7e89bb32330602049d` (`nvapi.h`, 2026-06-01), including
the structure sizes, version values, marker enumeration and QueryInterface IDs.

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
| `src/DX12IntelXeLL.{h,cpp}` | Runtime XeLL loading, Intel support probe, sleep-mode state and complete frame-marker lifecycle |
| `src/DX12LowLatencyPacing.h` | Single DX12 pacing-authority resolver and developer XeLL comparison policies |
| `src/GPU3D_TexcacheDX12.{h,cpp}` | Texture-array heap behind the shared `Texcache<>` template |
| `src/GPU3D_DX12.{h,cpp}` | 3D rasterizer and dispatch orchestration; owns the raster resources and compute-pipeline use |
| `src/DX12Gpu2DComposer.{h,cpp}` | structured/native 2D composition, resolution-dependent compositor resources, and composed-output publication |
| `src/RendererOutputRing.{h,cpp}` | backend-neutral leased output-slot publication shared by the native compositors |
| `src/StructuredUploadPlan.h` | backend-neutral dirty-range planning for structured 2D uploads |
| `src/DX12PipelineRepository.{h,cpp}` | root-signature layout, compute PSO/library cache, command signature, and pipeline creation repository |
| `src/DX12CaptureBridge.{h,cpp}` | native display-capture sidecar/readback resources and the demand-driven copy bridge |
| `src/DX12PresentedFrame.h` | Opaque GPU-resource handoff descriptor shared by renderer and presenter |
| `src/GPU3D_DX12_shaders.h` | HLSL source strings and generation metadata used by the offline shader toolchain |
| `src/GPU3D_DX12_ShaderBlobs.inc` | generated, committed DXBC/DXIL table for the compute variants |
| `src/DX12ShaderCompiler.{h,cpp}` | runtime compiler used by the small native-presenter vertex/pixel shader pair |
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

39 compute pipeline variants are created in total: the 37 common 3D/presentation
steps plus `GPU2DNative` and `GPU2DNativeCapture`. Their pipeline objects are
created incrementally from the committed DXBC/DXIL through `ShaderCompileStep()`,
so the OSD shows progress instead of the emulator hitching, and they are rebuilt
whenever the internal resolution changes (tile geometry is baked in as
`#define`s, exactly like the OpenGL renderer).

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
* **Tile memory is bounded without dropping layers.** The OpenGL heuristic
  (`tiles * 16` work tiles) can ask for more than a GPU will hand out at high
  internal resolutions, so the allocation is halved until it fits. The host
  partitions the ordered polygon stream into conservative consecutive batches
  that each fit the resulting `MaxWorkTiles`; result and shadow-continuation
  buffers carry exact state between batches.

## Build and validation

The 3D renderer does not compile HLSL at runtime. Its 39 compute pipeline variants are
committed as DXBC for the three tile-geometry buckets used by 1x-4x, 5x-8x and
9x-16x. Screen dimensions, scale and scale-dependent buffer offsets travel in
`MetaUniform`, so a renderer switch or an internal-resolution change never
starts an HLSL compiler. Regenerate the table after changing the shader source:

```bash
python tools/dx12/compile-shaders.py
```

The small native-presentation vertex/pixel shader is still compiled during
presenter initialization through `d3dcompiler_47.dll`. The MinGW build needs no
DX12 import libraries: entry points are resolved with `GetProcAddress`, and only
`dxguid` is linked.

Because a shader error would only show up as a black screen on a machine with a
D3D12 GPU, the shader set has an offline audit:

```bash
python tools/ci/audits/check-dx12-shaders.py
```

It assembles the same sources used to generate the committed table and compiles
all 117 variants (39 pipeline variants times three tile-geometry buckets): the
37 common variants use `fxc.exe`/DXBC and the two native GPU2D variants use
`dxc.exe`/DXIL. A warning is a failure as well as a compile error. The audit
skips cleanly when the Windows SDK is not installed. CI separately verifies that
the committed shader-table source hash is current.

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
It intentionally is not keyed to the frontend-only developer-feature define:
the inline probe is included by both core and frontend translation units, so
giving those units different definitions would violate the C++ ODR. With the
environment variable unset its hot-path cost is the single disabled branch.

The 32 MiB upload ring is texture-only. The frame uniform and both 256x256
clear bitmaps use small persistently mapped upload resources whose reuse is
protected by the existing one-frame-in-flight wait. Texture-ring exhaustion
therefore falls back to a retained spill upload without starving required
frame metadata; failure to create or map that spill is propagated as a DX12
runtime failure instead of silently sampling an incomplete texture.

The 4x Rev 1 F7 audit scene on the RTX 5070 Ti measured zero capture reads,
zero texture-ring overflows, zero compositor drops and zero steady-state HUD
texture recreations. That evidence keeps the conditional capture redesign and
grow-only HUD allocation out of the hot-path change. Directly generating spans
into mapped upload memory was also tested and rejected: median
`build_polygons` increased from about 150 us to about 1.09 ms, while the
existing contiguous staging copy costs about 19 us. The CPU scratch plus bulk
copy is therefore intentional on this path.

A forced-overflow run temporarily reduced the texture upload ring to 1 MiB and
loaded the same F7 state. Its first measured window recorded six overflows and
151,552 spill bytes while continuing to present complete top/bottom screens and
the Custom HUD. No renderer fallback, device removal, or D3D12 debug-layer
error was emitted. The production ring remains 32 MiB.

The identical-frame branch is statically covered and instrumented, but no
natural positive fixture was found in the available MPH samples: Rev 1 F1-F8,
original-revision F1/F2/F7/F8, and a 30-second boot/title sequence all emitted
a GX flush every frame and reported `identical=0`. A forced renderer flag was
not used because it would freeze genuinely changing 3D and could not establish
the required stale-frame parity. Positive runtime coverage therefore still
requires a state that naturally leaves `FlushRequest` clear.

## Verified scope

The GPU-independent edge vectors and the opt-in Software/DX12 native 3D pixel
comparison are documented in [Raster parity verification](../../development/rendering/raster-parity.md).

The Windows Release build and all 117 generated shader modules (39 pipeline variants in
three tile-geometry buckets) pass on the repository build path. Runtime validation
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

The platform-scatter audit reports `used / allowed`; for example `21 / 22`
with `PASS` means one unit of remaining headroom, not one unresolved platform
branch.
