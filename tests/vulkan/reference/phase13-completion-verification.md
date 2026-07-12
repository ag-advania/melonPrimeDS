# Phase 13 completion verification

The developer harness verifies the complete Phase 13 stability contract:

- VSync ON and OFF policy
- VSync interval and frame-limit interaction
- Fast Forward and Slow Motion latest-frame drop
- audio-sync independence
- actual surface present-mode enumeration
- FIFO and selected low-latency swapchain creation
- process-local device-loss fallback without changing the saved renderer
- one-shot OSD policy
- stale Vulkan resource generation rejection
- identity-bound pipeline-cache cold and warm paths
- atomic pipeline-cache replacement
- required-pipeline prewarm and explicit missing-variant failure
- no pending-acquire deadlock and no stale output presentation

The runtime QVulkanWindow path remains FIFO because Qt exposes only FIFO there. Phase 13 therefore prevents acquire blocking by allowing only one pending presentation request and always composes the latest completed output when the request is serviced. The developer harness additionally validates direct swapchain creation with supported low-latency modes.
