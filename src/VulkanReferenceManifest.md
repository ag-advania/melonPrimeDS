# Vulkan reference port manifest

This manifest is implementation metadata for the desktop Vulkan reconstruction. It is not a runtime capability contract and must never be used to select a renderer or report a backend as active.

## Pinned reference points

- Core repository: `SapphireRhodonite/melonDS-android-lib`
- Core commit: `d77944275fa61f9b79cfcead2c3e98993429a023`
- Frontend repository: `SapphireRhodonite/melonDS-android`
- Frontend tag: `0.7.0.rc4` (commit `2c10e59d7209d354e90d9ef4228330bac3f6e794`)

The matching constants live in `VulkanReferencePortVersion.h`. Updating either reference requires a new file-by-file audit of every row below.

## Difference classes

- `Reference code`: text matches the pinned source after ignoring line-ending conversion.
- `Core compatibility adaptation`: include/dispatch/API changes required by this melonDS fork.
- `Desktop platform adaptation`: replaces Android window, EGL, JNI, or Vulkan dispatch ownership without changing the reference ABI.
- `MelonPrime adaptation`: game/frontend integration kept outside the reference algorithm where possible.
- `Temporary bridge`: pre-existing phased integration that remains isolated until the deletion phase named in the notes.

There are no unclassified divergences in the audited set. Any future difference not covered by a row is an unexplained divergence and must be reverted to the pinned reference or added here with a concrete owner and removal/lifetime rule.

## Core implementation files

| Destination | Reference path | Class and retained difference |
|---|---|---|
| `src/GPU.h` | `src/GPU.h` | Core compatibility adaptation: the current upstream GPU owner keeps display-capture registers outside the older Sapphire GPU2D unit. R16 adds a line-0 capture snapshot (control, engine-A display control, and screen swap) only inside the MelonPrime Vulkan build gate, without changing register read/write behavior or the non-Vulkan GPU layout. |
| `src/GPU.cpp` | `src/GPU.cpp` | Same classification as the header. Inside the MelonPrime Vulkan build gate, capture start/end and VRAM capture-block ownership use the immutable frame control; non-Vulkan builds retain the original live-register path. Savestate format is unchanged and reconstructs the transient latch after load only when the adapter exists. |
| `src/GPU_Soft.cpp` | `src/GPU2D_Soft.cpp` | Core compatibility adaptation for the current monolithic software GPU owner: the MelonPrime Vulkan branch makes per-line capture mixing and destination writes consume the R16 line-0 snapshot, while the non-Vulkan branch retains the original current-core inputs. The DS RGB555 mixing and capture-range VRAM dirty behavior remain current-core code. |
| `src/GPU3D_AcceleratedFrontend.h` | `src/GPU3D_AcceleratedFrontend.h` | Reference-majority MelonPrime adaptation: provenance plus immutable polygon/render-state input, per-draw texture keys, high-resolution setting transport, and coverage depth-bias transport. Geometry, line endpoint resolution, draw ordering, and coverage algorithms remain at the pinned reference locations. |
| `src/GPU3D_AcceleratedFrontend.cpp` | `src/GPU3D_AcceleratedFrontend.cpp` | Same classification as the header. The R10 conformance audit restored reference line ownership: this frontend emits the two resolved endpoints and `GPU3D_Vulkan.cpp` performs the pinned `appendLineSegment` expansion. |
| `src/GPU3D_Texcache.h` | `src/GPU3D_Texcache.h` | Core compatibility adaptation: current melonDS GPU ownership and clear-bitmap dirty ABI are retained; the pinned before-mutation callback is preserved so Vulkan waits only immediately before invalidation/reuse. |
| `src/GPU3D_Vulkan.h` | `src/GPU3D_Vulkan.h` | Reference-majority core/Desktop adaptation: `Renderer3D` ownership wrappers, `volk`, desktop `VulkanContext` accessors, scaled render-target state, six-slot resource completion handoff, immutable scene/capture input, per-context clear/toon upload generations, capture-slot frame identity, and `Vulkan3DFrameView`. The duplicate core compositor was removed in R6; remaining temporary/dead builders are owned by R26. |
| `src/GPU3D_Vulkan.cpp` | `src/GPU3D_Vulkan.cpp` | Same classification as the header. The pinned raster/texture/line/clear/final-effect/capture-export algorithms remain the base; desktop queue synchronization, frame handoff, immutable full render/capture-state dispatch, capture slot/history identity, per-context clear-buffer reuse, and change-driven toon-table upload are isolated adapters. |
| `src/GPU3D_TexcacheVulkan.h` | `src/GPU3D_TexcacheVulkan.h` | Core compatibility adaptation: `<volk.h>` replaces direct Vulkan prototypes. |
| `src/GPU3D_TexcacheVulkan.cpp` | `src/GPU3D_TexcacheVulkan.cpp` | Core compatibility adaptation: `volk` replaces `VulkanDispatch.h`. |
| `src/VulkanContext.h` | `src/VulkanContext.h` | Desktop platform adaptation: process-wide ref-counted context, desktop surface extensions, and `volk`; R4 completes platform instance/present requirements and R24 completes shared synchronization/lifetime. |
| `src/VulkanContext.cpp` | `src/VulkanContext.cpp` | Desktop platform adaptation corresponding to the header; Android surface/AHB/debug integration is intentionally not imported into core. |
| `src/VulkanPerfStats.h` | `src/VulkanPerfStats.h` | Reference code. |

## Core shader sources

Every source below is reference code and is text-identical to the pinned core commit after ignoring line endings.

| Destination | Reference path |
|---|---|
| `src/GPU3D_Vulkan_InterpSpansShader.comp` | `src/GPU3D_Vulkan_InterpSpansShader.comp` |
| `src/GPU3D_Vulkan_BinCombinedShader.comp` | `src/GPU3D_Vulkan_BinCombinedShader.comp` |
| `src/GPU3D_Vulkan_CalculateWorkOffsetsShader.comp` | `src/GPU3D_Vulkan_CalculateWorkOffsetsShader.comp` |
| `src/GPU3D_Vulkan_SortWorkShader.comp` | `src/GPU3D_Vulkan_SortWorkShader.comp` |
| `src/GPU3D_Vulkan_TriRasterShader.comp` | `src/GPU3D_Vulkan_TriRasterShader.comp` |
| `src/GPU3D_Vulkan_TriRasterBaseShader.comp` | `src/GPU3D_Vulkan_TriRasterBaseShader.comp` |
| `src/GPU3D_Vulkan_TriRasterCompatShader.comp` | `src/GPU3D_Vulkan_TriRasterCompatShader.comp` |
| `src/GPU3D_Vulkan_DepthBlendShader.comp` | `src/GPU3D_Vulkan_DepthBlendShader.comp` |
| `src/GPU3D_Vulkan_FinalPassShader.comp` | `src/GPU3D_Vulkan_FinalPassShader.comp` |
| `src/GPU3D_Vulkan_CaptureLineExportShader.comp` | `src/GPU3D_Vulkan_CaptureLineExportShader.comp` |
| `src/GPU3D_Vulkan_GraphicsRasterShader.vert` | `src/GPU3D_Vulkan_GraphicsRasterShader.vert` |
| `src/GPU3D_Vulkan_GraphicsRasterShader.frag` | `src/GPU3D_Vulkan_GraphicsRasterShader.frag` |
| `src/GPU3D_Vulkan_GraphicsNoColorShader.frag` | `src/GPU3D_Vulkan_GraphicsNoColorShader.frag` |
| `src/GPU3D_Vulkan_GraphicsClearShader.frag` | `src/GPU3D_Vulkan_GraphicsClearShader.frag` |
| `src/GPU3D_Vulkan_GraphicsFinalShader.vert` | `src/GPU3D_Vulkan_GraphicsFinalShader.vert` |
| `src/GPU3D_Vulkan_GraphicsEdgeShader.frag` | `src/GPU3D_Vulkan_GraphicsEdgeShader.frag` |
| `src/GPU3D_Vulkan_GraphicsEdgeFogShader.frag` | `src/GPU3D_Vulkan_GraphicsEdgeFogShader.frag` |
| `src/GPU3D_Vulkan_GraphicsFogShader.frag` | `src/GPU3D_Vulkan_GraphicsFogShader.frag` |

## Core generated shader headers

The pinned headers were used to audit source/variant identity, but are no longer
tracked or included from the source tree. `tools/vulkan/sapphire_shader_manifest.json`
records every core source, entry point, define set, legacy symbol/header name,
reference path, and pinned commit. `generate_sapphire_spirv.py` emits the 25 core
headers under `${CMAKE_BINARY_DIR}/generated/sapphire`; CMake regenerates them
from GLSL only when a manifest, ABI include, generator, or shader source changes.
The old source-only `GPU3D_Vulkan_ShaderData.h` had no corresponding GLSL source
and no runtime consumer, so it is deliberately not part of the generated set.

## Frontend implementation files

Reference paths in this table are relative to `app/src/main/cpp/renderer/` in the pinned frontend tag.

| Destination | Reference path | Class and retained difference |
|---|---|---|
| `src/frontend/qt_sdl/VulkanReference/VulkanOutput.h` | `VulkanOutput.h` | Core compatibility adaptation: local include paths and `volk`; descriptor/push-constant ABI unchanged. |
| `src/frontend/qt_sdl/VulkanReference/VulkanOutput.cpp` | `VulkanOutput.cpp` | Core compatibility adaptation: `volk` replaces `VulkanDispatch.h`; reference algorithm otherwise retained. |
| `src/frontend/qt_sdl/VulkanReference/VulkanCompositorShader.comp` | `VulkanCompositorShader.comp` | Reference code. |
| `src/frontend/qt_sdl/VulkanReference/VulkanAccumulate3dShader.comp` | `VulkanAccumulate3dShader.comp` | Reference code. |
| `src/frontend/qt_sdl/VulkanReference/FrameQueue.h` | `FrameQueue.h` | Desktop platform adaptation: reference policy, frame identity, queue statistics, deadline, drop, reuse, and resync ABI retained; EGL/GL resource fields replaced by Vulkan handles. |
| `src/frontend/qt_sdl/VulkanReference/FrameQueue.cpp` | `FrameQueue.cpp` | Desktop platform adaptation: full reference queue lifecycle restored; Android GL texture allocation/destruction excluded because desktop VulkanOutput/presenter own images. |
| `src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h` | `VulkanSurfacePresenter.h` | Desktop platform adaptation: `void*` native handle, Win32 declarations, local includes, and `volk`; Android native window API excluded. |
| `src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp` | `VulkanSurfacePresenter.cpp` | Desktop platform adaptation: Win32 surface creation and client extent replace `ANativeWindow`; cross-platform generalization is owned by R20. |
| `src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.vert` | `VulkanSurfacePresenter.vert` | Reference code. |
| `src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.frag` | `VulkanSurfacePresenter.frag` | Reference code. |
| `src/frontend/qt_sdl/VulkanReference/VulkanFilterMode.h` | `VulkanFilterMode.h` | Reference code. |

## Frontend generated shader headers

The same manifest and generator emit all four frontend headers into the same
build-directory namespace as the core headers. The manifest pins their reference
paths to `app/src/main/cpp/renderer/` at `0.7.0.rc4`; source-tree copies and the
former private core compositor SPIR-V duplicate are intentionally absent.

## Deliberately excluded Android ownership

- JNI, Java callbacks, Android logging, and Activity lifecycle.
- `ANativeWindow` retain/release and size queries.
- Android surface and Android Hardware Buffer extensions in the shared desktop context.
- EGL/OpenGL texture allocation in the Vulkan frame queue.

Desktop replacements are owned by `VulkanContext`, `MelonPrimeVulkanSurfaceHost` (R20), and the Qt frontend session (R3/R19/R21). No Android UI code is copied into the core renderer.
