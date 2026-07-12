# Vulkan Phase 7.6 opaque pipeline verification

## Scope

This subphase consumes the Phase 7.4 packed vertex/index payload and the Phase
7.5 adjacent-only batch plan in a real Vulkan graphics pipeline. It is a
no-ROM deterministic correctness bootstrap, not the final `Renderer3D`
integration.

Implemented and verified by the harness:

- device-local packed vertex and `uint16` index buffers
- source-order-preserving indexed batch submission
- untextured opaque triangle pipeline
- Z-buffer and W-buffer shader variants
- `VK_COMPARE_OP_LESS` and `VK_COMPARE_OP_LESS_OR_EQUAL`
- depth write
- opaque alpha reject at `30.5 / 31.0`
- two color attachments: color and DS attribute output
- dynamic stencil polygon-ID replacement
- color, attribute and stencil GPU readback
- repeated create/draw/readback/destroy lifecycle

The Phase 7.5 pipeline key is corrected to include stencil reference. Adjacent
polygons with different polygon IDs must not share one batch because one Vulkan
draw has one dynamic stencil reference.

## Harness

```text
--melonprime-vulkan-opaque-pipeline-test <json>
```

Expected top-level evidence:

```text
contract_version = 1
completed_iterations = 3
batch_count = 4
draw_count = 4
stencil_key_boundary_passed = true
z_less_pipeline_created = true
z_less_equal_pipeline_created = true
w_less_pipeline_created = true
color_readback_completed = true
attribute_readback_completed = true
stencil_readback_completed = true
samples_matched = true
```

Deterministic sample cases:

1. nearer red Z-buffer triangle rejects a later farther green triangle
2. equal-depth blue triangle using LEQUAL overwrites the first triangle
3. alpha-15 polygon is discarded and leaves clear color/attribute/stencil
4. W-buffer cyan triangle writes fragment depth and stencil polygon ID
5. untouched background remains the clear-plane value

## Intentionally deferred

- ROM `RenderPolygonRAM` submission from the live renderer
- texture cache and textured opaque modes
- decal, modulate, toon and highlight sampling
- translucent polygons
- shadow mask and visible shadow stages
- line polygon rasterization
- Better Polygons
- edge marking, fog and final pass
- `GetLine()` integration

Game rendering therefore remains on the Software correctness baseline and
`NativeVulkan3DImplemented` remains false.
