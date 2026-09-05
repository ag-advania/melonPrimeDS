# Vulkan backend

Cross-platform 3D renderer and native presenter, originally rewritten from scratch on
`develop_remakeVulkan` and currently maintained on `develop_hud` at `2a0266f14`
(checkout verified 2026-08-28). Like the DirectX 12 backend it is a port of melonDS's
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
 └── VulkanRenderer3D (: Renderer3D)   3D rasterizer + composition orchestration
     └── VulkanGpu2DComposer            structured/native 2D composition + output ring

ScreenPanelVulkan (: ScreenPanel)      Qt seam, layout/HUD/OSD
 └── VulkanPresenter                   shared VkDevice, swapchain, vkQueuePresentKHR
```

The software 2D engines record the same per-pixel structured planes the DX12
backend consumes (`src/MelonPrimeStructuredComposition.h`, namespace
`StructuredComposition`; gated by `MELONPRIME_HAS_STRUCTURED_SOFT_2D`, which is
satisfied by *either* Vulkan or DX12 being enabled). After the DS scanlines are
complete, a Vulkan compute pass combines those planes with the high-resolution
3D target into one of three device-local BGRA frame buffers at
`256*scale x 192*scale`. `RendererOutputLease` carries the slot to
`ScreenPanelVulkan`; the presenter copies both screens directly into its sampled
images on the same logical device, then draws the established layout / Custom
HUD / radar / OSD composition into an acquired `VkSwapchainKHR` image.

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
* The visible presentation path stays on the GPU: compositor buffer → sampled
  screen images → swapchain. The native 256x192 resolve is read back only when
  display capture calls `GetLine()`.
* Vulkan selection owns presentation regardless of `Screen.UseGL`; no OpenGL
  context or Qt frame mailbox is involved, and there is **no CPU readback on the
  final path** — the last call of every frame is a real `vkQueuePresentKHR`.
* `VulkanDevice` is a ref-counted view of one process-wide logical device. The
  presenter normally creates it first with the surface-aware queue selection;
  renderer switches release only their view. The renderer and presenter share
  the main queue, whose host submissions are serialized by the device's queue
  mutex.

### Presentation while paused

`ScreenPanelVulkan` follows the DX12 panel: `drawScreen()` normally returns as
soon as emulation is not running, because the swapchain keeps showing the last
presented image. The Custom HUD on-screen editor is the one paused state that
must keep composing, so `setHudEditModeActive()` latches
`hudEditLivePresentation` and lets the paused draw pass through. Unlike DX12,
the Vulkan panel also drops its modal-pause freeze overlay in that state. See
[`dx12-backend.md`](dx12-backend.md) for the shared reasoning.

### Presenting software-renderer frames

`3D.ForceSoftwareOutsideMatch` swaps the 3D renderer to `SoftRenderer` while
this panel keeps owning the surface and swapchain — `prepareForRendererTransition()`
quiesces GPU work and drops renderer-owned leases but deliberately keeps the
`VkSurfaceKHR`/`VkSwapchainKHR` pair alive. `drawScreenFrame()` then presents
the software renderer's `CpuBgra` output through `VulkanPresenter::UploadLayer()`.

A live `VulkanRenderer` publishing `CpuBgra` means something else entirely (the
startup hybrid: Software 2D plus a 3D placeholder, still compiling pipelines),
and stays hidden. The `dynamic_cast<VulkanRenderer*>` result is the
discriminator between the two. See
[`dx12-backend.md`](dx12-backend.md#presenting-software-renderer-frames) for the
full contract, including the frame-identity and native-visibility handling; the
Vulkan panel mirrors it exactly.

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
| **CaptureSidecar** | 34 | 1 |
| **Compositor** | 35 | 1 |
| **CorrectCoverage** | 36 | 1 |
| **GPU2DNative** | 37-38 | 2 (128- and 256-workgroup variants) |

The first 33 indices keep the OpenGL compute renderer's ordering, so indices are
directly comparable when debugging Vulkan against OpenGL Compute, and
`count = 33` still matches `ComputeRenderer3D::ShaderCompileStep()`. The six
native-capture/presentation/composition/parity stages are *appended*, not interleaved: none
exists in `GPU3D_Compute`. They were designed from the DS display semantics
(`GPU2D_Soft::ColorComposite`, `GPU_ColorOp.h`, `SoftRenderer::ExpandColor` /
`ApplyMasterBrightness`) and the structured composition contract; the DX12
shaders were used only to cross-check functional scope. DX12 numbers its own
steps the same way (`ShaderStep_Resolve` / `ShaderStep_Compositor`), followed by
the two native GPU2D variants.

The eight rasterise kinds are `NoTexture`, `NoTextureToon`, `NoTextureHighlight`,
`UseTextureDecal`, `UseTextureModulate`, `UseTextureToon`, `UseTextureHighlight`,
`ShadowMask`.

### Polygon geometry settings

Vulkan deliberately does not consume `3D.GL.BetterPolygons`. That option is a
center-fan workaround for classic OpenGL and native Metal raster, where an N-sided DS
polygon must first be split into GPU triangles and the new internal edges can
change interpolation. Metal raster now enables this workaround unconditionally
in its backend and locks the setting in Video Settings. The Vulkan compute
rasterizer performs no such split:
`BuildPolygons()` walks the original polygon's left and right edges and creates
one X span per scanline, matching `GPU3D_Compute`. Adding the center-fan path
would replace the compute renderer's native polygon interpolation rather than
improve it.

The Video Settings dialog therefore disables Improved polygon splitting for
Vulkan and explains why. Vulkan also uses high-resolution vertex positions,
but MelonPrimeDS now forces `3D.GL.HiresCoordinates=true`: the checkbox is
checked and disabled, and the backend applies the effective value live without
rebuilding resolution-sized resources. See
[High-resolution vertex coordinates](high-resolution-coordinates.md) for the
cross-backend policy and 1x exception.

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
12. publish the device-local composed buffer as a leased presentation slot

Pipeline objects are created incrementally from the committed SPIR-V through
`ShaderCompileStep()`, so the OSD shows progress instead of the emulator hitching.

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

39 pipelines x 3 buckets = **117 SPIR-V modules** cover all 16 scales. The SPIR-V
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

Two formerly inherited OpenGL-compute limits are closed without changing the
raster result. `BinCombined` restricts the polygon ballot and its shift to lanes
0-31 while lanes 32-47 still process the extra fine tiles at scales 9-16.
`InterpSpans` chunks the setup-index stream by the selected device's
`maxComputeWorkGroupCount[0]`, widening the multiplication before the cap so
MoltenVK-sized limits cannot wrap. Both rules are enforced by the shader/parity
audits.

## Descriptor contract

`src/VulkanDescriptors.h` is the single source of truth. The GLSL sources declare
exactly these set/binding numbers, and the feature probe derives its
descriptor-limit checks from the same tables, so one edit propagates to both.

Two sets, split by update frequency: set 0 changes at most once per frame, set 1
whenever the bound DS texture changes (which can happen many times per frame).
Merging them would force a rewrite of all seventeen set-0 descriptors on every
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
| 14 `CaptureSidecar` | `STORAGE_BUFFER` | retained display-capture sampling |
| 15 `BlendState` | `STORAGE_BUFFER` | cross-batch shadow/stencil continuation |
| 16 `ResultWinner` | `STORAGE_BUFFER` | native accepted-pixel AA correction |

| Set 1 binding | Type |
|---|---|
| 0 `CurrentTexture` | `COMBINED_IMAGE_SAMPLER`, `usampler2DArray` |
| 1 `Capture128Texture` | `COMBINED_IMAGE_SAMPLER`, `sampler2DArray` |
| 2 `Capture256Texture` | `COMBINED_IMAGE_SAMPLER`, `sampler2DArray` |
| 3 `ClearBitmapColor` | `COMBINED_IMAGE_SAMPLER`, `usampler2D` |
| 4 `ClearBitmapDepth` | `COMBINED_IMAGE_SAMPLER`, `usampler2D` |

Bindings 12-16 were appended for presentation, retained capture and native
coverage correction; **no original rasterizer binding number ever moved**.
`PresentationOut` is one binding rather than two
because Resolve and Compositor never run in the same dispatch: the host binds the
native-resolution capture buffer for one and the two-screen composed buffer for
the other. That is what the second set-0 allocation is for —
`DescriptorPoolSizing::RasterizerSetsPerFrame` is 2, slot 0 the rasterizer's and
slot 1 the compositor's. Set 1 is not bound at all for the compositor, which
declares no set-1 resource.

Capture image samplers (set 1 bindings 1/2) are **inactive by design** and point
at a cleared 1x1x1 placeholder array. This backend pairs with software 2D, so
retained display-capture sampling uses binding 14's packed sidecar instead;
`pc.TexIsCapture`, `CaptureYOffset` and the capture reference select that data.

## Synchronisation design

**No `vkDeviceWaitIdle` / `vkQueueWaitIdle` on any per-frame path.** The only
permitted `WaitIdle` sites are resolution changes (which destroy every
resolution-sized resource) and shutdown.

**Frames in flight is 1 for the rasterizer, 3 for the compositor output and 2
for the presenter.** `XSpanSetups`,
the three tile buffers, `BinResult`, `WorkDescs`, `ResultBuffer` and `FinalFB`
form a single shared working set; a second in-flight rasterizer frame would be a
WAR/WAW race on all of them, and duplicating the set costs roughly 1 GB at 16x.
The frame fence is waited at the *start* of frame N for frame N-1, with a whole
DS frame of software-2D work in between, so CPU/GPU overlap is preserved. This is
the same shape DX12 uses. `Vk::FramesInFlight = 2` governs the presenter's
per-frame CPU-side resources and is deliberately independent of the swapchain
image count (typically 3 for FIFO).

**The compositor records into its own three-slot `Vk::FrameRing`** — each slot
owns its command buffer/fence, structured staging buffer, device-local structured
input and composed output. Both rings submit to the same queue, so barriers over
`FinalFB` provide raster-write → compositor-read and compositor-read → next
raster-write dependencies through submission order. `ComposeStructuredOutput()`
submits and returns without a CPU fence wait. A presenter lease prevents slot
reuse until the presenter's frame fence proves its buffer-to-image copies have
retired; when all slots are busy, the compositor reuses the last published frame
instead of blocking VBlank.

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
uses). Custom HUD updates upload the union of the current and previous dirty
rectangles into the persistent HUD image. This updates both newly painted pixels
and pixels cleared after an element moved without copying the entire window-sized
overlay. The radar continues to sample the GPU bottom-screen source directly.

Present mode is **queried, never assumed**:

| Requested | Chosen | Logged reason |
|---|---|---|
| VSync on | `FIFO` | VSync on: FIFO |
| VSync off | `IMMEDIATE` | VSync off: IMMEDIATE supported |
| VSync off, no IMMEDIATE | `MAILBOX` | IMMEDIATE unavailable; MAILBOX tear-free fallback |
| VSync off, neither | `FIFO` | only FIFO-compatible path is available |

```
[Vulkan] presentation: requested-vsync=off available-present-modes=IMMEDIATE,MAILBOX,FIFO selected-present-mode=IMMEDIATE swapchain-images=3 extent=284x406 format=44 window-mode=windowed reason=VSync off: IMMEDIATE supported; VRR actual state is driver/display controlled
[Vulkan] first frame presented extent=256x384 presentMode=IMMEDIATE
```

The present mode is immutable once a swapchain exists, so `SetVSync()` marks the
swapchain for rebuild rather than trying to mutate it. `ScreenPanelVulkan`
re-reads `Screen.VSync` every frame, so the Video settings checkbox applies live.
The OpenGL-style VSync Interval control is disabled with an explanatory tooltip
while native Vulkan is selected; the saved interval remains available to other
renderers.

The presenter reports the selected present mode and window mode, but it never
claims that G-SYNC, FreeSync or another VRR path is active: that state remains
under driver/display control. `VK_EXT_full_screen_exclusive` is intentionally
not part of the current implementation. It remains an optional future addition
rather than a partially enabled presentation path.

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
| FreeBSD / NetBSD / OpenBSD | `VK_KHR_xcb_surface`, `VK_KHR_xlib_surface` fallback | `MelonPrimeVulkanSurfaceBSD.cpp` |
| Other Unix | none; fails loudly at runtime | `MelonPrimeVulkanSurfaceStub.cpp` |

The Linux adapter chooses its backend from the **live** Qt platform plugin
(`QGuiApplication::platformName()` plus the handles Qt actually hands out), never
from a compile-time guess: the same binary runs on X11, XWayland and native
Wayland, and the required WSI extension differs in all three. XCB is tried first
because it is the transport Qt's own `xcb` plugin speaks, with Xlib as fallback.
On Linux only, the panel also re-binds the presenter when a compositor or Qt
reparent hands out a new native window (`prepareLinuxPresentationSurface()`),
because the old `VkSurfaceKHR` then refers to a window that no longer exists.

The BSD adapter is intentionally narrower than the Linux adapter: it accepts
only Qt's `xcb` platform plugin, uses Qt 6.5+'s public
`QNativeInterface::QX11Application` to obtain the live XCB connection and Xlib
display, tries `VK_KHR_xcb_surface` first, and falls back to
`VK_KHR_xlib_surface`. Older Qt versions, non-XCB plugins, and native Wayland
are explicit unsupported failures. The BSD workflow builds this adapter with
Vulkan enabled and also compiles a separate hard-disabled variant; the
workflow itself remains the authoritative BSD build evidence.

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
publishes Input Sample / Simulation / Render Submit / Present markers. Input
Sample is placed immediately before the first input read; Simulation Start
follows `RunFrameHook()` and `SetKeyMask()`. The helper rejects duplicate or
reversed Input Sample / Simulation markers. Minimum interval is always zero, so
Reflex never adds a frame-rate cap.

`vkLatencySleepNV` blocks inside the driver rather than returning immediately:
measured p50 1798 us with pacing off and p50 1247 us with pacing on, while the
`vkWaitSemaphores` that follows it is p50 2 us. Where that block is paid is
mode-dependent:

- **Off** issues the sleep on a persistent worker thread immediately after the
  previous accepted present and joins it just before `vkQueuePresentKHR`, so the
  whole emulated frame overlaps the driver-side block. Its hot-path cost falls
  to p50 191 us.
- **On / On+Boost** issue and wait inline in `BeginFrame`, before input
  sampling, because Reflex exists to place input directly after the pacing wait
  returns. Their block cannot be overlapped: the only work available before
  input is the presenter cleanup between present and the next frame. That
  ~1.25 ms is the pacing itself, not overhead to schedule around -- preissuing
  for these modes was measured and rejected on 2026-08-24
  (`docs/archive/audits/rendering/2026-08/vulkan_reflex_on_preissue_ab_2026-08-24.md`).

Each queued sleep is stamped with the frame generation allowed to consume it.
The frame that is still open when the next frame's sleep is issued must not join
it: doing so serialises the full block back onto the CPU hot path and regressed
the Off path to ~330 FPS before generation ownership was introduced.
`vkGetLatencyTimingsNV` reporting, the marker set, and Present ID correlation
are unchanged by the placement. Cold start, swapchain recreation, mode changes
and any Off frame with no preissued sleep to adopt fall back to the same inline
issue-and-wait, so correctness never depends on the request having been made
early.

**AMD Anti-Lag 2** uses the native `VK_AMD_anti_lag` device extension and its
`antiLag` feature. Every emulated frame gets one monotonically increasing frame
ID: an INPUT update immediately before late input polling, a matching PRESENT
update immediately before `vkQueuePresentKHR`. `maxFPS` is always zero.

The Anti-Lag 2 name is intentional. AMD's current product page lists Vulkan
support from Adrenalin 24.9.1 onward and identifies `VK_AMD_anti_lag` as the
Vulkan integration used for Anti-Lag 2. The Khronos extension description uses
the older Anti-Lag / Anti-Lag+ terminology, but that does not require a
different user-facing product label.

Reflex Off disables `lowLatencyMode` and boost but deliberately continues
`vkLatencySleepNV`, latency markers, submission Present ID and
`VkPresentIdKHR`. NVIDIA's Reflex QA contract requires those calls and PCL
timestamps to keep updating in all three UI modes; with pacing disabled the
sleep semaphore is released without adding the low-latency delay.

**Vendor-neutral present pacing** is surface-scoped and optional. The instance
enables `VK_KHR_get_surface_capabilities2` when available; device creation then
probes and conditionally enables `VK_KHR_present_id2`,
`VK_KHR_present_wait2`, `VK_KHR_calibrated_timestamps`,
`VK_EXT_present_timing`, and `VK_KHR_present_mode_fifo_latest_ready`. A missing
extension, feature bit, entry point, or surface capability falls back to the
legacy surface query and host limiter without disabling Vulkan.

The optional late-wait authority is selected once per frame in this order:
NVIDIA Reflex, AMD Anti-Lag 2, generic present timing (claimed by either the
bounded wait or target-time scheduling), no optional late wait.
The existing host limiter remains the sole exact frame-rate cap whenever the FPS
limit is enabled; vendor latency APIs and the bounded present wait must not turn
that toggle into unlimited rendering. Generic `vkWaitForPresent2KHR` is bounded
to 2 ms, runs immediately before late input, waits each accepted Present ID at
most once, and resets on swapchain recreation or out-of-date results.
Fast-forward, slow-motion, and unlimited-FPS frames never use the refresh-bound
generic path.
Timing reports are drained every frame (logged every 600 developer frames); if
the optional results queue is nevertheless full, the same image and Present ID
are retried once without timing metadata instead of failing the presenter.

**Target-time presentation scheduling** is what separates the `JustInTime`
policies from `PresentWait`: they request an absolute presentation time through
`VkPresentTimingInfoEXT::targetTime` rather than only bounding a wait.

The frame interval the targets are spaced by is the emulator's own, taken from
the frame limiter's `storedFrametimeStep` and passed down through
`beginVulkanLowLatencyFrame()`. It is never derived from the display refresh
rate: the DS frame rate follows the configured TargetFPS, and a 144 Hz monitor
must not change how fast the machine runs. Outside normal speed the interval is
zero, which is what turns scheduling off for fast-forward, slow motion and
unlimited FPS. The flag is
`VK_PRESENT_TIMING_INFO_PRESENT_AT_NEAREST_REFRESH_CYCLE_BIT_EXT`, so choosing
which refresh cycle to land on stays the presentation engine's decision instead
of a hand-written 2/3 cadence in the emulator.

Targets are computed as `baseline stage time + N x frame interval`, where `N`
counts **accepted presents**, not emulated frames. The logical Present ID is the
Reflex frame ID when Reflex is running and therefore skips values whenever a
frame is simulated but not presented; a small fixed ring maps reported IDs back
to presentation sequence numbers. The baseline is rebased onto every new
complete timing report, so rounding error and clock drift cannot accumulate, and
a rejected or queue-full present releases its reserved sequence instead of
leaving a hole in the cadence.

A queue-full present is retried once with the timing chain removed, keeping the
same image, logical ID and reserved sequence — but **not** the same wait
semaphores. `VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT` refuses the presentation
while still enqueueing the semaphore waits the call was given, so re-waiting
them would ask for a signal nothing produces. Ordering holds without them: the
first call's wait is already ahead of the retry on the same present queue.

`VulkanPresentTimingModel` holds this arithmetic
with no Vulkan objects at all and is executed by
`melonprime_vulkan_present_timing_tests` on every Vulkan build.

Both lifecycle queries behind it are dynamic. `vkGetSwapchainTimingPropertiesEXT`
may answer `VK_NOT_READY` before the first present -- a pending state, not a
failure -- and is retried once presents are being accepted. Time-domain
enumeration follows Vulkan's count/array contract: it accepts `VK_SUCCESS`,
retries a bounded `VK_INCOMPLETE` result, and does not treat `VK_NOT_READY` as a
valid pending response. `timingPropertiesCounter` and `timeDomainsCounter` from
each drained results batch are compared against the stored values, so a
refresh-rate change, fullscreen transition, power-state change or VRR/FRR
switch re-queries them. A time-domain change or a report answered in an
unexpected domain drops the baseline rather than projecting a target on a clock
the timestamps did not come from. The target time domain is
`SWAPCHAIN_LOCAL`, falling back to `PRESENT_STAGE_LOCAL`, and the stage is the
most display-visible one the surface reports.

The bounded present wait and target-time scheduling are **independent
capabilities**, resolved together once per frame by `ResolveVulkanPresentPacing()`
in `VulkanPresentPacingPolicy.h` -- a pure constexpr function with no Vulkan
includes, executed by the same test binary as the timing model.
`VK_KHR_present_wait2` waits on the *previous* present; `VK_EXT_present_timing`
schedules *this* one, and its dependencies are `VK_KHR_swapchain`,
`VK_KHR_present_id2`, `VK_KHR_get_surface_capabilities2` and
`VK_KHR_calibrated_timestamps` -- not the wait. A driver exposing only present
timing therefore gets full target-time scheduling with the wait simply skipped,
and a runtime failure that retires the wait does not retire the scheduler.

### Absolute and relative target modes

`VK_EXT_present_timing` offers two ways to ask for a target, and they mean
different things:

| Mode | `targetTime` is | Flag |
|---|---|---|
| Absolute | a point on the presentation timeline | `NEAREST_REFRESH_CYCLE` |
| Relative | how long the **previous** image must stay visible | `PRESENT_AT_RELATIVE_TIME` (+ `NEAREST_REFRESH_CYCLE` when quantized) |

`SelectVulkanTargetSchedulingMode()` picks absolute wherever both the device
feature and the surface capability allow it, and falls back to relative
otherwise. Absolute is preferred because its targets are expressed in the same
units the feedback baseline measures, so every rebase corrects drift exactly;
relative only constrains the gap between two presentations. Falling back to
relative is a supported outcome, not a failure — it reports `Reason=None`, and
only a surface or device with neither mode produces `NoTargetTimingMode*`.

The fallback is not hypothetical: the NVIDIA driver this path was validated
against advertises `presentAtAbsoluteTime` at device level but reports
`presentAtAbsoluteTimeSupported = false` for the surface. Without relative
support the `JustInTime` policies would silently behave exactly like
`PresentWait` there.

A relative duration is quantized to whole refresh intervals on a fixed-refresh
display, because that is the only granularity the engine can act on. The
emulator's rate is rarely an integer divisor of the display's — 60 FPS on 144 Hz
is 2.4 refreshes per frame — so `VulkanRelativeCadence` spreads the remainder
with a fractional accumulator, producing 2,2,3,2,3… rather than rounding every
frame to 2 (17% fast) or 3 (25% slow). The invariant it holds is that
accumulated error stays below one refresh, so the long-run average is the
emulator's own interval.

Three cases are not quantized. Variable refresh (`refreshInterval` is
`UINT64_MAX`) has no grid, so the duration is the frame interval floored at the
display's minimum cycle. An unknown refresh interval (`0`) uses the raw frame
interval rather than inventing a quantum. And a display slower than the emulator
would compute zero quanta, which is the extension's "no target" value, so it is
clamped to one refresh — the most such a display can give.

The cadence is transactional like the presentation sequence: prepared per
present, committed once when the engine accepts it (including a queue-full retry
that dropped its timing metadata, since the frame was still displayed), and
abandoned on rejection. It resets on swapchain recreation, on a mode change, and
whenever the refresh interval or the emulator's frame interval changes — a phase
accumulated against one grid describes nothing on another.

Relative scheduling also waits for a first present: the spec ignores a relative
target on a swapchain that has never presented, so the first accepted present
reports `bootstrap waiting for the first present` and scheduling starts on the
next one.

### When scheduling activates

Scheduling requires all of: a `JustInTime` policy, normal speed, a non-zero
frame interval, present ID 2 surface support, **either** timing mode supported
at both device and surface level, a FIFO-family present mode, ready timing
properties and time domains, a valid target stage, and — for absolute — a
feedback baseline.
Anything missing falls back to `targetTime = 0` and is recorded as a named
reason in the developer log -- never to a renderer failure or a software
fallback. `FIFO_LATEST_READY` is selected only for VSync when that whole
capability and lifecycle path is in place -- with **either** scheduling mode,
since requiring absolute would deny the present mode to exactly the surfaces
relative was added for -- because time-based image selection is the reason the
mode exists; a baseline is deliberately not part of that gate, because one
cannot exist before the first present.

The developer log reports `targetMode=absolute|relative|none` alongside
`targetScheduling`, and the A/B capture records `target_mode` per frame. Without
it a run that silently fell through to no target is indistinguishable from one
that scheduled, and `target_value_ns` cannot be read at all — it is an instant
in absolute mode and a duration in relative mode.

The optional results queue is sized from the swapchain image count (16 to 64
slots), because a report holds its slot until the presentation engine completes
it. Release builds request timestamps for the target stage only; developer
builds request every stage the surface offers, which is why the extra telemetry
costs queue pressure that shipping users do not pay. If the queue fills anyway,
the present is retried without timing metadata, draining continues -- draining
is what frees slots -- and the queue is grown once per full event, at most three
times per swapchain, before timing settles into off. Once metadata is off for
good and the queue has drained empty, the per-frame results query stops too.

A **failed initial queue allocation** is different from a failed growth. Without
a queue no present may carry timing at all, so both the metadata and the results
query stay off and no recovery is armed -- the recovery trigger is a drained
report, which could never arrive. A failed growth leaves the previous queue in
place and only skips the re-enable. Either way the renderer keeps running; only
target-time pacing is lost.

Failure classes are kept apart on the way out, too. `BeginFrame()` returns
`Continue`, `SwapchainOutOfDate` or `DeviceLost` rather than a bool, and the
presenter routes them through `VulkanPacerActionFor()`: an out-of-date swapchain
is rebuilt, while `VK_ERROR_DEVICE_LOST` from `vkWaitForPresent2KHR` goes to the
existing Vulkan runtime-failure path. Rebuilding a swapchain on a lost device
only repeats the failure every frame, which is what a shared bool result caused.

The settings probe evaluates the same optional-feature dependencies as device
creation: the NVIDIA control requires the low-latency extension, timeline
semaphore extension+feature and present-ID extension+feature; the AMD control
requires both `VK_AMD_anti_lag` and `antiLag`. Thus a partial driver exposure is
disabled in the UI instead of failing only when the logical device is created.

Both report requested / supported / enabled / actual / reason and **never fail
renderer creation**:

```
[Vulkan] presenter NVIDIA Reflex (VK_NV_low_latency2): requested=on supported=yes device-extension-enabled=yes actual=active reason=latency markers active; no frame-rate cap requested
[Vulkan] presenter AMD Radeon Anti-Lag 2 (VK_AMD_anti_lag): requested=on supported=no device-extension-enabled=no actual=inactive reason=this GPU does not expose VK_AMD_anti_lag (Radeon Anti-Lag 2 is AMD-only)
[Vulkan] NVIDIA Reflex mode=on lowLatencyMode=true lowLatencyBoost=false
```

Configuration keys are `3D.DX12.NvidiaReflexMode` and
`3D.AMD.AntiLag2Enabled` (shared with DX12 for compatibility), plus the
developer A/B key `3D.Vulkan.PresentPacingPolicy`. All five keep the host frame
limiter as the exact FPS cap:

| Value | Policy | Bounded `PresentWait2` | `targetTime` | Present mode |
|---|---|---|---|---|
| `0` | `TelemetryOnly` (default) | no | 0 | FIFO |
| `1` | `PresentWait` | when supported | 0 | FIFO |
| `2` | `JustInTime` | when supported | absolute | FIFO |
| `3` | `JustInTimeFifoLatestReady` | when supported | absolute | `FIFO_LATEST_READY` |
| `4` | `PresenterOneFrameBudget` | when supported | 0 | selected present mode |

Policy `4` is an explicit latency A/B prototype. It waits for the previous
accepted present within a bounded presenter frame budget before late input and
does not change the global renderer `FramesInFlight` ring or request target-time
metadata. Active Reflex/Anti-Lag remains the sole vendor pacing authority.

"when supported" is per-capability, not per-policy: the `JustInTime` rows keep
their absolute `targetTime` on a surface with no `VK_KHR_present_wait2`.

The default stays telemetry-only until the target-time path has been validated
on real hardware. Feature state and frame
IDs belong to each presenter/emulator instance; no process-global latency or
pacing state is introduced. Anti-Lag 2 is a boolean preference; configurations
written by the early integer-default implementation migrate explicit `0`/`1`
values to `false`/`true` without changing the user's choice.

## Performance telemetry and measured policy

Set `MELONPRIME_PERF=1` before starting the application to enable the existing
`[MelonPrimePerf]` frame report and Vulkan's `[VulkanPerf]` stage report. The
Vulkan report emits 1 Hz p50/p95/p99/max CPU timings for raster-fence wait,
texture-cache update, polygon preparation, descriptor updates, structured-plane
packing, software 2D, Acquire waits, HUD upload and queue submission. Its
counter line records the selected `acquire_timeout_ns`, low-latency Acquire
attempt/skip counts, the validation-only repeated-image-index probe (not queue
ownership), the retained zero `present_image_fence` measurement, geometry
expansion, copy/upload bytes, descriptor writes, texture-staging scratch
fallback and compositor drops. The scale-change log also splits the raster,
compositor-device and compositor-host allocation estimates.
The gate is off by default and the disabled path does no timestamp sampling.

For present-pacing A/B measurement there is a separate build option,
`MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE` (default OFF). It writes one CSV row
per accepted present -- host marker timestamps plus the pacing state the frame
was presented under -- and adds nothing else: stage queries, pacing decisions and
frame content are identical to a build without it. It is deliberately **not**
tied to `MELONPRIME_ENABLE_DEVELOPER_FEATURES`, because developer builds request
every present stage the surface supports and so are not a valid stand-in for a
shipping build's timing. `MELONPRIME_LATENCY_RUN_ID` and `MELONPRIME_LATENCY_CSV`
name the run and its output; `tools/perf/aggregate-vulkan-latency.py` turns the
CSVs into per-run and per-mode percentiles.

The Validation Layer and NVIDIA A/B procedure, including which build may produce
latency numbers and which may not, is
[`docs/development/testing/vulkan-present-pacing-runbook.md`](../../development/testing/vulkan-present-pacing-runbook.md).
Validation needs `CMAKE_BUILD_TYPE=Debug` (preset `debug-mingw-x86_64`, script
`tools/build/windows/build-mingw-validation.bat`); a successful release build
says nothing about whether validation ran.

### Runtime status, 2026-08-13

First Validation Layer session on real hardware: RTX 5070 Ti, driver
610.74.0.0, Vulkan loader 1.4.357, `VK_LAYER_KHRONOS_validation` enabled and
confirmed in the log. Each pacing policy plus Reflex on and off was run with a
ROM loaded. **Core validation errors: 0** in every configuration, after one real
defect this session found and fixed:

> `VUID-VkPresentTimingInfoEXT-timeDomainId-12400` — `timeDomainId` must always
> be an ID returned by `vkGetSwapchainTimeDomainPropertiesEXT`, including on
> presents that request no target time. It was only being set inside the
> target-time branch, so every telemetry present submitted zero. Timing metadata
> is now attached only once the time domains are enumerated, and carries the
> selected ID unconditionally.

This driver exposes every generic present capability at device level -- present
ID 2, present wait 2, calibrated timestamps, present timing,
`presentAtAbsoluteTime`, `presentAtRelativeTime` and FIFO latest-ready -- but
the **surface** reports `presentAtAbsoluteTimeSupported = false`. That is what
relative-time scheduling was implemented for; with it, the same hardware now
reaches:

```text
policy=JustInTime authority=GenericPresentTiming state=TargetSchedulingActive
targetMode=relative absoluteSupported=no relativeSupported=yes fallback=none
```

and `JustInTimeFifoLatestReady` selects `FIFO_LATEST_READY`. Both were
unreachable before. Evidence:
[`docs/archive/audits/vulkan/2026-08-13-relative-scheduling/`](../../archive/audits/vulkan/2026-08-13-relative-scheduling/).

Implemented and active is not the same as beneficial: whether relative
scheduling actually reduces latency still needs the A/B.

Synchronization validation was then enabled as a second pass (layer banner
confirming `Core Checks, Synchronization, Stateless Parameter, Object lifetime,
Thread Safety, Handle Wrapping`) and re-run across policies 0, 2 and 3 plus
Reflex On+Boost: **0 hazards**. With VSync off the swapchain correctly selects
`IMMEDIATE` and target scheduling stays off; with Reflex on the authority is
`NvidiaReflex` and both generic mechanisms are off, which is the runtime proof
of the vendor-authority rule. No device loss, software fallback or swapchain
recreate storm in any run.

Not yet covered, and still NOT RUN: the fullscreen/resize/DPI/minimize event
matrix, F2 and renderer-switch cycling, fast-forward and slow-motion, and all
latency measurement — each needs a person driving the emulator.

The 2026-08-09 F7 gameplay baseline used an RTX 5070 Ti, VSync off, Reflex off,
and the same saved match at 1x/4x/8x/16x. Values below are medians of the emitted
one-second-window percentiles; they are CPU duration, not GPU timestamps:

| Scale | raster wait p95 | `BuildPolygons` p50 | descriptor update p50 | structured 2D p50 | queue submit p50 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1x | 3.0 us | 102 us | 0.6 us | 3.82 ms | 6.8 us |
| 4x | 3.1 us | 133 us | 0.6 us | 3.82 ms | 6.8 us |
| 8x | 3.3 us | 136 us | 0.6 us | 3.83 ms | 6.8 us |
| 16x | 3.8 us | 193 us | 0.6 us | 3.81 ms | 7.6 us |

No sampled window used texture scratch allocations or dropped a compositor
frame. At 16x the presenter copied the expected 96 MiB/frame inside device-local
memory, but acquire/image-fence CPU time and frame delivery stayed stable on this
GPU. These measurements deliberately reject speculative changes: raster working
set duplication/pre-wait reordering, fixed-binding descriptor redesign, staging
growth and direct-image compositor output remain deferred until their respective
wait, write, fallback, drop or GPU-time counters justify the added complexity.
`StoreStructuredEnginePixel()` is inline because it is called 98,304 times per
frame; larger structured-2D changes still require a dedicated A/B capture because
the reported `structured_2d` interval includes the normal software 2D renderer.

## Files

| File | Role |
| --- | --- |
| `src/VulkanCommon.{h,cpp}` | shared types, `VkResult` formatting, debug object naming |
| `src/VulkanLoader.{h,cpp}` | runtime loader; global / instance / device dispatch tables |
| `src/VulkanContext.{h,cpp}` | instance, validation layer, debug utils, physical-device selection |
| `src/VulkanDevice.{h,cpp}` | logical device, queue families, queues, per-device scale ceiling |
| `src/VulkanPresentedFrame.h` | opaque device-local composed-buffer handoff |
| `src/VulkanFeatureProbe.{h,cpp}` | extension / feature / format / limit probing and its log |
| `src/VulkanMemory.{h,cpp}` | allocation, buffers, images, staging, readback |
| `src/VulkanDescriptors.{h,cpp}` | set layouts, pool sizing, updates — the binding contract |
| `src/VulkanSync.{h,cpp}` | frame rings, fences, semaphores, barriers, deferred destruction |
| `src/VulkanPerf.h` | runtime-gated Vulkan CPU timing and traffic counters |
| `src/VulkanNvidiaReflex.{h,cpp}` | `VK_NV_low_latency2` |
| `src/VulkanAmdAntiLag.{h,cpp}` | `VK_AMD_anti_lag` |
| `src/VulkanPresentPacer.{h,cpp}` | vendor-neutral present IDs, timing telemetry, bounded input-adjacent wait and authority selection |
| `src/VulkanModernPresentCompat.h` | Khronos header-359 declarations missing from the minimum supported Vulkan SDK |
| `src/GPU3D_TexcacheVulkan.{h,cpp}` | texture array behind the shared `Texcache<>` template |
| `src/GPU3D_Vulkan.{h,cpp}` | 3D rasterizer and dispatch orchestration; owns the raster resources and compute-pipeline use |
| `src/VulkanGpu2DComposer.{h,cpp}` | structured/native 2D composition, resolution-dependent compositor resources, and composed-output publication |
| `src/RendererOutputRing.{h,cpp}` | backend-neutral leased output-slot publication shared by the native compositors |
| `src/StructuredUploadPlan.h` | backend-neutral dirty-range planning for structured 2D uploads |
| `src/VulkanPipelineCache.{h,cpp}` | `VkPipelineCache` lifetime, device-identity framing, disk load/save, and rejection handling |
| `src/VulkanCaptureBridge.{h,cpp}` | native display-capture sidecar/readback resources and the demand-driven copy bridge |
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
python tools/ci/audits/check-vulkan-shaders.py      # all 117 variants compile + spirv-val
```

`check-vulkan-shaders.py` assembles exactly the sources the generator builds —
same `#define` prologue, same per-variant defines — compiles all 39 pipelines in
all 3 tile-geometry buckets, validates every module with `spirv-val`, additionally
validates the 560 scale-specialized modules for scales 1..16, and prints a
device-limit note for every scale/resource that exceeds Vulkan's *guaranteed*
minimums so the host-side probe requirement stays visible. It skips cleanly when
glslang or spirv-val are not installed.

The present-path shaders have their own generator,
`tools/vulkan/compile-present-shaders.py`, whose output
(`MelonPrimeVulkanPresentShaderBlobs.h`) is likewise committed.

The GPU-independent edge vectors and the opt-in 1x Software/Vulkan native 3D
pixel comparison are documented in
[Raster parity verification](../../development/rendering/raster-parity.md).

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
* **Identical 3D frames reuse `FinalFB`.** Texture-cache mutation is checked just
  as in the OpenGL compute renderer; when neither textures nor 3D state changed,
  raster uploads and dispatches are skipped while the current structured 2D
  compositor still runs.

## Known limitations

* From scale 7 the storage buffers exceed Vulkan's guaranteed 128 MB
  `maxStorageBufferRange`, and from scale 9 Rasterise/DepthBlend use 1024
  invocations per workgroup, above the guaranteed 128. These are arithmetic
  predictions about the *guaranteed minimums*, not observed driver failures; the
  host probe is what decides whether a given device may offer those scales.
* AMD Anti-Lag 2 has never been exercised on AMD hardware. An Intel Iris Plus
  Graphics 655 macOS developer run has exercised this Vulkan backend through
  the bundled MoltenVK loader, including Google display timing JIT and
  resize/minimize/restore cycles; this is one-machine evidence, not a
  cross-GPU parity certification. The archived run is
  [`docs/archive/audits/vulkan/2026-08-14-present-pacer-dispatch/f2-runtime.log`](../../archive/audits/vulkan/2026-08-14-present-pacer-dispatch/f2-runtime.log).
  AMD hardware and physical Linux Vulkan runtime remain unverified. Khronos
  validation is platform-scoped rather than globally unverified: the archived
  macOS Intel/MoltenVK no-bundle Debug F2 run loaded
  `VK_LAYER_KHRONOS_validation` for 60 seconds with VUID count 0 and
  validation ERROR count 0 (the only two validation-channel messages were
  MoltenVK warnings); Windows and Linux Khronos validation remain unverified.
* The modern vendor-neutral WSI extensions compile against both the minimum
  Vulkan header and Vulkan SDK 1.4.357. Surface capability, device enablement,
  and swapchain creation were runtime-validated on an NVIDIA RTX 5070 Ti driver
  exposing the complete present-id2/wait2/timing stack. The Intel/MoltenVK run
  exercised the GOOGLE display-timing path, while physical `VK_EXT_present_timing`
  on that device, physical Linux/Windows/AMD runs remain unverified. Khronos
  validation is verified only for the archived macOS Intel/MoltenVK no-bundle
  Debug F2 run; Windows and Linux validation-layer coverage remain unverified.
  The default policy remains telemetry-only unless an explicitly selected
  target-scheduling policy is active.
