# Vulkan Clean-Room Rewrite — Integration Contract and Delivered Status

Planning/verification branch: `develop_remakeVulkan`
Base commit at Phase 0: `2b90179cca8510c9721e7e8384f3dc550d6f07a6`
Current reference branch: `develop_hud` (HEAD `94f08caf0`, 2026-08-27)

Sections 1-5 are the original **Phase 0 deliverable**: the contract the new
Vulkan backend had to satisfy, derived **only from non-Vulkan callers** — the
`Renderer` / `Renderer3D` core interfaces, the Qt frontend renderer selection,
the screen panel base class, the build system and CI. The bodies of the outgoing
Vulkan files were not used to derive anything there.

Section 6 records what was actually **delivered**, and section 7 is the honest
status table: what has been observed working, how it was observed, and what has
not been observed at all. The backend's user-facing documentation is
[`docs/features/rendering/vulkan-backend.md`](../../../features/rendering/vulkan-backend.md).

The implementation and verification claims below are retained for their named
phase snapshots. New implementation or verification should use the current
reference branch; the dated claims are not a current-HEAD acceptance statement.

## 1. Permitted reference sources

| Source | Used for |
|---|---|
| `src/GPU_Soft.cpp`, `src/GPU2D_Soft.cpp`, `src/GPU3D_Soft.cpp` | Ground truth for DS pixel output |
| `src/GPU3D_Compute.cpp`, `src/GPU3D_Compute_shaders.h` | Compute rasterizer algorithm, fixed-point math, binning/sorting, span setup |
| Non-Vulkan `melonPrimeDS` code (core interfaces, Qt frontend, config, HUD, OSD, layout, CMake, CI) | Product requirements and integration seams |
| DX12 backend (`src/GPU3D_DX12.*`, `src/GPU_DX12.*`, `src/DX12*`, `MelonPrimeDX12*`) | **Functional** Definition of Done, frontend integration shape, renderer lifecycle — *not* API design |
| Khronos Vulkan Specification / Guide / Vulkan-Headers / Validation Layers / SPIR-V registry | All Vulkan API design decisions |

Forbidden as reference (code, design, shaders, history, comments, diffs):
WatermelonDS in any form; `SapphireRhodonite/melonDS-android`;
`SapphireRhodonite/melonDS-android-lib`; the local clones of either; and the
**outgoing Vulkan implementation on this branch**.

The outgoing Vulkan files may only be consulted for: file names, CMake
registration lines, renderer-enum wiring, and the public entry points listed in
this document. Section 3 below records exactly that, so that after Phase 1 the
old bodies never need to be opened again.

## 2. Why the rewrite (recorded rationale)

The outgoing implementation (~80k lines including generated SPIR-V headers) is a
port of a third-party Android Vulkan backend built on a *deferred 3D + latch +
snapshot* architecture. Its remaining defects are architectural, not local: the
DS `compMode` control word is fixed to a 2D **engine**, while Metroid Prime
Hunters flips `POWCNT1` bit 15 every frame, so the engine→LCD assignment
alternates. The software renderer is immune (it only has to get the final pixel
right), but a deferred 3D compositor reads `compMode` per LCD and therefore
blends 3D on one phase and not the other, producing the documented ~10x
inter-frame flicker on F4/F5 scenes.

The DX12 backend in this repository solves the same problem with a different
architecture — *immediate* high-resolution 3D plus structured software 2D
planes composed in the same frame — and does not exhibit the defect. The new
Vulkan backend adopts that **architecture** (which originates from
`GPU3D_Compute`, a permitted source) with Vulkan-native API design.

## 3. Required integration contract

### 3.1 Core renderer objects

Two classes, mirroring the DX12 pairing:

```
melonDS::VulkanRenderer3D : public melonDS::Renderer3D   // src/GPU3D_Vulkan.{h,cpp}
melonDS::VulkanRenderer   : public melonDS::SoftRenderer // src/GPU_Vulkan.{h,cpp}
```

`Renderer3D` members that must be implemented (`src/GPU3D.h`):

- `bool Init()`
- `void Reset()`
- `void RenderFrame()`
- `u32* GetLine(int line)`
- `bool UsesStructured2DMetadata() const noexcept` → **must return `true`**
- `bool NeedsShaderCompile()` / `void ShaderCompileStep(int& current, int& count)`

`SoftRenderer` members that must be overridden (`src/GPU_Soft.h`). Note the
constraint already documented for DX12: `SoftRenderer`'s implementations
`dynamic_cast` `Rend3D` to `SoftRenderer3D` and dereference unchecked, so **all**
of these must be overridden:

- `bool Init()`, `void Stop()`
- `void PreSavestate()`, `void PostSavestate()`
- `void SetRenderSettings(RendererSettings&)`
- `void Start3DRendering()`
- `void VBlank()`
- `RendererOutput GetOutput()`
- `bool NeedsShaderCompile()`, `void ShaderCompileStep(int&, int&)`

Plus the MelonPrime additions the frontend calls:

- `bool HasRuntimeFailure() const noexcept`
- `const std::string& GetRuntimeFailureReason() const noexcept`
- Low-latency frame hooks (see §3.5)

`VulkanRenderer3D::New(melonDS::GPU3D&)` returns `nullptr` when Vulkan is
unavailable or device setup failed, so `VulkanRenderer::Init()` can report a
truthful failure instead of constructing a dead renderer.

### 3.2 Structured 2D producer (already core-owned — do not delete)

`src/GPU_Soft.h` gates a structured-2D producer on:

```c
#if defined(MELONPRIME_DS) \
    && (defined(MELONPRIME_ENABLE_VULKAN) \
        || (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
#define MELONPRIME_HAS_STRUCTURED_SOFT_2D 1
#endif
```

This producer is **shared with DX12** and is a repo-owned contract
(`src/MelonPrimeStructuredComposition.h`, namespace `StructuredComposition`). It
is *not* part of the outgoing Vulkan implementation and must survive Phase 1
unchanged. It is activated by `Rend3D->UsesStructured2DMetadata()`.

Consumer view published by the core:

```cpp
struct SoftRenderer::StructuredVulkanFrameView
{
    const u32* Plane[2][3]{};   // [screen][plane]  256*192 each
    const u32* LineMeta[2]{};   // [screen]         192 each
    bool NativeMenuHeld = false;
    bool Valid = false;
    u64  Generation = 0;
};
bool GetStructuredVulkanFrame(StructuredVulkanFrameView&) const noexcept;
```

The new Vulkan compositor consumes exactly this, matching the DX12 entry point
shape:

```cpp
bool ComposeStructuredOutput(const std::array<const u32*, 6>& planes,
                             const std::array<const u32*, 2>& lineMeta,
                             u64 generation);
const u32* GetComposedScreen(u32 screen) const noexcept;
u32 GetComposedWidth() const noexcept;
u32 GetComposedHeight() const noexcept;
```

`GetComposedWidth/Height` return the **internal-resolution** size, not 256x192.
That is the mechanism by which high resolution survives to present.

### 3.3 Frontend renderer selection

- `renderer3D_Vulkan` already exists in `src/frontend/qt_sdl/EmuInstance.h`
  alongside `renderer3D_Software / OpenGL / OpenGLCompute / Metal /
  MetalCompute / DX12`. Value and ordering are config-compatible and must not
  change.
- `EmuThread::updateRenderer()` (`src/frontend/qt_sdl/EmuThread.cpp:1530`)
  constructs `std::make_unique<VulkanRenderer>(*nds)`, then verifies via
  `dynamic_cast` and, on failure, logs
  `Renderer fallback requested=Vulkan actual=Software stage=3D-renderer-init`
  and reports through `MelonPrime::VulkanFeatureCheck::ReportRuntimeFailure`.
  This "no silent fallback" shape is required and already present.
- `EmuThread::handleVulkanRuntimeFailure()` mirrors the DX12 equivalent at
  `EmuThread.cpp:1616` for mid-session device failures.
- `MelonPrime::VideoBackend::PresentationBackend::Vulkan`
  (`MelonPrimeVideoBackend.h`) selects the panel; `ResolvePresentationBackend()`
  is the only place that decides.
- `RendererSettings` carries `ScaleFactor`, `BetterPolygons`,
  `HiresCoordinates`, `NvidiaReflexMode`, `AmdAntiLag2Enabled`.
  `VulkanRenderer` forwards scale and high-resolution coordinates, but
  deliberately does not forward `BetterPolygons`: the Vulkan compute path
  rasterizes original polygon spans and never performs the triangle split that
  option exists to correct. Video Settings mirrors that contract.

### 3.4 Presentation panel

`ScreenPanelVulkan : ScreenPanel` (`src/frontend/qt_sdl/Screen.h:437`) is the
only Qt seam. Required overrides, all already declared by the non-Vulkan base
class or by the DX12 sibling:

- `bool initVulkan()`, `void drawScreen()`
- `void paintEvent()`, `void resizeEvent()`, `bool event()`
- `void setupScreenLayout()`
- `void beginModalPausePresentation()` / `endModalPausePresentation()`
- `void setHudEditModeActive(bool)` under `MELONPRIME_CUSTOM_HUD`
- Wayland pointer-lock overrides under
  `__linux__ && MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK`
- `static void PrepareForInstanceRendererTransition(EmuInstance*)`

Final presentation must be `VkSwapchainKHR` + `vkQueuePresentKHR`. CPU readback
into `QImage` + `QPainter` is forbidden on the final path.

### 3.5 Low-latency hooks

`ScreenPanel` declares, under `MELONPRIME_ENABLE_VULKAN`, five virtuals that the
emulation thread calls:

```cpp
virtual void beginVulkanLowLatencyFrame(int reflexMode, bool antiLag2Enabled);
virtual void markVulkanReflexInputSample();
virtual void markVulkanReflexRenderSubmitStart();
virtual void markVulkanReflexRenderSubmitEnd();
virtual void finishVulkanLowLatencyFrame();
```

Backed by `VK_NV_low_latency2` and `VK_AMD_anti_lag`, per Khronos/vendor
specifications only. Markers must correspond to real `vkQueueSubmit` /
`vkQueuePresentKHR` boundaries, not to Qt paint events. Unsupported hardware
disables the feature and logs requested/supported/enabled/actual/reason; it must
never fail renderer creation.

### 3.6 Build and CI registration

- `src/frontend/qt_sdl/CMakeLists.txt` holds the `MELONPRIME_ENABLE_VULKAN`
  option and source registration.
- Public defines on the core target: `MELONPRIME_ENABLE_VULKAN=1`,
  `VK_NO_PROTOTYPES=1`, and on Windows `VK_USE_PLATFORM_WIN32_KHR=1`.
- No static link against the Vulkan loader — entry points resolve at runtime.
- `.github/workflows/build-windows.yml` and `build-ubuntu.yml` already reference
  `MELONPRIME_ENABLE_VULKAN`; the matrix must keep Vulkan ON and OFF green, and
  Vulkan must coexist with DX12 on Windows.

### 3.7 Core-side guarded hunks

Vulkan-guarded regions currently exist in these non-Vulkan-named files:

| File | Disposition |
|---|---|
| `src/GPU.h` (2), `src/GPU_Soft.h` (1) | **Keep** — shared with DX12 via `MELONPRIME_HAS_STRUCTURED_SOFT_2D` |
| `src/GPU3D.h` (1) | **Keep** — `UsesStructured2DMetadata()` gate, shared with DX12 |
| `src/GPU2D_Soft.cpp` (7), `src/GPU2D_Soft.h` (1) | **Review** — structured producer hunks shared with DX12 stay; Sapphire-generation extras go |
| `src/GPU.cpp` (1) | **Review** — legacy reference-timeline hook, expected to be removable |
| `src/GPU3D_Texcache.h` (3) | **Remove** — Sapphire-generation texcache additions; the new backend uses the generic texcache like DX12 does |
| `src/GPU3D_AcceleratedFrontend.{h,cpp}` | **Delete** — Vulkan-only, Sapphire-derived (1028 lines) |

## 4. Target file layout

Responsibilities are split; no single giant translation unit.

```
src/VulkanCommon.h              shared types, result checking, debug naming
src/VulkanLoader.{h,cpp}        runtime loader + per-instance/device dispatch
src/VulkanContext.{h,cpp}       instance, validation, debug utils, physical device
src/VulkanDevice.{h,cpp}        logical device, queue families, queues
src/VulkanFeatureProbe.{h,cpp}  extension/feature/format/limit probing + logging
src/VulkanMemory.{h,cpp}        allocation, buffers, images, staging, readback
src/VulkanDescriptors.{h,cpp}   set layouts, pools, updates
src/VulkanShader.{h,cpp}        SPIR-V modules, compute pipelines, pipeline cache
src/VulkanSync.{h,cpp}          frame contexts, fences, semaphores, barriers,
                                deferred destruction queue
src/GPU3D_TexcacheVulkan.{h,cpp}
src/GPU3D_Vulkan.{h,cpp}        compute rasterizer (port of GPU3D_Compute)
src/GPU3D_Vulkan_shaders/*.comp GLSL sources
src/GPU_Vulkan.{h,cpp}          SoftRenderer2D + VulkanRenderer3D glue
src/VulkanNvidiaReflex.{h,cpp}  VK_NV_low_latency2
src/VulkanAmdAntiLag.{h,cpp}    VK_AMD_anti_lag
src/frontend/qt_sdl/MelonPrimeVulkanFeatureCheck.{h,cpp}
src/frontend/qt_sdl/MelonPrimeVulkanSurface*.{h,cpp,mm}   WSI per platform
src/frontend/qt_sdl/MelonPrimeVulkanPresenter.{h,cpp}     swapchain + present
src/frontend/qt_sdl/MelonPrimeVulkanCompositor.{h,cpp}    HUD/OSD/layout compose
tools/ci/audits/check-vulkan-shaders.py                   glslang + spirv-val
```

## 5. Compute rasterizer stage mapping

Stages are taken from `src/GPU3D_Compute.cpp` / `GPU3D_Compute_shaders.h` and
must match one-for-one:

| Stage | Variants |
|---|---|
| ClearCoarseBinMask | 1 |
| ClearIndirectWorkCount | 1 |
| CalculateWorkOffsets | 1 |
| SortWork | 1 |
| BinCombined | 1 |
| InterpSpans | 2 (Z-buffer, W-buffer) |
| Rasterise | 8 kinds x 2 depth modes = 16 |
| DepthBlend | 2 (Z, W) |
| FinalPass | 8 (edge marking / fog / AA combinations) |

**33 rasterizer pipelines**, matching `ComputeRenderer3D::ShaderCompileStep()`'s
`count = 33` exactly. The GL ordering is preserved (InterpSpans 0-1,
BinCombined 2, DepthBlend 3-4, Rasterise 5-20, Clear/Calc/Sort 21-24,
FinalPass 25-32) so indices stay directly comparable when debugging the Vulkan
backend against the OpenGL compute renderer.

| Presentation stage (phase 8-9) | Index | Variants |
|---|---|---|
| Resolve | 33 | 1 |
| Compositor | 34 | 1 |

**35 pipelines total.** The two presentation stages are appended rather than
interleaved so indices 0-32 keep matching the OpenGL renderer; the DX12 backend
numbers its own steps the same way (`ShaderStep_Resolve` /
`ShaderStep_Compositor` after `ShaderStep_FinalPass0 + 8`).

The eight rasterise kinds are `NoTexture`, `NoTextureToon`,
`NoTextureHighlight`, `UseTextureDecal`, `UseTextureModulate`, `UseTextureToon`,
`UseTextureHighlight`, `ShadowMask`.

**Correction (Phase 4 finding, resolved in phase 8-9).** An earlier revision of
this table listed `Resolve` and `Compositor` as stages 34 and 35. That was wrong:
neither exists in `GPU3D_Compute`. They are *presentation* stages the DX12 backend
added on top of the compute rasterizer — a native-resolution resolve for display
capture, and the structured-2D/high-resolution-3D compositor. Phase 8-9 shipped
them as pipelines 33 and 34, designed from the DS display semantics
(`GPU2D_Soft::ColorComposite`, `GPU_ColorOp.h`, `SoftRenderer::ExpandColor` /
`ApplyMasterBrightness`) and the structured composition contract (§3.2), with the
DX12 shaders used only to cross-check functional scope.

### 5.1 Scale handling (Phase 4 decision, implemented)

`TileSize`, `CoarseTileCountY`, `CoarseTileArea` and
`ClearCoarseBinMaskLocalSize` all derive from `range = (scale>=5) + (scale>=9)`,
so internal resolutions 1x-16x collapse into **three compiled tile-geometry
buckets**: range 0 (1-4), range 1 (5-8), range 2 (9-16). Those four values are
injected as `-D` defines at SPIR-V generation time. `ScreenWidth`, `ScreenHeight`
and `MaxWorkTiles` are **specialization constants** (ids 0/1/2); every derived
value folds through `OpSpecConstantOp` at pipeline creation, so there is no
runtime cost relative to the GL renderer's baked literals.

Total shipped: 35 pipelines x 3 buckets = **105 SPIR-V modules**, covering all 16
scales. SPIR-V is committed, so the build has no glslang dependency; the `.comp`
and `.glsl` files are inputs to the offline generator only and must not be added
to any source list.

### 5.2 Inherited defects, transcribed deliberately

Two problems exist in the OpenGL compute renderer and were transcribed verbatim
rather than "fixed", because diverging would break the 1:1 correspondence that
makes the port verifiable. Both are flagged in-source and both need a decision
before the high scales are offered:

1. **`BinCombined` shift-width UB at scales 9-16.** The workgroup is sized by
   `CoarseTileArea` (48 at range 2) but the coarse pass treats invocations as a
   32-lane ballot, so lanes 32-47 evaluate `1U << localIdx`, which is undefined
   in GLSL for shifts >= 32. Pre-existing upstream issue affecting both renderers.
2. **Scale-16 `InterpSpans` worst-case dispatch is 65536 groups**, one over
   Vulkan's guaranteed `maxComputeWorkGroupCount` of 65535. Reachable only when
   the span buffer is fully consumed.

Additionally, from scale 7 storage buffers exceed 128 MB and from scale 9
Rasterise/DepthBlend use 1024 invocations per workgroup — above Vulkan's
*guaranteed* minimum of 128. These are arithmetic predictions, not observed
driver failures. The host **must** query `VkPhysicalDeviceLimits` and refuse the
scales the device cannot support, rather than dispatching and hoping.

## 5.3 Phase 5-7 deviations from this contract (accepted, with reasons)

**Frames in flight is 1 for the rasterizer, not 2.** `XSpanSetups`, the three tile
buffers, `BinResult`, `WorkDescs`, `ResultBuffer` and `FinalFB` form a single
shared working set. A second in-flight frame would be a WAR/WAW race on all of
them, and duplicating the set costs roughly 1 GB at 16x. The frame fence is
waited at the *start* of frame N for frame N-1, with a whole DS frame of
software-2D work in between, so CPU/GPU overlap is preserved. This is the same
shape DX12 uses (`FrameInFlight`). The *presenter* (Phase 10) is a separate ring
and may still use 2.

**`GetLine()` downscale used `vkCmdBlitImage` in phase 5-7; phase 8-9 replaced
it.** The interim path filtered a 2x2 neighbourhood and filtered colour
independently of alpha, so from 4x upward it aliased where DX12 averaged.
`Resolve.comp` (pipeline 33) now does the alpha-weighted box filter DX12 does,
writing packed r6g6b6a5 straight into a device-local buffer that is copied to the
readback allocation; `NativeResolveImage` and the format's linear-filter query
are gone. `EnsureFrameReadback()` is a plain `memcpy` as a result — the
UNORM8 -> 6-bit reconstruction happens on the GPU.

**Capture textures are inactive by design.** As with DX12, this backend pairs
with the software 2D renderer, which writes captures into real VRAM. So
`pc.TexIsCapture` is always 0 and set 1 bindings 1/2 bind a cleared 1x1x1
placeholder array. The bindings and the `CaptureYOffset` push constant stay
plumbed in case GPU-side captures are ever reintroduced.

## 5.4 Phase 8-9 changes to the descriptor contract (deliberate, stated loudly)

The set 0 binding contract in §5 / `src/VulkanDescriptors.h` grew by **two
bindings, appended at the end. No existing binding number moved.**

| Set 0 binding | Type | Used by |
|---|---|---|
| 12 `StructuredInput` | `STORAGE_BUFFER`, readonly | Compositor |
| 13 `PresentationOut` | `STORAGE_BUFFER` | Resolve, Compositor |

`PresentationOut` is one binding rather than two because the two stages never run
in the same dispatch: the host binds the native-resolution capture buffer for
Resolve and the two-screen composed buffer for Compositor. That is what the
second set-0 allocation is for — `DescriptorPoolSizing::RasterizerSetsPerFrame`
went from 1 to 2, slot 0 being the rasterizer's and slot 1 the compositor's.
Set 1 is unchanged (five bindings) and is not bound at all for the compositor,
which declares no set-1 resource.

The GLSL side lives in `GPU3D_Vulkan_shaders/PresentationBuffers.glsl`, and
`tools/vulkan/vulkan_shader_set.py::SET0_BINDINGS` carries the same two entries,
so the generated header, the manifest and the feature probe's descriptor-limit
demand all follow from one edit.

**Composition path.** `VulkanRenderer::VBlank()` pulls
`SoftRenderer::GetStructuredVulkanFrame()` and calls
`VulkanRenderer3D::ComposeStructuredOutput()`, which stages the six planes plus
two line-metadata arrays, dispatches `Compositor` over both screens at the
internal resolution and publishes the device-local result as a leased
three-slot ring buffer. `AcquireOutputLease()` returns that buffer at
`GetComposedWidth()/GetComposedHeight()` — the internal size — so neither a CPU
nor a native-resolution intermediate exists on the display path.

The compositor records into **its own three-slot `Vk::FrameRing`** rather than
the rasterizer's. Every slot owns its structured staging/input and composed
output buffers, so `ComposeStructuredOutput()` submits and returns without a
CPU fence wait. Both rings submit to the same shared-device queue: explicit
barriers order `FinalFB` raster writes, compositor reads and the next raster
writes. `RendererOutputLease` prevents a slot from being overwritten until the
presenter's copy fence retires. No `vkDeviceWaitIdle` / `vkQueueWaitIdle`
appears in the steady-state path.

## 6. Delivered layout and deviations from §4

The shipped file set matches §4 with these differences, all deliberate:

| §4 entry | Delivered as |
|---|---|
| `src/VulkanShader.{h,cpp}` | **Not a separate translation unit.** Shader modules, the compute pipelines and the `VkPipelineCache` live in `src/GPU3D_Vulkan.{h,cpp}` next to the dispatch orchestration that owns them; the SPIR-V blobs and the module table are generated into `src/GPU3D_Vulkan_shaders/generated/`. |
| `src/frontend/qt_sdl/MelonPrimeVulkanCompositor.{h,cpp}` | **Not a separate translation unit.** HUD/OSD/layout composition stayed in `ScreenPanelVulkan` (`MelonPrimeScreenVulkan.cpp`) so it reuses the same QImage helpers as the software and DX12 panels; the *structured 2D* compositor is a compute pipeline inside `GPU3D_Vulkan.cpp`. |
| `MelonPrimeVulkanSurface*` | Delivered as `Win32`, `Linux`, `MacOS.mm`, `Stub` plus a shared `Common.cpp` for `Destroy()` and the non-macOS `RunFrameInPlatformScope()`. |
| — | Added: `src/frontend/qt_sdl/MelonPrimeVulkanPresentShaders/` (committed present vertex/fragment SPIR-V, generated by `tools/vulkan/compile-present-shaders.py`). |
| — | Added: `src/VulkanPresentedFrame.h`, the opaque device-local renderer → presenter handoff. |
| — | Added (developer builds only): `src/frontend/qt_sdl/MelonPrimeRendererSwitchStress.{h,cpp}`, the `MELONPRIME_RENDERER_SWITCH_STRESS` driver. |

One defect found during the phase 15-18 sweep and fixed there:
`<windows.h>` macro-expands `CreateSemaphore`, so `Vk::DeviceDispatch` had two
definitions in one binary depending on whether a translation unit pulled in
`windows.h` before `VulkanLoader.h`. GCC's LTO `-Wodr` reported it on the
Vulkan-ON / DX12-OFF configuration. `src/VulkanLoader.h` now `#undef`s the macro
before the struct.

## 7. Status

Verification claims below are per-item and per-platform. "Observed" means the
described artefact was produced and read in this environment on the dates of the
phase 15-18 sweep; everything else is **UNVERIFIED**, which is not a synonym for
"probably fine".

Hardware used for every runtime observation: one Windows 11 machine, NVIDIA
GeForce RTX 5070 Ti, driver 610.74.0.0, Vulkan 1.4.341, MinGW-w64 GCC 14.2,
Qt 6.11.

### 7.1 Delivered and verified

| Item | How it was verified |
|---|---|
| Vulkan initializes for real, or fails loudly with requested/actual/reason | Startup log: full feature-probe table, `selected NVIDIA GeForce RTX 5070 Ti ... score 1175, up to 16x`, `renderer requested=Vulkan presenter actual=Vulkan` |
| 35 pipelines x 3 tile-geometry buckets compile offline; SPIR-V validates | `tools/ci/audits/check-vulkan-shaders.py`: 105 variants compiled, 105 modules `spirv-val`-clean, 560 scale-specialized modules validated for scales 1..16 |
| Committed SPIR-V is in step with the GLSL | `tools/vulkan/compile-shaders.py --check`: "all 15 committed generated files are up to date"; `compile-present-shaders.py` regenerates byte-identical output |
| Internal resolution 1x/2x/4x/8x/16x survives to present | Runtime log per scale: `1x -> 256x192 bucket 0`, `2x -> 512x384 bucket 0`, `4x -> 1024x768 bucket 0`, `8x -> 2048x1536 bucket 1`, `16x -> 4096x3072 bucket 2`, all with `capture resolve 256x192` |
| Bucket selection matches `range = (scale>=5) + (scale>=9)` | Same logs; buckets 0/0/0/1/2 for the five scales above |
| Device scale ceiling is probed and respected | `Internal resolution -- up to 16x supported` from the probe; 16x accepted and rendered |
| Native `VkSwapchainKHR` + `vkQueuePresentKHR`, present mode queried | 2026-08-09 post-sync-design run: `available-present-modes=FIFO,FIFO_RELAXED,MAILBOX,IMMEDIATE,UNKNOWN(1000361000) selected-present-mode=IMMEDIATE swapchain-images=3 ... window-mode=windowed`; the first-present line also reported `IMMEDIATE` |
| VSync on selects FIFO, VSync off prefers IMMEDIATE then MAILBOX | Separate startup runs on the same surface selected `FIFO` for `requested-vsync=on` and `IMMEDIATE` for `requested-vsync=off`; both reasons and the full available-mode list were logged |
| VSync toggles at runtime and the present mode really changes | Windows UI Automation toggled the live Video-settings checkbox Off → On → Off; the log showed `IMMEDIATE → FIFO → IMMEDIATE`, while VSync Interval remained disabled after both transitions |
| Validation layer clean | A dedicated build with `MELONDS_VULKAN_ENABLE_VALIDATION` (see §7.5) ran scales 1/4/16, VSync on, the 8-direction switch sweep and the window stress. `[Vulkan] validation layer enabled` in every run; **zero** `[Vulkan/validation]` and **zero** `[Vulkan/performance]` messages. The 2026-08-09 post-sync-design F7 run on the new IMMEDIATE path also reported zero validation/performance messages, runtime failures, fallbacks and device losses. The only earlier debug-utils output was 28 `[Vulkan/general]` Vulkan *loader* notices about a duplicate OBS Studio capture layer installed on this machine — which also serves as the positive control that the messenger really was delivering messages to the log |
| Window lifecycle: resize storm, minimize/restore, fullscreen, focus loss | One session driven through 120 `SetWindowPos` calls at 15 ms, 4 minimize/restore cycles, 3 fullscreen toggles and 3 focus-loss cycles: 132 swapchain recreations including `extent=2560x1440`, zero errors, zero `SURFACE_LOST`/`OUT_OF_DATE` escalations, zero renderer fallbacks |
| Renderer switching, Vulkan↔Software round trips | `MELONPRIME_RENDERER_SWITCH_STRESS=3,0`, 40/40 switches, 21 Vulkan 3D-renderer initializations, 63 swapchain creations, no fallback or runtime failure. The post-sync-design F7 match run additionally completed 16/16 Vulkan/DX12/Software transitions (`3,4,3,0` x4), with zero fallback, runtime failure or stderr output |
| All eight renderer-switch directions | `MELONPRIME_RENDERER_SWITCH_STRESS=3,0,3,1,3,2,3,4`, 32/32 switches (Vulkan↔Software, ↔OpenGL, ↔OpenGL Compute, ↔DX12), no fallback or runtime failure |
| NVIDIA Reflex active on supported hardware | Separate Off/On/On+Boost F7 runs reported the matching requested/actual state; driver calls logged `off/false/false`, `on/true/false`, and `on+boost/true/true`, always with no application frame-rate cap requested |
| Anti-Lag 2 disables cleanly on non-AMD hardware, without failing the renderer | `AMD Radeon Anti-Lag 2 (VK_AMD_anti_lag): requested=on supported=no device-extension-enabled=no actual=inactive reason=this GPU does not expose VK_AMD_anti_lag` while Vulkan kept rendering |
| Legacy Anti-Lag integer preferences migrate without changing meaning | Isolated configurations containing `AntiLag2Enabled = 0` and `= 1` started as requested Off and On respectively, then saved canonical boolean `false` and `true` values |
| Windows build, Vulkan ON + DX12 ON | `tools/build/windows/build-mingw.bat` — succeeded |
| Windows build, Vulkan OFF (`MELONPRIME_FORCE_DISABLE_VULKAN=ON`) | Configured and built into its own tree — succeeded |
| Structured-2D producer survives with Vulkan OFF | `nm` on `GPU_Soft.cpp.obj` in the Vulkan-OFF tree shows `SoftRenderer::GetStructuredVulkanFrame`; the build defines `MELONPRIME_ENABLE_DX12=1` and not `MELONPRIME_ENABLE_VULKAN` |
| Windows build, Vulkan ON + DX12 OFF (`MELONPRIME_FORCE_DISABLE_DX12=ON`) | Configured and built into its own tree — succeeded (this is the configuration that surfaced the `CreateSemaphore` ODR violation) |
| Windows build, developer features ON | The default `build-mingw.bat` path configures `MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON`; the switch-stress driver used above only exists in that build |
| Repository audits | See §7.4 |

### 7.2 Verified only statically

| Item | What was actually done |
|---|---|
| Linux WSI adapter (`MelonPrimeVulkanSurfaceLinux.cpp`) | No Linux toolchain exists on this machine (no WSL distribution, no cross-compiler), so a Linux build was impossible. Instead the file was compiled with the real MinGW/Qt6 toolchain and `-fsyntax-only` after rewriting **only** the platform gate `defined(__linux__)` → `1`, with the private QPA include path that `Qt6::GuiPrivate` supplies on Linux. Result: clean. This proves includes resolve, types/fields/signatures are real and the Qt/QPA calls type-check. It does **not** prove it links or behaves on Linux. |
| `ScreenPanelVulkan`'s `__linux__` branches | Same method, on `MelonPrimeScreenVulkan.cpp` plus a shadowed `Screen.h`: once with the Wayland pointer-lock blocks off and once with `MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK` forced on (its header is a pure pimpl and needs no libwayland headers). Both clean. |
| macOS WSI adapter (`MelonPrimeVulkanSurfaceMacOS.mm`) | Manual review only — there is no Objective-C++/AppKit toolchain here. Checked: the `#if` gate matches the CMake `APPLE` branch; ARC is applied via `set_source_files_properties(... -fobjc-arc)`; QuartzCore/AppKit/Metal are linked; `VkMetalSurfaceCreateInfoEXT`'s locally declared layout matches the registry and `VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT` is declared unconditionally in `vulkan_core.h`; `Destroy()` and the non-macOS `RunFrameInPlatformScope()` live in `MelonPrimeVulkanSurfaceCommon.cpp` under `#if !defined(__APPLE__)`, so there is no duplicate definition. |
| Scale refusal above the device ceiling | Code path read and confirmed to be a refusal (`SetRuntimeFailure` → emulation-thread fallback with the stored reason), not a clamp. It could not be triggered: this GPU's probed ceiling is 16x, which equals the configuration maximum. |

### 7.3 UNVERIFIED

Nothing below has been observed working. None of it may be described as verified.

- **AMD Anti-Lag 2 on AMD hardware.** Only the *unsupported* path has run. The
  `VK_AMD_anti_lag` enable, the per-frame INPUT/PRESENT updates and the frame-ID
  pairing have never executed.
- **Any non-NVIDIA GPU.** No AMD, Intel, Qualcomm or software (lavapipe) device
  has run this backend. Adapter selection scoring, memory-type selection, format
  support fallbacks and the whole feature-probe refusal machinery have only ever
  seen one device.
- **Linux, at runtime.** No Linux build has been produced or run. XCB, Xlib and
  Wayland surface creation, the XWayland/Wayland surface-rebinding path
  (`prepareLinuxPresentationSurface`), and the Wayland pointer-lock overrides in
  the Vulkan panel are all unexecuted.
- **Linux, as a link.** The static checks above are `-fsyntax-only`; no Linux
  link has ever happened, so undefined symbols and Qt private-header packaging
  differences across distributions remain unknown.
- **macOS entirely.** No macOS build, no MoltenVK, no `VK_EXT_metal_surface`, no
  `CAMetalLayer` path, no `@autoreleasepool` frame scope. Static review only.
  One review finding is recorded but **not** acted on: Apple documents that a
  custom layer must be assigned to `NSView.layer` *before* `wantsLayer = YES`
  for a layer-hosting view, and the adapter sets `wantsLayer` first. Whether
  that matters in practice is untested, and changing it blind would be an
  unverifiable behaviour change.
- **BSD.** The stub adapter's loud-failure path has never run.
- **Validation-layer coverage below WARNING severity.** The debug messenger
  subscribes to `WARNING | ERROR` only; `VERBOSE` and `INFO` are deliberately
  omitted because the layer emits thousands of INFO messages per frame for this
  workload. "Zero validation messages" therefore means zero warnings and zero
  errors, not zero messages of any severity. Synchronization validation, GPU-
  assisted validation and best-practices validation were not separately enabled.
- **Validation on any scale other than 1x, 4x and 16x**, and on any code path
  not exercised by the six validation runs.
- **Pixel-exact parity with the software renderer.** No golden-image or
  per-frame hash comparison was run in this phase. Earlier phases compared
  specific scenes by eye and by frame capture; that is not a pixel-exactness
  claim, and no automated parity harness exists for the Vulkan path.
- **Long-run and leak behaviour.** The longest observed session is on the order
  of a minute. No multi-hour run, no VRAM/host-memory growth measurement, no
  descriptor-pool or deferred-destruction-queue growth measurement.
- **Frame-rate and latency numbers.** The 60/60 fps and Reflex figures recorded
  in earlier phases were measured on this one machine and are not a performance
  claim for any other configuration. No measurement in this phase used
  `tools/perf/`.
- **Multi-instance behaviour.** Only `Instance0` was ever active.
- **The two inherited OpenGL-compute defects** (`BinCombined` shift-width UB at
  scales 9-16; scale-16 `InterpSpans` 65536-group dispatch). Scale 16 rendered
  without an observed failure on this driver, which is not the same as the
  defects being absent or harmless elsewhere.
- **Savestates, display capture across renderer switches, and the Custom HUD
  editor** under Vulkan were not exercised in this phase.

### 7.4 Audit results (phase 15-18 sweep)

All pass on the phase 15-18 verification tree:

| Audit | Result |
|---|---|
| `audit-config-defaults.ps1` | PASS |
| `audit-hud-key-parity.ps1 -Strict` | PASS |
| `check-inc-ownership.ps1` | PASS (90 `.inc` files) |
| `audit-metroid-literal-budget.ps1 -Budget 1` | PASS (1/1) |
| `audit-platform-scatter-budget.ps1 -Budget 22` | PASS |
| `audit-color-dialog-prefs.ps1` | PASS |
| `audit-melonprime-srp-performance.ps1` | PASS |
| `audit-melonprime-thread-boundary.ps1 -Strict` | PASS (0 findings) |
| `audit-melonprime-instance-state.ps1 -Strict` | PASS (22 pre-existing findings, none in Vulkan files) |
| `check-dx12-shaders.py` | PASS (175 variants) |
| `audit-structured-composition-contract.py` | PASS (23 constants, 10 pinned expressions) |
| `check-vulkan-shaders.py` | PASS (105 variants, 665 modules validated) |
| `compile-shaders.py --check` | PASS |
| `check-doc-links.py` | PASS |
| `check-claude-layout.py` | PASS (the fixed "exactly 9 workflow references" expectation became a floor when the Vulkan shader audits were wired into `build-ubuntu.yml`) |
| `git diff --check` | clean |

### 7.5 Build matrix

Every configuration has its own binary directory and shares the already-populated
`vcpkg_installed` tree, so no dependency is rebuilt. All use Ninja, `Release`,
`x64-mingw-static-release`, `--parallel 1` — the same toolchain and vcpkg
arguments as `tools/build/windows/build-mingw.bat`, plus only the option under
test.

| Configuration | Directory | Result |
|---|---|---|
| Vulkan ON + DX12 ON, developer features ON (the default) | `build/release-mingw-x86_64` | built |
| Vulkan OFF (`MELONPRIME_FORCE_DISABLE_VULKAN=ON`), DX12 ON | `build/matrix-vulkan-off` | built |
| Vulkan ON, DX12 OFF (`MELONPRIME_FORCE_DISABLE_DX12=ON`) | `build/matrix-dx12-off` | built |
| Vulkan ON + DX12 ON, developer features OFF (release shape) | `build/matrix-dev-off` | built |
| Vulkan ON + DX12 ON + `MELONDS_VULKAN_ENABLE_VALIDATION=1` | `build/matrix-validation` | built; used for every validation-layer run |
| Linux | — | **not buildable here**; static verification only, see §7.2 |
| macOS | — | **not buildable here**; manual review only, see §7.2 |
