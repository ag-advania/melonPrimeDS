# Vulkan Phase 7.4 Vertex Upload Verification

## Scope

Phase 7.4 defines and validates the CPU-to-Vulkan packed polygon upload contract.
It does not switch ROM rendering away from the Software correctness baseline and
does not claim that native DS polygon rasterization is complete.

## Implemented

- OpenGL Classic-compatible 28-byte (`7 x u32`) packed vertex layout
- fixed 64-byte, 16-byte-aligned polygon metadata record
- `FinalPosition` path at scale 1
- `HiresPosition * scale >> 4` path above scale 1
- FinalZ compression plus Z-shift metadata
- FinalW, FinalColor, alpha and signed texture-coordinate packing
- polygon attributes, texture parameters, palette and resolved texture layer
- W-buffer, facing, translucent, shadow and line flags
- source `RenderPolygonRAM` order preservation
- degenerate polygon omission without renumbering source order
- triangle and polygon-fan index generation
- line duplicate-vertex filtering
- closed edge-index generation
- staging-buffer to device-local-buffer upload
- device-local-buffer to host readback and exact-byte comparison
- three create/upload/readback/destroy iterations

## Harness

```text
--melonprime-vulkan-vertex-upload-test <output.json>
```

Expected JSON invariants:

```text
passed = true
contract_version = 1
packed_vertex_size = 28
packed_polygon_size = 64
source_polygon_count = 4
emitted_polygon_count = 3
skipped_degenerate_count = 1
vertex_count = 9
index_count = 11
edge_index_count = 16
source_order_preserved = true
triangle_fan_validated = true
line_duplicate_filter_validated = true
scale_1_and_hires_scale_2_validated = true
polygon_metadata_validated = true
device_local_buffer_used = true
payload_matched = true
software_game_rendering_preserved = true
native_ds_polygon_raster_integrated = false
```

## Remaining Phase 7 work

Phase 7.5 must consume these source-order records to define adjacent-only polygon
batching and pipeline keys. Opaque, translucent, shadow, toon, fog, edge, line,
Better Polygons and GetLine integration remain later Phase 7 subphases.
