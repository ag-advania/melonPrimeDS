# Phase 7.9A toon/highlight contract verification

This step fixes the 32-entry DS toon-table decode and the CPU/shader-visible toon/highlight reference contract before native draw integration.

Validated: toon and highlight modes, textured and untextured reference paths, vertex alpha preservation, std140-compatible 528-byte config, shader manifest generation.

Not yet integrated: ROM Renderer3D draw submission and Vulkan texture cache.
