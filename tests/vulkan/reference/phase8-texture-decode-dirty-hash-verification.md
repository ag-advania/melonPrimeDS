# Phase 8.2 Vulkan texture decode and dirty hash verification

## Scope

This phase adds the CPU decode and invalidation boundary used by
`TexcacheVulkan` before image upload.

Covered Nintendo DS texture formats:

1. A3I5
2. 2-bit palette
3. 4-bit palette
4. 8-bit palette
5. 4x4 compressed
6. A5I3
7. direct color

The output is the same packed RGB6A5 representation consumed by the Vulkan
integer-texture shaders. Texture and palette ranges are hashed independently.
Dirty filtering uses the core 512-byte VRAM dirty granularity.

## Harness

```text
melonDS.exe --melonprime-vulkan-texture-decode-test texture-decode.json
```

The harness uses deterministic 8x8 inputs for all seven formats. It checks:

- exact decoded texel digests and selected texels
- exact texture, auxiliary-texture and palette footprints
- direct-color VRAM wrapping across the end of the 512 KiB texture mirror
- relevant texture and palette hash changes
- unrelated memory changes not invalidating an entry
- compressed auxiliary-data dirty-page invalidation
- direct-color textures ignoring palette-only dirty pages

## Deferred

- persistent upload ring
- display-capture-backed textures
- savestate synchronization
- ROM Renderer3D integration
