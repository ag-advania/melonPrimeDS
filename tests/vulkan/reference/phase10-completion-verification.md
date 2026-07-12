# Phase 10 Completion Verification

Marker: `MELONPRIME_VULKAN_PHASE10_COMPLETION_BOOTSTRAP_V1`

Phase 10 validates a bounded three-slot Vulkan output ring and direct resident-image sampling by two presenter consumers. The harness publishes a two-layer integer source image in `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`, acquires independent window leases, waits for producer completion through a timeline semaphore or fence fallback, and renders two differently arranged presenter outputs without copying source pixels through the CPU presenter path.

The harness also validates:

- no output-slot reuse while either presenter retains a lease;
- slot reuse after all presenter references are released;
- generation invalidation for scale or renderer changes;
- bounded frame-drop behavior when all three slots are occupied;
- deterministic window-close lease release;
- image and sample parity for both presenter windows;
- complete create, submit, readback and destroy cycles repeated three times.

The verification harness is a Phase 10 subsystem acceptance candidate. It does not activate the ROM-visible Vulkan renderer, native DS polygon rasterizer, or Vulkan Compute renderer. Those facts are emitted explicitly in the JSON report.
