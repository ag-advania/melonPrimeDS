# Phase 8.1 Vulkan texture-cache residency verification

This developer harness validates the first `TexcacheVulkan` lifecycle boundary.
It uses the DS texture-key normalization rules, individual Vulkan images and a
cached descriptor set per texture/sampler pair. Descriptor indexing is not used.

Required cases:

- sampling and texcoord-generation bits are excluded from the texture identity
- direct-color textures ignore the palette base
- all nine S/T clamp, repeat and mirror sampler combinations are created
- repeated and non-adjacent requests reuse a resident image
- an unchanged texture is not uploaded again
- a content-generation change invalidates and reuploads the existing image
- ordered batch decisions preserve source order
- descriptor sets are reused for identical texture/sampler pairs
- GPU samples before and after invalidation match the CPU reference

Display capture, savestate synchronization, upload-ring wrap handling and ROM
Renderer3D integration remain later Phase 8 work. Software remains the runtime
correctness baseline.
