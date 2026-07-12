# Phase 9 Vulkan 2D / Final Composition Verification

Marker: `MELONPRIME_VULKAN_PHASE9_COMPLETION_CONTRACT_V1`

This phase adds a two-pass Vulkan 2D and final-composition subsystem candidate.

1. `phase9_2d.comp` composes BG, OBJ, window, mosaic, blend and the 3D layer into a two-layer native-resolution image.
2. `phase9_final.comp` applies per-scanline display mode, ScreenSwap, Master Brightness, VRAM/FIFO sources and scale into a two-layer GPU-resident final image.
3. The final image is published in `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` and debug-read back only in the developer harness.
4. Frame, 3D, 2D-final and capture serials must match before publication.

The harness runs create / dispatch / publish / debug-readback / destroy three times and compares the complete 2x top-and-bottom output against the CPU reference.

ROM-visible Vulkan 2D activation and the zero-copy presenter remain deliberately disabled. Phase 10 owns presenter ring and multi-window leasing.
