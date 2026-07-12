# Vulkan Phase 7.1～7.2 clear-plane continuation verification

**Status:** Patch applied; Windows build and GPU clear-plane bootstrap pending.
**CI shader metadata status:** `PENDING_CRLF_NORMALIZED_CHECK`
**Build status:** `PENDING_WINDOWS_BUILD`
**Clear-plane runtime status:** `PENDING_3_OF_3_CLEAR_READBACK`
**ROM runtime status:** `SOFTWARE_CORRECTNESS_BASELINE_RETAINED`

## CI fix

`tools/vulkan/generate_spirv.py` now hashes and compares text after normalizing CRLF and lone CR to LF. This keeps the committed LF lock authoritative while preventing Windows checkout line endings from being misreported as manifest or shader-source drift.

The existing CI command remains unchanged:

```text
python .\tools\vulkan\generate_spirv.py --check --allow-missing-compiler
```

A deterministic line-ending regression test is available:

```text
python .\tools\vulkan\generate_spirv.py --self-test-line-endings
```

The core CMake generator inventory now declares all committed generated headers and all three shader sources as outputs/dependencies.

## Phase 7.1～7.2 implemented scope

- Adds a typed native raster target contract for scale 1～16.
- Defines color, attribute and depth-stencil attachment formats/usages.
- Decodes `RenderClearAttr1` and `RenderClearAttr2` with the same bit layout used by the OpenGL Classic correctness path.
- Converts the OpenGL clear depth convention to Vulkan depth range `[0, 1]`.
- Builds three Vulkan `VkClearValue` entries for color, attributes and depth-stencil.
- Creates real native Vulkan color, attribute and depth-stencil images.
- Executes render-pass load-op clears without adding a new shader.
- Reads the color and attribute images back and checks first, center and last pixels.
- Repeats device/resource creation, clear, readback and destruction three times.
- Keeps ROM 2D/3D/capture/final output on the Software correctness baseline.

## Expected clear-plane harness contract

```json
{
  "passed": true,
  "completed_iterations": 3,
  "contract_passed": true,
  "clear_submitted": true,
  "color_readback_completed": true,
  "attribute_readback_completed": true,
  "color_samples_matched": true,
  "attribute_samples_matched": true,
  "depth_stencil_attachment_created": true,
  "depth_stencil_clear_submitted": true,
  "depth_stencil_readback_verified": false,
  "software_game_rendering_preserved": true,
  "native_ds_polygon_raster_integrated": false
}
```

## Deferred acceptance

- Depth-stencil clear is submitted to a real attachment, but direct depth/stencil readback is deferred.
- Clear bitmap, polygon upload, opaque/translucent/shadow passes, textures, edge/fog final pass and `GetLine()` remain later Phase 7 work.
- The renderer still returns Software CPU BGRA output for gameplay.
- Linux, macOS/MoltenVK, CI Vulkan runtime and validation-layer execution remain deferred.
