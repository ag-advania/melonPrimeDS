# Vulkan Phase 7.7 Translucent Pipeline Verification

## Scope

This phase adds an isolated untextured translucent Vulkan raster bootstrap.
It consumes the Phase 7.4 packed vertex/index payload and Phase 7.5
adjacent-only batch plan without connecting live ROM rendering.

Implemented and verified by the harness:

- source-alpha / one-minus-source-alpha RGB blending
- MAX alpha blend operation
- LESS and LESS_OR_EQUAL depth comparison
- polygon-controlled depth write
- Z-buffer and W-buffer vertex/fragment variants
- stencil NOTEQUAL comparison against `0x40 | polyID` in the low seven bits
- preservation of stencil bit 7 while replacing bit 6 and polyID
- suppression of a second translucent polygon with the same polyID
- blending of a translucent polygon with a different polyID
- fog attribute B-channel write mask and preserve variant
- translucent alpha range rejection for alpha 0 and alpha 31
- detection of the alpha-31 `NeedsOpaquePass` condition
- device-local vertex/index buffers
- color, attribute and stencil GPU readback
- three complete create/draw/readback/destroy iterations

## Deliberately not claimed

- live `RenderPolygonRAM` integration
- textured translucent rendering
- decal, modulate, toon or highlight textures
- shadow mask or visible shadow passes
- final fog or edge passes
- `GetLine()` integration
- complete native Vulkan 3D renderer

`VulkanRenderer` continues to use the Software renderer as the game-rendering
correctness source. `NativeVulkan3DImplemented` remains false.

## Harness

```text
--melonprime-vulkan-translucent-pipeline-test <json>
```

Expected top-level values:

```text
contract_version = 1
completed_iterations = 3
batch_count = 14
draw_count = 14
unique_pipeline_count >= 5
stencil_rule_passed = true
blend_rule_passed = true
needs_opaque_pass_detected = true
same_polyid_suppressed = true
different_polyid_blended = true
depth_write_off_passed = true
depth_write_on_passed = true
less_equal_passed = true
w_buffer_passed = true
fog_write_mask_passed = true
fog_preserve_mask_passed = true
alpha_zero_rejected = true
alpha_full_rejected = true
stencil_low7_passed = true
samples_matched = true
native_ds_polygon_raster_integrated = false
```
