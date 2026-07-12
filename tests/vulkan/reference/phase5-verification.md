# Vulkan Phase 5 verification

**Status:** Implemented before the Phase 6 Windows patch.
**Source revision observed by this patch:** `9040e63728d8`
**Scope:** `RendererOutputKind::VulkanImage`, typed `VulkanRendererOutput`, shared move-only `RendererOutputLease`, frame serial/generation matching.

## Evidence preserved from the supplied Windows work log

- Default-OFF Windows build completed.
- Vulkan-ON Windows build completed.
- Force-disabled Vulkan build completed and excluded Vulkan presenter sources/strings.
- `--melonprime-output-lease-test` returned success for OFF, Vulkan-ON, and force-disabled configurations.
- The Vulkan-ON result propagated `frame_serial=42` and `generation=7`.
- A stale generation was rejected.
- The release callback ran exactly once after move construction, move assignment, and repeated `ReleaseNow()`.
- The Phase 4 Vulkan presenter capture remained functional.

## Source contract checked by the Phase 6 patch preflight

- `RendererOutputKind::VulkanImage` exists only inside the Vulkan build gate.
- `VulkanRendererOutput` carries image/view/format/extent/layout/layers/serial/generation/timeline metadata.
- `RendererOutput::MatchesProducerFrame()` rejects stale generations.
- `RendererOutputLease` is available to MelonPrime CPU/OpenGL/Metal/Vulkan outputs and remains move-only.
- `GPU::AcquireRendererOutputLease()` delegates to the active renderer.

## Deferred acceptance

- A real Vulkan GPU-image producer does not exist until later phases.
- Output-slot reuse and cross-queue timeline waiting cannot be runtime-validated in Phase 5 alone.
- Linux, macOS/MoltenVK, GitHub Actions, and validation-layer runs remain deferred.
