# Vulkan Phase 7.3 clear-bitmap verification

**Status:** Patch applied; Windows Vulkan build, shader regeneration and GPU clear-bitmap bootstrap pending.

**Build status:** `PENDING_WINDOWS_BUILD`

**Shader generation status:** `PENDING_CLEAR_BITMAP_SPIRV`

**Clear-bitmap runtime status:** `PENDING_SCALE_1_2_4_REPEAT_READBACK`

## Scope

This continuation implements the isolated Phase 7.3 clear-bitmap path while the
actual ROM renderer continues to use the Software 2D/3D/capture/final-image
correctness baseline.

Implemented contract and bootstrap coverage:

- DS clear-bitmap enable bit and X/Y offset decode
- opaque polygon ID decode
- VRAM texture slots 2 and 3 data conversion
- RGB5 to DS RGB6 expansion and alpha5 preservation
- depth15 to depth24 expansion and fog-bit packing
- color source image: `VK_FORMAT_R8G8B8A8_UINT`
- depth source image: `VK_FORMAT_R32_UINT`
- nearest `REPEAT` sampler
- push-constant offset and polygon ID layout
- Vulkan fragment depth output
- stencil replacement with `0xFF`
- color and attribute MRT readback
- wrap-boundary samples at scale 1, 2 and 4
- dirty-state retention while clear bitmap is disabled
- dirty-state consumption only when enabled

## Intentionally deferred

- wiring real `GPU.VRAMFlat_Texture[0x40000/0x60000]` into the ROM renderer
- persistent per-frame upload rings
- texture-cache-driven dirty notifications
- Phase 7.4 packed polygon vertex upload
- opaque, translucent, shadow, fog and edge polygon passes
- native `Renderer3D::GetLine()` integration
- native 2D, capture and GPU-resident final composition

## Acceptance

The no-ROM harness must report:

- all requested iterations completed
- scales 1, 2 and 4 tested
- core decode and dirty-tracker contract passed
- integer source textures uploaded
- repeat sampler and offset push constants used
- Vulkan draw submitted
- color and attribute readback completed
- all expected samples matched
- at least one wrapped sample validated
- depth and stencil writes submitted
- Software game-rendering baseline preserved
- native DS polygon raster still reported as not integrated

The shader tool must also pass:

```text
python tools/vulkan/generate_spirv.py --self-test-line-endings
python tools/vulkan/generate_spirv.py --check --allow-missing-compiler
```
