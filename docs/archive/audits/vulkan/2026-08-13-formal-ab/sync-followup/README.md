# Targeted Sync follow-up

The follow-up was executed on the Debug Validation build with
`khronos_validation.validate_sync = true` while exercising the production
window and renderer-transition paths:

- resize x40, minimize/restore x20, fullscreen toggle x8;
- Software, OpenGL, OpenGL Compute, and DX12 renderer stress x20 each;
- swapchain rebuilds completed without `DEVICE_LOST`;
- all follow-up stdout/stderr logs reported zero `SYNC-HAZARD` findings and
  clean validation.

Primary evidence is under
[`../manual-phase1`](../manual-phase1), especially the `vk-manual-*.out.log`
and `.err.log` files.
