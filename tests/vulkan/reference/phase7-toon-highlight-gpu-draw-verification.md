# Phase 7.9D Toon / Highlight GPU Draw Verification

## Scope

This phase replaces the Phase 7.9C host mirror with a real Vulkan runtime path:

- two 528-byte uniform buffers
- descriptor set layout at set 0 / binding 0
- descriptor pool and two descriptor sets
- `vkUpdateDescriptorSets`
- pipeline layout compatibility
- `vkCmdBindDescriptorSets`
- opaque toon and highlight draws
- translucent toon and highlight draws
- device-local packed-vertex buffer
- color attachment transfer readback

The deterministic scene uses four non-overlapping triangles. The opaque samples
must match the table color and highlight sum. The translucent samples must also
match source-alpha RGB blending and MAX alpha blending.

## Deliberate limitations

- Texture sampling is not connected in this phase.
- ROM rendering still uses the Software correctness baseline.
- The production `VulkanRenderer` does not yet consume these pipelines.
- Native DS polygon rasterization remains reported as false.
