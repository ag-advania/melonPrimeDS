# Vulkan Phase 7.8 Shadow Pipeline Verification

## Scope

This phase adds the untextured two-stage DS shadow path on top of the Phase 7.7 translucent pipeline.

Implemented bootstrap behavior:

- shadow-mask depth-fail stencil bit 7 write
- lower polygon-ID bits preserved by stencil write mask `0x80`
- visible-shadow same-polyID rejection pass
- visible-shadow blend pass only where bit 7 remains set
- lower seven stencil bits updated to `0x40 | polyID`
- RGB source-alpha blending and alpha MAX
- depth-write ON/OFF contract
- Z-buffer and W-buffer variants
- color, attribute and stencil readback

ROM rendering remains on the Software correctness baseline. Textured shadow polygons and live `RenderPolygonRAM` integration are deferred.

## Harness

```text
--melonprime-vulkan-shadow-pipeline-test <json>
```

The harness creates deterministic opaque surfaces, shadow-mask polygons and visible-shadow polygons. It validates:

1. mask depth failure sets bit 7
2. mask depth pass leaves bit 7 clear
3. lower opaque polygon IDs survive the mask stage
4. a visible shadow cannot shadow the same lower polygon ID
5. a different lower polygon ID receives blended shadow color
6. the final stencil value retains bit 7 and writes `0x40 | shadowPolyID`
7. W-buffer path produces the same two-stage behavior

Expected headline values:

```text
contract_version = 1
completed_iterations = 3
batch_count = 7
draw_count = 9
unique_pipeline_count >= 8
samples_matched = true
native_ds_polygon_raster_integrated = false
```

## Deferred

- textured shadow polygons
- live ROM renderer integration
- toon and highlight
- final fog and edge passes
- line polygons and Better Polygons
- `GetLine()` readback integration
