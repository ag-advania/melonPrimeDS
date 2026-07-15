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

## Forbidden diffs (presentation ownership)

Do not reintroduce MelonPrime-only logic in `VulkanOutput` or the frontend session
present path:

```text
GUI present-time live Vulkan3DFrameView acceptance gates
queued serial / generation must match live renderer serial / generation
producer-side composeAndSubmitFrame()
splash hide without commitPresentedFrame()
```

Queued frames must compose from `FrameResource` snapshots prepared on the
producer thread.

## Desktop adapters (intentional non-upstream)

```text
MelonPrimeVulkanSurfaceHost
MelonPrimeScreenVulkan (splash, HUD, layout)
MelonPrimeVulkanFrontendSession (Qt mutex, generation, presenter registration)
VulkanDesktopCompat logging/stub hooks
```

Mark desktop-only blocks with:

```cpp
// MELONPRIME_DESKTOP_ADAPTER_BEGIN
...
// MELONPRIME_DESKTOP_ADAPTER_END
```

## Known parity gaps

| Area | Sapphire | MelonPrimeDS | Status |
|---|---|---|---|
| `GPU2D::SoftRenderer` capture metadata | in-core `GPU2D_Soft.cpp` | `SoftRenderer` + `SapphireGPU2DSoftAccess` facade | partial: capture 3D source + line mask in `GPU_Soft::DoCapture` |
| Complete-frame structured ring | not used by Sapphire latch | removed S59-5 | closed |
| `GetVulkan3DFrameView()` on GUI present | not used | removed S59-3 | closed |
| `buildCompositionInputs` live serial gate | absent | removed S59-2 | closed |

Full vendor of `GPU2D_Soft.cpp/.h` from the pinned core commit remains the
next step if capture ownership regressions appear after the splash fix.

## Verification checklist

1. Vulkan ROM boot: `[VulkanPresent] buildInputs=1` even when `queuedSerial != liveSerial`
2. First successful present calls `commitPresentedFrame()` and hides no-ROM splash
3. `[VulkanProducer] queuePush` appears within 2–3 frames of ROM start
4. Software/OpenGL backends unchanged
