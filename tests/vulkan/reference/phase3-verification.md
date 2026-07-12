# Phase 3 verification record

Date: 2026-07-12 (Asia/Tokyo)

## Status

Phase 3 is implemented for the current Windows verification scope. The Qt
frontend owns a process-lifetime `QVulkanInstance`, creates surfaces through
Qt, scores physical devices, validates the raster and compute baselines, and
owns the selected logical device per emulation instance. The renderer remains
Software and the Vulkan choices remain hidden from the UI.

## Commit

`feat(vulkan): add Qt Vulkan instance host and device capability probe`

## Build verified

- Vulkan ON: Windows MinGW configure, compile, and link passed with
  `qtbase[vulkan]` supplied only by the opt-in vcpkg feature.
- Force-disabled: Windows MinGW configure, compile, and link passed.
- Default OFF: Windows MinGW configure, compile, and link passed and is the
  final working-tree build configuration.
- The force-disabled Ninja graph contains no Vulkan host/probe source and the
  executable contains no Vulkan probe environment names or diagnostic text.
- Compiler-backed SPIR-V verification passed in the Vulkan-ON build; the final
  default-OFF compiler-free source/hash integrity check also passed.

## Runtime verified

The developer-only `--melonprime-vulkan-probe` harness completed ten consecutive
Qt surface and logical-device create/destroy cycles and exited successfully:

- completed iterations: 10 / 10
- instance, presentation, raster, and compute: available
- selected device: NVIDIA GeForce RTX 5070 Ti (`VEN_10DE`, `DEV_2C05`)
- graphics / compute / present queue family: 0 / 0 / 0
- color format: `VK_FORMAT_B8G8R8A8_UNORM`
- depth/stencil format: `VK_FORMAT_D32_SFLOAT_S8_UINT`
- timeline semaphore: available
- max compute work-group count Y: 65535
- max compute work-group invocations: 1024
- max compute shared memory: 49152 bytes

The restored default-OFF build also reproduced the unchanged HUD golden output:
`8CC5500A2D7871565D3512129ECA1B640AD00CE0D1398A41FC35D8DC1004B490`.

`QVulkanInstance::surfaceForWindow` and `supportsPresent` are used; no frontend
Win32 surface creation is present. Unsupported paths return one explicit reason.

## Lifetime and policy

- Instance creation is lazy and GUI-thread-only.
- `MelonApplication` owns and destroys the process instance.
- `EmuInstance` caches the device context so future renderer/presenter objects
  share one logical device.
- Device destruction waits idle before releasing the Qt device-function wrapper.
- Development builds request `VK_LAYER_KHRONOS_validation` when installed;
  release builds do not require it.
- The Phase 2 renderer and presenter fallback policy remains unchanged.

## Validation

- Required config/schema/ownership/platform/thread/SRP audits passed.
- `git diff --check` passed.
- No Vulkan validation messages occurred during the ten-cycle run, but the
  Khronos validation layer was not installed and therefore was not enabled.

## Deferred by current scope

- X11, Wayland, macOS, and MoltenVK build/runtime verification.
- GitHub Actions execution.
- Validation-enabled repetition after installing
  `VK_LAYER_KHRONOS_validation`.
- ROM-backed renderer creation and rendering (Phase 4 onward).

## Rollback

Revert the Phase 3 commit. The persisted renderer IDs introduced in Phase 2
remain unchanged.
