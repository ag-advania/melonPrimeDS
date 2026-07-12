# Phase 7.10A Vulkan texture-sampling bootstrap verification

## Scope

This phase validates the texture-facing Vulkan contract without changing game rendering.
A deterministic 4x4 DS RGB6A5 texture is uploaded to `VK_FORMAT_R8G8B8A8_UINT` and
sampled by a compute pipeline through nearest clamp, repeat, and mirrored-repeat samplers.
The same dispatch validates raw sampling, modulate, decal, textured toon, and textured
highlight color math against the CPU reference in `GPU3D_Vulkan.cpp`.

## Acceptance

- Three create/upload/dispatch/readback/destroy iterations pass.
- Clamp, repeat, and mirror select different deterministic texels.
- Modulate and decal match the OpenGL Classic combiner order.
- Textured toon replaces vertex RGB before modulation.
- Textured highlight performs grayscale modulation and then adds the toon-table color.
- GPU output matches byte-quantized CPU reference values.
- Existing untextured opaque, translucent, shadow, and toon/highlight harnesses still pass.

## Deferred

Texture cache ownership, capture textures, polygon shader descriptor binding, ROM rendering,
fog/edge final passes, and `GetLine()` integration remain deferred.
