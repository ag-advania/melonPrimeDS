# Sapphire Vulkan source manifest

Pinned provenance for the MelonPrimeDS desktop Vulkan reconstruction.
This file records the exact Sapphire revision used for file-level ports.
It is not a runtime capability contract.

## Parent repository

| Field | Value |
|---|---|
| Repository | `SapphireRhodonite/melonDS-android` |
| Tag | `0.7.0.rc4` |
| Commit | `2c10e59d7209d354e90d9ef4228330bac3f6e794` |
| Local path | `C:\Users\Admin\Documents\git\melonDS-android` |

## Core repository (submodule gitlink)

| Field | Value |
|---|---|
| Repository | `SapphireRhodonite/melonDS-android-lib` |
| Gitlink commit | `d77944275fa61f9b79cfcead2c3e98993429a023` |
| Local path | `C:\Users\Admin\Documents\git\melonDS-android-lib` |

Verified on 2026-07-15:

```text
git -C melonDS-android describe --tags --exact-match HEAD  -> 0.7.0.rc4
git -C melonDS-android rev-parse HEAD                        -> 2c10e59d7209d354e90d9ef4228330bac3f6e794
git -C melonDS-android submodule status melonDS-android-lib  -> d77944275fa61f9b79cfcead2c3e98993429a023
```

Moving branch tips (`master`, `GBARumble_PR` HEAD, post-release commits) must not be used as reference sources.

## Copied frontend renderer files

Reference root: `app/src/main/cpp/renderer/` in parent tag `0.7.0.rc4`.

| Sapphire source | MelonPrime destination | Copy status |
|---|---|---|
| `FrameQueue.h` | `VulkanReference/FrameQueue.h` | P1 fresh copy baseline |
| `FrameQueue.cpp` | `VulkanReference/FrameQueue.cpp` | P1 fresh copy baseline |
| `VulkanOutput.h` | `VulkanReference/VulkanOutput.h` | P1 fresh copy baseline |
| `VulkanOutput.cpp` | `VulkanReference/VulkanOutput.cpp` | P1 fresh copy baseline |
| `VulkanCompositorShader.comp` | `VulkanReference/VulkanCompositorShader.comp` | P1 fresh copy baseline |
| `VulkanAccumulate3dShader.comp` | `VulkanReference/VulkanAccumulate3dShader.comp` | P1 fresh copy baseline |
| `VulkanSurfacePresenter.h` | `VulkanReference/VulkanSurfacePresenter.h` | P1 fresh copy baseline |
| `VulkanSurfacePresenter.cpp` | `VulkanReference/VulkanSurfacePresenter.cpp` | P1 fresh copy baseline |
| `VulkanSurfacePresenter.vert` | `VulkanReference/VulkanSurfacePresenter.vert` | P1 fresh copy baseline |
| `VulkanSurfacePresenter.frag` | `VulkanReference/VulkanSurfacePresenter.frag` | P1 fresh copy baseline |
| `VulkanFilterMode.h` | `VulkanReference/VulkanFilterMode.h` | P1 fresh copy baseline |
| `VulkanCompositorShaderData.h` | build-generated (`tools/vulkan`) | P3 regenerate from `.comp` |
| `VulkanAccumulate3dShaderData.h` | build-generated (`tools/vulkan`) | P3 regenerate from `.comp` |
| `VulkanSurfacePresenterVertexShaderData.h` | build-generated (`tools/vulkan`) | P3 regenerate from `.vert` |
| `VulkanSurfacePresenterFragmentShaderData.h` | build-generated (`tools/vulkan`) | P3 regenerate from `.frag` |

## Copied producer latch closure (MelonInstance)

Reference root: `app/src/main/cpp/MelonInstance.{h,cpp}` in parent tag `0.7.0.rc4`.

| Sapphire source | MelonPrime destination | Copy status |
|---|---|---|
| `MelonInstance.cpp` latch helpers | `SapphireVulkanFrameLatch.cpp` | P5 dependency closure + S71 desktop ownership/black-contract adapter |
| `MelonInstance.h` latch state | `SapphireVulkanFrameLatch.h` | P5 dependency closure + S71 desktop ownership/black-contract adapter |
| `MelonPrimeDesktop2DBlackContract.h` | `MelonPrimeDesktop2DBlackContract.h` | S71 desktop-only 2D payload / protected-black helpers |

## Copied core files

Reference root: `src/` in core gitlink `d77944275fa61f9b79cfcead2c3e98993429a023`.

| Sapphire source | MelonPrime destination | Copy status |
|---|---|---|
| `GPU2D_Soft.h/.cpp` structured Vulkan 2D API | `GPU_Soft.h/.cpp` | P4 core port |
| `GPU.h/.cpp` front buffer / framebuffer | `GPU.h/.cpp` | P4 core port |
| `GPU3D_Vulkan.h/.cpp` capture export | `GPU3D_Vulkan.h/.cpp` | P4 core port (audit) |

## Local adaptation summary

Desktop adaptations are isolated in `MELONPRIME_ADAPT_BEGIN` / `MELONPRIME_ADAPT_END` blocks or separate adapter files:

- `volk` dispatch instead of direct Vulkan prototypes
- `VkImage` frame resources instead of EGL/OpenGL textures
- Win32/Qt surface host (`MelonPrimeVulkanSurfaceHost`) instead of `ANativeWindow`
- Qt layout affine transforms, HUD, radar, splash overlays (`MelonPrimeScreenVulkan`)
- Separate present queue family and timeline semaphore submit fix (retained)
- Build-generated SPIR-V headers via `tools/vulkan/generate_sapphire_spirv.py`

## Legacy custom path (removed P9)

The MelonPrime-only frontend bridge listed below has been removed.

```text
MelonPrimeStructuredSnapshot (removed)
captureCompletedSnapshot() (removed)
buildSoftPackedSnapshot() (removed)
producer-side composeAndSubmitFrame() (removed)
SapphireStructured2DFrameSnapshot ring in GPU_Soft (removed S59-5)
GPU::CopyStructured2DFrameSnapshot() (removed S59-5)
```

Structured Vulkan 2D data is exported only through:

```text
GPU.Framebuffer[frontBuffer] packed rows
SoftRenderer::GetStructuredVulkan2DPlane()
SoftRenderer capture metadata buffers
```

## S59 splash-fix progress

| Phase | Description | Status |
|---|---|---|
| S59-1 | Restore Sapphire VulkanOutput public API | **done** |
| S59-2 | buildCompositionInputs from queued FrameResource snapshot | **done** |
| S59-3 | GUI presenter stops querying live frame view for acceptance | **done** |
| S59-4 | VulkanRenderer3D color target accessors | **done** |
| S59-5 | Remove redundant structured frame snapshot bridge | **done** |
| S59-6 | Upstream parity tracker + capture metadata path | **done** (see `docs/vulkan/SAPPHIRE_UPSTREAM.md`) |

## Phase progress

| Phase | Description | Status |
|---|---|---|
| P0 | Source pin manifest | **done** (`51660830f`) |
| P1 | Clean copy directory | **done** (`9d9461153`) |
| P2 | FrameQueue exact port | **done** (`3cd3bab92`) |
| P3 | VulkanOutput/shader exact port | **done** (`7a1e36129`) |
| P4 | Core exact port | **done** (`9b62b74ef`) |
| P5 | Latch dependency closure | **done** (this commit) |
| P6 | runFrame transaction port | **done** (this commit) |
| P7 | Presenter ownership port | **done** (this commit) |
| P8 | Desktop adapters restore | **done** (P4 LegacyCustom presenter + MelonPrimeScreenVulkan overlays retained) |
| P9 | Legacy custom path removal | **done** (this commit) |

## S71 2D black contract progress

| Phase | Description | Status |
|---|---|---|
| S71-1 | Split physical 2D ownership from render-time 3D screen ownership | **done** |
| S71-2 | Treat opaque black as present 2D payload in desktop frame caching | **done** |
| S71-3 | Preserve actual packed 2D source and protected-black metadata during structured merges | **done** |
| S71-4 | Validate protected-black invariants before Vulkan composition | **done** |
| S71-5 | Add black-over-3D and ownership-transition golden tests | **done** |
| S71-6 | Track Sapphire FrameLatch extraction and desktop adapter parity in CI | **done** |
