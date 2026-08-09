# Vulkan backend

Cross-platform 3D renderer and native presenter, rewritten from scratch on
`develop_remakeVulkan`. Like the DirectX 12 backend it is a port of melonDS's
**OpenGL compute renderer** (`src/GPU3D_Compute.cpp` + `GPU3D_Compute_shaders.h`)
— the GPU form of the software rasterizer — not of the fixed-function OpenGL
renderer.

Everything is behind a hard build gate. With `MELONPRIME_ENABLE_VULKAN` off (or
`MELONPRIME_FORCE_DISABLE_VULKAN` on), no Vulkan source, include path, compile
definition or loader dependency reaches either build target, and the Software /
OpenGL / OpenGL Compute / DX12 / Metal paths are untouched.

The rewrite replaced an earlier port of a third-party Android Vulkan backend
built on a *deferred 3D + latch + snapshot* architecture. That architecture ties
the DS `compMode` control word to a 2D **engine**, while Metroid Prime Hunters
flips `POWCNT1` bit 15 every frame, so the engine→LCD assignment alternates and
3D was blended on one phase but not the other. The design recorded here —
*immediate* high-resolution 3D plus structured software 2D planes composed in
the same frame — is structurally immune to that, and is the same shape the DX12
backend uses. The full rationale and the delivered-status table are in
[`docs/plans/rendering/vulkan/clean-room-rewrite-contract.md`](../../plans/rendering/vulkan/clean-room-rewrite-contract.md).

## Architecture

```
VulkanRenderer (: SoftRenderer)        software 2D engines + structured planes
 └── VulkanRenderer3D (: Renderer3D)   3D rasterizer + high-resolution compositor

ScreenPanelVulkan (: ScreenPanel)      Qt seam, layout/HUD/OSD
 └── VulkanPresenter                   own VkDevice, swapchain, vkQueuePresentKHR
```

The software 2D engines record the same per-pixel structured planes the DX12
backend consumes (`src/MelonPrimeStructuredComposition.h`, namespace
`StructuredComposition`; gated by `MELONPRIME_HAS_STRUCTURED_SOFT_2D`, which is
satisfied by *either* Vulkan or DX12 being enabled). After the DS scanlines are
complete, a Vulkan compute pass combines those planes with the high-resolution
3D target and reads back two BGRA screens at `256*scale x 192*scale`.
`RendererOutput` carries those dimensions to `ScreenPanelVulkan`, which performs
the established layout / Custom HUD / radar / OSD composition and then uploads
the screens, HUD and OSD as device-local images that are drawn as textured quads
into an acquired `VkSwapchainKHR` image.

The compute composition is submitted from `VulkanRenderer::VBlank()` on the
emulation thread. This binds the structured 2D planes and `FinalFB` to the same
DS frame even when software flips `ScreenSwap` every frame, so presentation
timing can never combine one frame's screen mapping with another frame's 3D
image.

A separate 256x192 resolve remains available for DS display capture and the
other core operations that need the `Renderer3D::GetLine()` contract.

Consequences:

* Internal resolution applies to the composed screen output, not only the 3D
  raster target.
* The presentation path has one high-resolution GPU readback per newly composed
  frame plus per-layer image uploads. The native resolve is read back only when
  display capture calls `GetLine()`.
* Vulkan selection owns presentation regardless of `Screen.UseGL`; no OpenGL
  context or Qt frame mailbox is involved, and there is **no CPU readback on the
  final path** — the last call of every frame is a real `vkQueuePresentKHR`.
* The presenter creates its **own** `VkDevice` from the shared `VulkanContext`
  rather than borrowing the renderer's. The renderer's device dies on every
  renderer switch while the panel must keep presenting across those switches
  (and while no renderer exists at all, for the splash screen). The
  renderer→presenter handoff is already CPU memory
  (`RendererOutput::CpuBgra` at the internal resolution), so no device-local
  resource is shared across the two devices.

### Presentation while paused

`ScreenPanelVulkan` follows the DX12 panel: `drawScreen()` normally returns as
soon as emulation is not running, because the swapchain keeps showing the last
presented image. The Custom HUD on-screen editor is the one paused state that
must keep composing, so `setHudEditModeActive()` latches
`hudEditLivePresentation` and lets the paused draw pass through. Unlike DX12,
the Vulkan panel also drops its modal-pause freeze overlay in that state. See
[`dx12-backend.md`](dx12-backend.md) for the shared reasoning.

## GPU3D_Compute stage mapping

Stages correspond one-for-one with `src/GPU3D_Compute.cpp` /
`GPU3D_Compute_shaders.h`:

| Stage | Pipeline indices | Variants |
|---|---|---|
| InterpSpans | 0-1 | 2 (Z-buffer, W-buffer) |
| BinCombined | 2 | 1 |
| DepthBlend | 3-4 | 2 (Z, W) |
| Rasterise | 5-20 | 8 kinds x 2 depth modes = 16 |
| ClearCoarseBinMask / ClearIndirectWorkCount / CalculateWorkOffsets / SortWork | 21-24 | 4 |
| FinalPass | 25-32 | 8 (edge marking / fog / AA combinations) |
| **Resolve** | 33 | 1 |
| **Compositor** | 34 | 1 |

The first 33 indices keep the OpenGL compute renderer's ordering, so indices are
directly comparable when debugging Vulkan against OpenGL Compute, and
`count = 33` still matches `ComputeRenderer3D::ShaderCompileStep()`. The two
presentation stages are *appended*, not interleaved: neither exists in
`GPU3D_Compute`. They were designed from the DS display semantics
(`GPU2D_Soft::ColorComposite`, `GPU_ColorOp.h`, `SoftRenderer::ExpandColor` /
`ApplyMasterBrightness`) and the structured composition contract; the DX12
shaders were used only to cross-check functional scope. DX12 numbers its own
steps the same way (`ShaderStep_Resolve` / `ShaderStep_Compositor`).

The eight rasterise kinds are `NoTexture`, `NoTextureToon`, `NoTextureHighlight`,
`UseTextureDecal`, `UseTextureModulate`, `UseTextureToon`, `UseTextureHighlight`,
`ShadowMask`.

Per frame, one command buffer:

1. `ClearCoarseBinMask` — reset the coarse tile bitmasks
2. `ClearIndirectWorkCount` — reset the per-variant work counters
3. `InterpSpans` (Z/W) — per-scanline X span interpolation
4. `BinCombined` — coarse + fine binning in one pass
5. `CalculateWorkOffsets` — per-variant offsets and split dispatch arguments
6. `SortWork` — sort the work list by variant
7. `Rasterise` — one indirect dispatch per variant, 16 variants
8. `DepthBlend` (Z/W) — clear plane, depth test, translucency, shadows
9. `FinalPass` — edge marking / fog / anti-aliasing resolve, 8 variants
10. `Resolve` — alpha-weighted box filter down to a 256x192 packed r6g6b6a5
    capture buffer for `GetLine()`
11. after the software 2D scanlines complete, `Compositor` combines the
    structured planes with the high-resolution `FinalFB`
12. copy the two high-resolution BGRA screens to the presentation readback

Pipelines are compiled incrementally through `ShaderCompileStep()`, so the OSD
shows progress instead of the emulator hitching.

## Scale buckets and specialization

`TileSize`, `CoarseTileCountY`, `CoarseTileArea` and
`ClearCoarseBinMaskLocalSize` all derive from `range = (scale>=5) + (scale>=9)`,
so internal resolutions 1x-16x collapse into **three compiled tile-geometry
buckets**: range 0 (1-4), range 1 (5-8), range 2 (9-16). Those four values are
injected as `-D` defines when the SPIR-V is generated offline.

`ScreenWidth`, `ScreenHeight` and `MaxWorkTiles` are **specialization constants**
(ids 0/1/2). Every derived value folds through `OpSpecConstantOp` at pipeline
creation, so there is no runtime cost relative to the OpenGL renderer's baked
literals, and one bucket covers every scale inside its range.

35 pipelines x 3 buckets = **105 SPIR-V modules** cover all 16 scales. The SPIR-V
is committed, so the build has no glslang dependency; the `.comp` / `.glsl` files
are inputs to the offline generator only and are deliberately not in any source
list.

`SetRenderSettings()` logs the active geometry, for example:

```
[Vulkan] internal resolution 4x -> 3D output 1024x768, tiles 128x96 (8px), tile-geometry bucket 0, capture resolve 256x192
[Vulkan] internal resolution 16x -> 3D output 4096x3072, tiles 128x96 (32px), tile-geometry bucket 2, capture resolve 256x192
```

### Scale refusal, not clamping

`VulkanFeatureProbe` walks every scale factor against the device's real
`VkPhysicalDeviceLimits` and device-local memory budget
(`maxStorageBufferRange`, `maxTexelBufferElements`,
`maxComputeWorkGroupInvocations` / `Size` / `Count`, `maxImageDimension2D`) and
records the highest one the device can actually run, together with the limit that
decided it:

```
[Vulkan] NVIDIA GeForce RTX 5070 Ti ok   Internal resolution -- up to 16x supported
[Vulkan] selected NVIDIA GeForce RTX 5070 Ti (NVIDIA) -- score 1175, up to 16x internal resolution
```

`VulkanRenderer3D::SetRenderSettings()` then **refuses** a scale above that
ceiling instead of silently dropping to a lower one, because silently rendering
at a different resolution than the user asked for would misreport what is on
screen. The refusal goes through `SetRuntimeFailure()`, so the emulation thread
reports the stored reason and switches to Software rather than continuing with a
partial frame.

Two defects are inherited verbatim from the OpenGL compute renderer rather than
"fixed", to keep the 1:1 correspondence that makes the port verifiable. Both are
flagged in-source:

1. **`BinCombined` shift-width UB at scales 9-16.** The workgroup is sized by
   `CoarseTileArea` (48 at range 2) but the coarse pass treats invocations as a
   32-lane ballot, so lanes 32-47 evaluate `1U << localIdx`, undefined in GLSL
   for shifts >= 32.
2. **Scale-16 `InterpSpans` worst-case dispatch is 65536 groups**, one over
   Vulkan's guaranteed `maxComputeWorkGroupCount` of 65535. Reachable only when
   the span buffer is fully consumed.

## Descriptor contract

`src/VulkanDescriptors.h` is the single source of truth. The GLSL sources declare
exactly these set/binding numbers, and the feature probe derives its
descriptor-limit checks from the same tables, so one edit propagates to both.

Two sets, split by update frequency: set 0 changes at most once per frame, set 1
whenever the bound DS texture changes (which can happen many times per frame).
Merging them would force a rewrite of all fourteen set-0 descriptors on every
texture switch.

| Set 0 binding | Type | Used by |
|---|---|---|
| 0 `MetaUniform` | `UNIFORM_BUFFER` | all |
| 1 `PolygonBuffer` | `STORAGE_BUFFER`, readonly | rasterizer |
| 2 `XSpanSetups` | `STORAGE_BUFFER` | rasterizer |
| 3 `YSpanSetups` | `STORAGE_BUFFER` | rasterizer |
| 4 `ColorTiles` | `STORAGE_BUFFER` | rasterizer |
| 5 `DepthTiles` | `STORAGE_BUFFER` | rasterizer |
| 6 `AttrTiles` | `STORAGE_BUFFER` | rasterizer |
| 7 `ResultBuffer` | `STORAGE_BUFFER` | rasterizer |
| 8 `BinResultBuffer` | `STORAGE_BUFFER` | rasterizer |
| 9 `WorkDescBuffer` | `STORAGE_BUFFER` | rasterizer |
| 10 `SetupIndices` | `UNIFORM_TEXEL_BUFFER`, `R16G16B16A16_UINT` | rasterizer |
| 11 `FinalFB` | `STORAGE_IMAGE`, `R8G8B8A8_UNORM` | rasterizer, Resolve, Compositor |
| 12 `StructuredInput` | `STORAGE_BUFFER`, readonly | Compositor |
| 13 `PresentationOut` | `STORAGE_BUFFER` | Resolve, Compositor |

| Set 1 binding | Type |
|---|---|
| 0 `CurrentTexture` | `COMBINED_IMAGE_SAMPLER`, `usampler2DArray` |
| 1 `Capture128Texture` | `COMBINED_IMAGE_SAMPLER`, `sampler2DArray` |
| 2 `Capture256Texture` | `COMBINED_IMAGE_SAMPLER`, `sampler2DArray` |
| 3 `ClearBitmapColor` | `COMBINED_IMAGE_SAMPLER`, `usampler2D` |
| 4 `ClearBitmapDepth` | `COMBINED_IMAGE_SAMPLER`, `usampler2D` |

Bindings 12 and 13 were appended for the presentation stages; **no rasterizer
binding number ever moved**. `PresentationOut` is one binding rather than two
because Resolve and Compositor never run in the same dispatch: the host binds the
native-resolution capture buffer for one and the two-screen composed buffer for
the other. That is what the second set-0 allocation is for —
`DescriptorPoolSizing::RasterizerSetsPerFrame` is 2, slot 0 the rasterizer's and
slot 1 the compositor's. Set 1 is not bound at all for the compositor, which
declares no set-1 resource.

Capture textures (set 1 bindings 1/2) are **inactive by design**, exactly as on
DX12: this backend pairs with the software 2D renderer, which writes captures
into real VRAM, so `pc.TexIsCapture` is always 0 and those bindings point at a
cleared 1x1x1 placeholder array. The bindings and the `CaptureYOffset` push
constant stay plumbed in case GPU-side captures are reintroduced.

## Synchronisation design

**No `vkDeviceWaitIdle` / `vkQueueWaitIdle` on any per-frame path.** The only
permitted `WaitIdle` sites are resolution changes (which destroy every
resolution-sized resource) and shutdown.

**Frames in flight is 1 for the rasterizer, 2 for the presenter.** `XSpanSetups`,
the three tile buffers, `BinResult`, `WorkDescs`, `ResultBuffer` and `FinalFB`
form a single shared working set; a second in-flight rasterizer frame would be a
WAR/WAW race on all of them, and duplicating the set costs roughly 1 GB at 16x.
The frame fence is waited at the *start* of frame N for frame N-1, with a whole
DS frame of software-2D work in between, so CPU/GPU overlap is preserved. This is
the same shape DX12 uses. `Vk::FramesInFlight = 2` governs the presenter's
per-frame CPU-side resources and is deliberately independent of the swapchain
image count (typically 3 for FIFO).

**The compositor records into its own `Vk::FrameRing`** — own command pool,
command buffer and fence — rather than the rasterizer's. It has to: the
structured planes are only complete after all 192 scanlines, long after
`RenderFrame()` submitted, and reusing the rasterizer's slot would reset the
fence that `GetLine()`'s capture readback is still waiting on. Both rings submit
to the same queue, so the compositor's barrier over `FinalFB` picks up the
rasterizer's writes through submission order. `ComposeStructuredOutput()` waits on
its own fence before returning, which both publishes the frame and stops the next
`RenderFrame()` from overwriting `FinalFB` while the compositor still reads it.

**Object lifetime goes through one chokepoint.** `Vk::DeferredDestroyQueue`
(`src/VulkanSync.h`) takes a handle plus the absolute frame number that last
referenced it and retires it only after that frame's fence has signalled. Nothing
calls `vkDestroy*` directly once rendering has started. The queue is locked
because renderer resources are enqueued from the emulation thread and
swapchain/surface-sized resources from the GUI thread.

## Presentation path

`VulkanPresenter` (`src/frontend/qt_sdl/MelonPrimeVulkanPresenter.{h,cpp}`) owns
the swapchain and does the real present. It keeps one persistent device-local
image per layer — `ScreenTop`, `ScreenBottom`, `Hud`, `Osd` — reallocated only
when its dimensions change, so a steady-state frame creates no images. Each layer
is drawn as a textured quad with either `Opaque` blending (the composed screens,
whose alpha carries no coverage) or `Premultiplied`
(`QImage::Format_ARGB32_Premultiplied` sources, the same factors `ScreenPanelGL`
uses).

Present mode is **queried, never assumed**:

| Requested | Chosen | Logged reason |
|---|---|---|
| VSync on | `FIFO` | FIFO is the specification-guaranteed VSync mode |
| VSync off | `MAILBOX` | VSync off, MAILBOX supported (no tearing, no frame-rate cap) |
| VSync off, no MAILBOX | `IMMEDIATE` | VSync off, MAILBOX unsupported, IMMEDIATE supported |
| VSync off, neither | `FIFO` | surface supports neither; VSync remains effectively on |

```
[Vulkan] swapchain created extent=284x406 images=3 format=44 Requested VSync=off / Requested Present Mode=MAILBOX>IMMEDIATE / Actual Present Mode=MAILBOX / Reason=VSync off, MAILBOX supported (no tearing, no frame-rate cap)
[Vulkan] first frame presented extent=256x384 presentMode=MAILBOX
```

The present mode is immutable once a swapchain exists, so `SetVSync()` marks the
swapchain for rebuild rather than trying to mutate it. `ScreenPanelVulkan`
re-reads `Screen.VSync` every frame, so the Video settings checkbox applies live.

GUI-thread events (resize, DPI change, fullscreen transition) only set atomic
flags; they never touch a Vulkan object. Swapchain recreation happens inside
`BeginFrame()` on the presenting thread.

### Window-system integration

One platform adapter per OS behind `MelonPrime::VulkanSurface`, each additionally
guarded by its own `#if` so an accidental double-add cannot produce duplicate
symbols:

| Platform | Extension | File |
|---|---|---|
| Windows | `VK_KHR_win32_surface` | `MelonPrimeVulkanSurfaceWin32.cpp` |
| Linux | `VK_KHR_xcb_surface`, `VK_KHR_xlib_surface`, `VK_KHR_wayland_surface` | `MelonPrimeVulkanSurfaceLinux.cpp` |
| macOS | `VK_EXT_metal_surface` over a `CAMetalLayer` (MoltenVK) | `MelonPrimeVulkanSurfaceMacOS.mm` |
| BSD / other | none; fails loudly at runtime | `MelonPrimeVulkanSurfaceStub.cpp` |

The Linux adapter chooses its backend from the **live** Qt platform plugin
(`QGuiApplication::platformName()` plus the handles Qt actually hands out), never
from a compile-time guess: the same binary runs on X11, XWayland and native
Wayland, and the required WSI extension differs in all three. XCB is tried first
because it is the transport Qt's own `xcb` plugin speaks, with Xlib as fallback.
On Linux only, the panel also re-binds the presenter when a compositor or Qt
reparent hands out a new native window (`prepareLinuxPresentationSurface()`),
because the old `VkSurfaceKHR` then refers to a window that no longer exists.

None of the platform units define `VK_USE_PLATFORM_*`. Doing so would add members
to `Vk::InstanceDispatch` for that translation unit alone and give the struct two
layouts in one binary — an ODR violation with no diagnostic. Each adapter instead
declares the platform create-info struct locally (their layouts are fixed by the
registry and the `sType` values are in `vulkan_core.h` unconditionally) and
resolves its `vkCreate*SurfaceKHR` through the caller-supplied
`vkGetInstanceProcAddr`.

For the same class of reason, `src/VulkanLoader.h` `#undef`s `CreateSemaphore`
before defining `DeviceDispatch`: `<windows.h>` defines it as a macro, so any
translation unit that pulled in `windows.h` first compiled the member — and every
use of it — under the expanded name `CreateSemaphoreW`, giving `DeviceDispatch`
two definitions in one binary. GCC's LTO `-Wodr` reports it.

## Low-latency integration

`ScreenPanel` declares five virtuals under `MELONPRIME_ENABLE_VULKAN` that the
emulation thread calls: `beginVulkanLowLatencyFrame`,
`markVulkanReflexInputSample`, `markVulkanReflexRenderSubmitStart`,
`markVulkanReflexRenderSubmitEnd`, `finishVulkanLowLatencyFrame`. The markers
correspond to real `vkQueueSubmit` / `vkQueuePresentKHR` boundaries, not to Qt
paint events.

**NVIDIA Reflex** uses the native `VK_NV_low_latency2` device extension, which
also requires `VK_KHR_present_id` and timeline semaphores. It enables latency
mode on each swapchain, associates the final graphics submission and
`vkQueuePresentKHR` with the same monotonically increasing Present ID, and
publishes Input Sample / Simulation / Render Submit / Present markers. The
emulation thread calls `vkLatencySleepNV` followed by a host wait on the
extension's timeline semaphore once per frame, immediately before late input
polling. Minimum interval is always zero, so Reflex never adds a frame-rate cap.

**AMD Anti-Lag 2** uses the native `VK_AMD_anti_lag` device extension and its
`antiLag` feature. Every emulated frame gets one monotonically increasing frame
ID: an INPUT update immediately before late input polling, a matching PRESENT
update immediately before `vkQueuePresentKHR`. `maxFPS` is always zero.

Both report requested / supported / enabled / actual / reason and **never fail
renderer creation**:

```
[Vulkan] NVIDIA Reflex (VK_NV_low_latency2): requested=yes supported=yes enabled=yes actual=active reason=enabled at device creation
[Vulkan] AMD Radeon Anti-Lag 2 (VK_AMD_anti_lag): requested=yes supported=no enabled=no actual=disabled reason=this GPU does not expose VK_AMD_anti_lag (Radeon Anti-Lag 2 is AMD-only)
[Vulkan] NVIDIA Reflex mode=on lowLatencyMode=true lowLatencyBoost=false
```

Configuration keys are shared with DX12 for compatibility:
`3D.DX12.NvidiaReflexMode` and `3D.AMD.AntiLag2Enabled`. Feature state and frame
IDs belong to each presenter/emulator instance; no process-global latency or
pacing state is introduced.

## Files

| File | Role |
| --- | --- |
| `src/VulkanCommon.{h,cpp}` | shared types, `VkResult` formatting, debug object naming |
| `src/VulkanLoader.{h,cpp}` | runtime loader; global / instance / device dispatch tables |
| `src/VulkanContext.{h,cpp}` | instance, validation layer, debug utils, physical-device selection |
| `src/VulkanDevice.{h,cpp}` | logical device, queue families, queues, per-device scale ceiling |
| `src/VulkanFeatureProbe.{h,cpp}` | extension / feature / format / limit probing and its log |
| `src/VulkanMemory.{h,cpp}` | allocation, buffers, images, staging, readback |
| `src/VulkanDescriptors.{h,cpp}` | set layouts, pool sizing, updates — the binding contract |
| `src/VulkanSync.{h,cpp}` | frame rings, fences, semaphores, barriers, deferred destruction |
| `src/VulkanNvidiaReflex.{h,cpp}` | `VK_NV_low_latency2` |
| `src/VulkanAmdAntiLag.{h,cpp}` | `VK_AMD_anti_lag` |
| `src/GPU3D_TexcacheVulkan.{h,cpp}` | texture array behind the shared `Texcache<>` template |
| `src/GPU3D_Vulkan.{h,cpp}` | the renderer: span setup, dispatch orchestration, `VkPipelineCache`, shader modules |
| `src/GPU3D_Vulkan_shaders/*.comp,*.glsl` | GLSL sources — offline generator inputs only |
| `src/GPU3D_Vulkan_shaders/generated/` | committed SPIR-V blobs and the module table (build inputs) |
| `src/GPU_Vulkan.{h,cpp}` | `VulkanRenderer`, pairing the 3D renderer with software 2D |
| `src/frontend/qt_sdl/MelonPrimeVulkanFeatureCheck.{h,cpp}` | runtime availability probe for the settings dialog |
| `src/frontend/qt_sdl/MelonPrimeVulkanSurface*.{h,cpp,mm}` | per-platform WSI adapters |
| `src/frontend/qt_sdl/MelonPrimeVulkanPresenter.{h,cpp}` | swapchain, layer images, `vkQueuePresentKHR` |
| `src/frontend/qt_sdl/MelonPrimeVulkanPresentShaders/` | committed present vertex/fragment SPIR-V |
| `src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp` | `ScreenPanelVulkan`, the only Qt seam |

## Build and validation

No static link against the Vulkan loader: the shared library is opened by name at
runtime and every entry point resolves through `vkGetInstanceProcAddr` /
`vkGetDeviceProcAddr`, so a single binary starts on a machine with no Vulkan
runtime at all. Only the headers are needed at build time
(`MELONPRIME_VULKAN_INCLUDE_DIR`). Public defines on the core target:
`MELONPRIME_ENABLE_VULKAN=1`, `VK_NO_PROTOTYPES=1`, and on Windows
`VK_USE_PLATFORM_WIN32_KHR=1`.

The Khronos validation layer is enabled automatically when it is installed, but
only in builds compiled with `MELONDS_VULKAN_ENABLE_VALIDATION` — set by CMake
for `Debug` configurations. Its absence is normal and must never fail startup.

Because the SPIR-V is committed, a shader change that is not regenerated would
otherwise be invisible. Two offline gates cover that:

```bash
python tools/vulkan/compile-shaders.py --check      # committed SPIR-V is up to date
python tools/ci/audits/check-vulkan-shaders.py      # all 105 variants compile + spirv-val
```

`check-vulkan-shaders.py` assembles exactly the sources the generator builds —
same `#define` prologue, same per-variant defines — compiles all 35 pipelines in
all 3 tile-geometry buckets, validates every module with `spirv-val`, additionally
validates the 560 scale-specialized modules for scales 1..16, and prints a
device-limit note for every scale/resource that exceeds Vulkan's *guaranteed*
minimums so the host-side probe requirement stays visible. It skips cleanly when
glslang or spirv-val are not installed.

The present-path shaders have their own generator,
`tools/vulkan/compile-present-shaders.py`, whose output
(`MelonPrimeVulkanPresentShaderBlobs.h`) is likewise committed.

## Differences from the OpenGL compute renderer

These are the only intentional behavioural deviations; the fixed-point math,
binning, span setup, blending and tile-geometry derivation are a 1:1 port.

* **Display capture as a texture is not special-cased.** The 2D engines are the
  software ones, so captures land in real VRAM and the ordinary texcache lookup
  already returns them.
* **`GetLine()` downscaling is a compute pass, not a blit.** `Resolve.comp`
  (pipeline 33) performs the same alpha-weighted box filter DX12 does and writes
  packed r6g6b6a5 straight into a device-local buffer that is copied to the
  readback allocation, so `EnsureFrameReadback()` is a plain `memcpy` and the
  UNORM8 → 6-bit reconstruction happens on the GPU. An interim `vkCmdBlitImage`
  path filtered a 2x2 neighbourhood and filtered colour independently of alpha,
  which aliased from 4x upward; it is gone, along with `NativeResolveImage` and
  the linear-filter format query.
* **Tile geometry is specialization-constant driven rather than baked**, which is
  what lets three compiled buckets cover sixteen scales.

## Known limitations

* The two inherited OpenGL-compute defects above (`BinCombined` shift-width UB at
  scales 9-16; scale-16 `InterpSpans` worst-case dispatch count) are transcribed
  deliberately and still need a decision before the highest scales are promoted.
* From scale 7 the storage buffers exceed Vulkan's guaranteed 128 MB
  `maxStorageBufferRange`, and from scale 9 Rasterise/DepthBlend use 1024
  invocations per workgroup, above the guaranteed 128. These are arithmetic
  predictions about the *guaranteed minimums*, not observed driver failures; the
  host probe is what decides whether a given device may offer those scales.
* The presenter reads the composed screens back to the CPU before uploading them
  as layer images, because the panel reuses the established QImage-based layout /
  HUD / OSD implementation. This is the same trade-off DX12 makes; the final
  present itself involves no readback.
* AMD Anti-Lag 2 has never been exercised on AMD hardware, and no non-NVIDIA GPU,
  Linux system or macOS system has run this backend. See the status table in
  [`docs/plans/rendering/vulkan/clean-room-rewrite-contract.md`](../../plans/rendering/vulkan/clean-room-rewrite-contract.md)
  for the complete verified/unverified breakdown.
