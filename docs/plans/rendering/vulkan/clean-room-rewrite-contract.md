# Vulkan Clean-Room Rewrite — Integration Contract (Phase 0)

Branch: `develop_remakeVulkan`
Base commit at Phase 0: `2b90179cca8510c9721e7e8384f3dc550d6f07a6`

This document is the **Phase 0 deliverable** of the Vulkan clean-room rewrite. It
records the contract the new Vulkan backend must satisfy, derived **only from
non-Vulkan callers** — the `Renderer` / `Renderer3D` core interfaces, the Qt
frontend renderer selection, the screen panel base class, the build system and
CI. The bodies of the outgoing Vulkan files were not used to derive anything
here.

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
internal resolution, copies the result back and publishes it into a
double-buffered CPU surface. `GetOutput()` returns that surface at
`GetComposedWidth()/GetComposedHeight()` — the internal size — so no
native-resolution intermediate exists on the display path.

The compositor records into **its own `Vk::FrameRing`** (own command pool,
command buffer and fence) rather than the rasterizer's. It has to: the structured
planes are only complete after all 192 scanlines, long after `RenderFrame()`
submitted, and reusing the rasterizer's slot would reset the fence `GetLine()`'s
capture readback is still waiting on. Both rings submit to the same queue, so the
compositor's barrier over `FinalFB` picks up the rasterizer's writes through
submission order. `ComposeStructuredOutput()` waits on its own fence before
returning, which both publishes the frame and keeps the next `RenderFrame()` from
overwriting `FinalFB` while the compositor is still reading it. No
`vkDeviceWaitIdle` / `vkQueueWaitIdle` anywhere in that path.

## 6. Definition of Done

Tracked in the source instruction document. The headline gates:

- No reference taken from any forbidden source, nor from the outgoing Vulkan
  bodies.
- Vulkan initializes for real, or fails loudly with requested/actual/reason.
- Compute rasterizer matches the software renderer; algorithms correspond to
  `GPU3D_Compute`.
- Internal resolution 1x–16x survives to final present — no 256x192 intermediate
  downscale on the display path.
- Structured 2D + high-resolution 3D compose in Vulkan; master brightness,
  screen swap, display capture, Custom HUD, radar and OSD all correct.
- Native `VkSwapchainKHR` / `vkQueuePresentKHR`; no CPU readback on the final
  path; present mode queried, never assumed.
- No per-frame `vkDeviceWaitIdle` / `vkQueueWaitIdle`; correct barriers, image
  layouts, queue ownership, descriptor and swapchain lifetime.
- All eight renderer-switch directions plus repeated round trips are stable.
- Reflex / Anti-Lag work where supported and disable cleanly where not.
- Every shader and variant compiles offline; SPIR-V validates.
- Validation layer clean across object lifetime, descriptors, image layout,
  synchronization, command buffers, queues and swapchain.
- Windows Vulkan ON / OFF / DX12-coexist and Linux Vulkan builds all green;
  Software, OpenGL, OpenGL Compute and DX12 behavior unchanged.

Items that cannot be executed in this environment are reported as
**UNVERIFIED**, never as passing.
