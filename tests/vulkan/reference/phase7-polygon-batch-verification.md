# Vulkan Phase 7.5 Polygon Batch Verification

## Scope

This phase adds deterministic pipeline/texture keys and adjacent-only batching on
top of the Phase 7.4 packed vertex/index/polygon upload.

Implemented:

- strict `RenderPolygonRAM` source-order validation
- 64-byte Vulkan raster pipeline key
- 32-byte texture/sampler binding key
- 128-byte batch record
- primitive, render mode, texture mode, toon/highlight, W-buffer, depth,
  fog-attribute and shadow-stage key fields
- normalized texture identity and clamp/repeat/mirror sampler identity
- contiguous index and edge-index range validation
- adjacent-only batch extension
- explicit A/B/A non-regrouping
- source-order gap batch termination
- host staging to device-local buffer transfer
- exact device-local readback comparison

Not implemented in this phase:

- native opaque polygon draw
- Vulkan graphics pipeline creation/cache
- translucent or shadow rendering
- texture cache integration
- ROM-visible Vulkan 3D output

The game renderer remains on the Software correctness baseline.

## No-ROM harness

```text
--melonprime-vulkan-polygon-batch-test <json>
```

Expected deterministic values:

```text
contract_version       = 1
pipeline_key_size      = 64
texture_key_size       = 32
batch_record_size      = 128
source_polygon_count   = 13
emitted_polygon_count  = 12
batch_count            = 9
```

Required booleans:

```text
layout_validated                    = true
source_order_preserved              = true
adjacent_only_batching_validated    = true
frame_wide_regrouping_rejected      = true
texture_sampler_key_validated       = true
pipeline_key_validated              = true
invalid_source_order_rejected       = true
invalid_buffer_range_rejected       = true
upload_submitted                    = true
readback_completed                  = true
device_local_buffer_used            = true
payload_matched                     = true
software_game_rendering_preserved   = true
native_ds_polygon_raster_integrated = false
```

## Regression harnesses

The Windows package also executes:

- Phase 7.4 vertex upload
- Phase 7.3 clear bitmap
- Phase 7.2 clear plane
- Phase 7.1 raster bootstrap
- renderer shell
- output lease
- Vulkan device probe
- shader line-ending and generated-file checks
- `git diff --check`
