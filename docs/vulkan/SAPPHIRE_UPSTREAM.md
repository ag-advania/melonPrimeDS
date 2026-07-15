# Sapphire upstream parity tracker

This document records the pinned Sapphire sources imported into MelonPrimeDS
Vulkan and the allowed desktop-only deltas.

## Pinned upstream

| Layer | Repository | Pin |
|---|---|---|
| Frontend | `SapphireRhodonite/melonDS-android` | tag `0.7.0.rc4` (`2c10e59d7209d354e90d9ef4228330bac3f6e794`) |
| Core | `SapphireRhodonite/melonDS-android-lib` | commit `d77944275fa61f9b79cfcead2c3e98993429a023` |

Local reference clones:

```text
C:\Users\Admin\Documents\git\melonDS-android
C:\Users\Admin\Documents\git\melonDS-android-lib
```

## Imported files (algorithm body should match upstream)

| File | Allowed diffs |
|---|---|
| `VulkanReference/VulkanOutput.cpp` | include paths, volk, generated shader symbol names, logging, `MELONPRIME_*` gates |
| `VulkanReference/VulkanOutput.h` | same as above |
| `VulkanReference/VulkanSurfacePresenter.*` | Qt/Win32 surface host adapters outside presenter core |
| `VulkanReference/FrameQueue.*` | desktop generation hooks only |
| `SapphireVulkanFrameLatch.*` | `MelonInstance` latch closure with NDS/GPU desktop bindings |
| `SapphireGPU2DCore/GPU2D_Soft.cpp` | `UnitSync`, `VulkanDesktopCompat`, framebuffer pointer wiring, `MELONPRIME_*` gates |
| `SapphireGPU2DCore/GPU2D_Soft.h` | namespace wrapper `SapphireGPU2DCore::GPU2D`, desktop include paths |

## Forbidden diffs (presentation ownership)

Do not reintroduce MelonPrime-only logic in `VulkanOutput` or the frontend session
present path:

```text
GUI present-time live Vulkan3DFrameView acceptance gates
queued serial / generation must match live renderer serial / generation
producer-side composeAndSubmitFrame()
splash hide without commitPresentedFrame()
clear-only swapchain present counted as game-frame success
```

Queued frames must compose from `FrameResource` snapshots prepared on the
producer thread.

## Desktop adapters (intentional non-upstream)

```text
MelonPrimeVulkanSurfaceHost
MelonPrimeScreenVulkan (splash, HUD, layout)
MelonPrimeVulkanFrontendSession (Qt mutex, generation, presenter registration)
VulkanDesktopCompat logging/stub hooks
SapphireGPU2DCore/UnitSync.* (melonPrime GPU2D → sapphire Unit mirror)
SapphireGPU2DSoftAccess facade (latch API parity)
```

Mark desktop-only blocks with:

```cpp
// MELONPRIME_DESKTOP_ADAPTER_BEGIN
...
// MELONPRIME_DESKTOP_ADAPTER_END
```

## GPU2D parity status

| Status | Detail |
|---|---|
| **OPEN / integration in progress** | Vendored `SapphireGPU2DCore/GPU2D_Soft.*` replaces line-range extraction hybrid (S61-5). |
| **Removed** | `SapphireGPU2DStructuredVulkan.cpp`, `tools/extract_sapphire_gpu2d_structured.py` (deprecated). |
| **Remaining risk** | `UnitSync` mirrors melonPrime `GPU2D` into sapphire `Unit`; scanline ownership must stay in vendored renderer. |

### Per-file ownership (S61-5+)

| Local file | Upstream source | Semantic owner |
|---|---|---|
| `SapphireGPU2DCore/GPU2D_Soft.cpp` | `melonDS-android-lib` @ `d77944275` | Sapphire `GPU2D::SoftRenderer` |
| `SapphireGPU2DCore/UnitSync.cpp` | MelonPrime adapter | Desktop-only |
| `GPU_Soft.cpp` (Vulkan) | MelonPrime | Delegates draw/capture to sapphire renderer |
| `SapphireGPU2DSoftAccess.cpp` | MelonPrime facade | Latch API only |

## Known parity gaps (frontend)

| Area | Sapphire | MelonPrimeDS | Status |
|---|---|---|---|
| Complete-frame structured ring | not used | removed S59-5 | closed |
| `GetVulkan3DFrameView()` on GUI present | not used | removed S59-3 | closed |
| `buildCompositionInputs` live serial gate | absent | removed S59-2 | closed |
| Clear-only present success | rejected upstream intent | rejected S61-1 | closed |
| Screen geometry validation | layout-driven | `hasValidGameScreenLayout` S61-2 | closed |

## Verification checklist

1. Vulkan ROM boot: `[VulkanPresent] buildInputs=1` even when `queuedSerial != liveSerial`
2. `[VulkanPresent] result=0` (`PresentedGameFrame`) before splash hide
3. `[VulkanSurfaceDraw] gameDrawCalls>=1` on successful presents
4. `[VulkanFrameContent]` shows non-zero plane/meta counts after boot
5. `[VulkanProducer] queuePush` within 2–3 frames of ROM start
6. Software/OpenGL backends unchanged (non-Vulkan `SoftRenderer2D` path)
