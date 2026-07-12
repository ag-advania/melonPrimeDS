# Phase 7.10B textured polygon verification

This developer harness validates real Vulkan graphics draws using the Phase 7 packed
vertex layout, a DS RGB6A5 integer texture, combined image samplers and the shared
528-byte toon/highlight UBO.

Required cases:

- opaque modulate with clamp-to-edge
- opaque decal with repeat
- textured toon with mirrored repeat
- textured highlight with clamp-to-edge
- translucent modulate blending
- translucent decal blending
- byte-tolerant color attachment readback over three create/draw/destroy iterations

The harness does not enable Vulkan for ROM rendering and does not claim texture-cache
integration. Software remains the correctness baseline.
