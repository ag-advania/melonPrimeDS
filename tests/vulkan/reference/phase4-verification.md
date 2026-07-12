# Phase 4 verification record

Date: 2026-07-12 (Asia/Tokyo)

## Status

The Phase 4 CPU BGRA Vulkan presenter baseline is implemented for Windows.
`ScreenPanelVulkan` embeds a Qt Vulkan window, reuses the existing
`ScreenLayout` and native CPU compositor, and presents the resulting frame with
a Vulkan sampled image and generated SPIR-V shaders. ROM-dependent and long-run
acceptance evidence remains pending because no scoped ROM is present.

## Commit

`feat(vulkan): add CPU BGRA Vulkan presenter baseline`

## Implementation

- The Vulkan presentation policy now selects `ScreenPanelVulkan` for Vulkan
  renderer IDs and the developer presenter override, without creating GL.
- Qt owns surface, swapchain, acquire/present synchronization, resize recovery,
  frame fences, and device-loss callbacks.
- MelonPrime owns the full-screen pipeline, sampler, descriptor sets, and one
  device-local upload image per concurrent frame.
- One persistently mapped host-coherent staging allocation is split into aligned
  frame slots; there is no per-frame Vulkan allocation.
- CPU BGRA is uploaded as `VK_FORMAT_B8G8R8A8_UNORM` with explicit transfer and
  fragment-read barriers.
- Per-slot frame hashes skip byte-identical uploads. Changed frames calculate a
  bounding dirty rectangle and copy only that image region.
- Nearest/linear screen filtering selects separate immutable samplers.
- The existing native compositor supplies screen layout, no-ROM splash, OSD,
  and Custom HUD. Its software path now also composites the circular radar
  overlay, so Vulkan does not own a second layout implementation.
- Embedded-window pointer, tablet, touch, wheel, enter, and leave events are
  forwarded to the existing `ScreenPanel` input handlers.
- A developer-only capture harness uses the actual swapchain readback path.

## Build verified

- Vulkan ON Windows MinGW configure, compile, and link passed.
- Compiler-backed generation and verification passed for the presenter vertex
  and fragment shaders.
- Force-disabled Windows MinGW configure, compile, and link passed; the Ninja
  graph and executable contain no Phase 4 Vulkan source or runtime text.
- Default-OFF Windows MinGW configure, compile, and link passed and is the final
  working build configuration.
- The MinGW static-Qt `QVulkanWindow` boundary translation unit is compiled
  without LTO to avoid GNU ld discarding a required QWindow adjustment thunk;
  all other translation units retain the normal project LTO setting.

## Runtime verified

All captures below completed with exit code 0 through `QVulkanWindow::grab()`:

- initial no-ROM Vulkan capture: 256 x 384
- live resize capture after moving the native window to 800 x 600: 784 x 536
- maximized capture: 2560 x 1344
- minimize followed by restore: capture succeeded
- two simultaneous emulation windows: both Vulkan captures succeeded

The captured no-ROM splash has the expected orientation, BGRA colors,
localized text, logo, and black clear area. The resize and maximized captures
preserve the shared layout and centering. The default-OFF HUD golden output is
unchanged:
`8CC5500A2D7871565D3512129ECA1B640AD00CE0D1398A41FC35D8DC1004B490`.

## Validation

- Required config/schema/ownership/platform/thread/SRP audits passed.
- Generated shader source/hash integrity check passed in the restored OFF tree.
- `git diff --check` passed.

## Unverified / follow-up acceptance

- Software renderer CPU frame presentation from a live ROM.
- Active OSD, Custom HUD/edit mode, radar, and Fast Forward while a ROM runs.
- A 30-minute active-rendering soak.
- Validation-layer-clean execution; the Khronos layer is not installed.
- QVulkanWindow uses its FIFO-managed baseline swapchain. Explicit
  MAILBOX/IMMEDIATE/FIFO_RELAXED selection for VSync OFF must be closed by the
  later synchronization/performance work before the overall plan is complete.
- Linux, macOS/MoltenVK, and GitHub Actions execution are deferred per the
  current Windows-only scope.

## Rollback

Revert the Phase 4 commit. The Phase 3 Vulkan probe remains available and the
presenter policy returns to its NativeQt fallback.
