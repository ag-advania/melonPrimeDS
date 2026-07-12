# Vulkan Phase 7 verification

**Status:** Patch applied; Windows build and native raster bootstrap runtime pending.
**Build status:** `PENDING_WINDOWS_BUILD`
**Bootstrap runtime status:** `PENDING_NATIVE_RASTER_BOOTSTRAP`
**ROM runtime status:** `SOFTWARE_CORRECTNESS_BASELINE_PRESERVED`

## Implemented scope

- Adds a native offscreen Vulkan graphics bootstrap under the complete Vulkan build gate.
- Reuses the already-generated Phase 4 Vulkan presenter vertex/fragment SPIR-V.
- Creates a 256x192 color attachment and depth/stencil attachment.
- Creates a sampled 1x1 source image, descriptor set, sampler, render pass, framebuffer, pipeline layout, and graphics pipeline.
- Submits an actual fullscreen-triangle draw on the selected Vulkan graphics queue.
- Copies the color attachment back to host-visible memory.
- Validates the first, center, and last pixels with a one-unit channel tolerance.
- Repeats device/resource creation and destruction three times in the Windows one-command validation BAT.
- Updates the Phase 6 renderer-shell contract to expose that the native raster bootstrap exists without claiming that DS polygon rasterization is integrated.

## Safety boundary

This Phase intentionally does **not** replace `SoftRenderer3D` for ROM rendering. The Phase 6 Software 2D/3D/capture/CPU-BGRA path remains the correctness source. The bootstrap proves that the selected device can execute MelonPrime-owned Vulkan graphics work and return deterministic pixels before DS polygon state, texture cache, fog, edge, shadow, capture, and final composition are migrated.

## Windows command executed by the package

```bat
build\release-mingw-x86_64\melonPrimeDS.exe --melonprime-vulkan-raster-bootstrap-test <json>
```

## Expected JSON contract

```json
{
  "passed": true,
  "requested_iterations": 3,
  "completed_iterations": 3,
  "draw_submitted": true,
  "readback_completed": true,
  "samples_matched": true,
  "software_game_rendering_preserved": true,
  "native_ds_polygon_raster_integrated": false
}
```

## Deferred acceptance

- DS polygon RAM upload and draw ordering.
- Z/W buffer behavior, translucent polygon-ID rules, shadow mask/shadow polygons.
- Texture decode/cache, repeat/mirror, alpha test, toon/highlight, fog, edge marking, antialiasing, and line polygons.
- Native Vulkan 2D, display capture, final composition, and zero-copy presenter handoff.
- ROM screenshot parity and long-run gameplay.
- Linux, Wayland, X11, macOS/MoltenVK, CI, and validation-layer execution.
