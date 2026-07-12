# Phase 7.9B Toon / Highlight Shader ABI verification

- Shared std140 set 0 binding 0 contract for opaque and translucent shaders.
- 32 vec4 toon entries followed by DispCnt, Mode, Textured and padding.
- C++ size 528 bytes with fixed offsets 512, 516 and 520.
- Z-buffer and W-buffer shader variants consume the same ABI.
- Actual GPU draw remains Phase 7.9C.
