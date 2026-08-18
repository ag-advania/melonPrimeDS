/*
    Copyright 2016-2026 melonDS team

    Per-frame latency capture for Vulkan present-pacing A/B measurement.

    This exists because the only existing presentation telemetry is developer
    telemetry, and a developer build is not what an A/B run may measure: with
    MELONPRIME_ENABLE_DEVELOPER_FEATURES the pacer requests timestamps for every
    present stage the surface offers, which changes driver work and timing-queue
    pressure relative to a shipping build. Comparing latency between policies
    therefore needs an instrument that is independent of developer features and
    that does not itself change what is rendered or presented.

    Rules this module keeps:

      * Default OFF. Without MELONPRIME_VULKAN_LATENCY_CAPTURE every method
        below is an empty inline function and the class holds no state.
      * Turning it ON changes logging only. It must never influence present
        stage queries, pacing decisions, swapchain setup or frame content -- an
        A/B build has to present exactly like the build it is measuring.
      * No file I/O and no allocation on the frame path. Samples land in a ring
        that is sized once; the CSV is written when the run ends.
*/

#ifndef VULKAN_PRESENT_LATENCY_CAPTURE_H
#define VULKAN_PRESENT_LATENCY_CAPTURE_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <cstddef>
#include <string>
#include <vector>

#include "VulkanPresentPacer.h"

namespace melonDS
{

#if defined(MELONPRIME_VULKAN_LATENCY_CAPTURE)

// One measured frame. Host timestamps are microseconds from a monotonic clock
// zeroed at capture start, so they are comparable within a run and meaningless
// across runs -- which is the correct property for latency deltas.
struct VulkanLatencySample
{
    u64 SampleIndex = 0;
    u64 LogicalFrameId = 0;
    u64 ReflexPresentId = 0;
    u64 PresentId = 0;

    u32 SwapchainImageIndex = 0;
    u32 FrameSlot = 0;
    u32 SwapchainImageCount = 0;
    u32 DistinctSwapchainImagesAcquiredSinceRecreate = 0;
    u64 LogicalFramesSinceLastAcceptedPresent = 0;
    // FrameRing retirement proxy; this is not the driver's internal WSI queue
    // depth and must not be presented as one.
    u64 UnretiredFrameRingSubmissionDepth = 0;
    u64 AcquireWaitUs = 0;

    u64 InputSampleUs = 0;
    u64 SimStartUs = 0;
    u64 SimEndUs = 0;
    u64 RenderSubmitStartUs = 0;
    u64 RenderSubmitEndUs = 0;
    u64 PresentStartUs = 0;
    u64 PresentEndUs = 0;
    // Only NVIDIA Reflex reports GPU-side render bounds. Zero means "not
    // measured on this path", never "measured as zero".
    u64 GpuRenderStartUs = 0;
    u64 GpuRenderEndUs = 0;

    int ReflexMode = 0;
    VulkanPresentPacer::StateSnapshot Pacing{};
};

// Fixed-capacity capture buffer for one measurement run.
class VulkanPresentLatencyCapture
{
public:
    // ~9 minutes at 60 FPS. Reserved once, in the constructor: a measurement
    // run must not allocate while it is measuring.
    static constexpr std::size_t Capacity = 32768;

    VulkanPresentLatencyCapture();
    ~VulkanPresentLatencyCapture();

    [[nodiscard]] bool IsEnabled() const noexcept { return Enabled; }

    // Marker points. Each records a host timestamp into the frame being built.
    void MarkInputSample() noexcept;
    void MarkSimulationStart() noexcept;
    void MarkSimulationEnd() noexcept;
    void MarkRenderSubmitStart() noexcept;
    void MarkRenderSubmitEnd() noexcept;
    void MarkPresentStart() noexcept;
    void MarkPresentEnd() noexcept;
    void MarkAcquireStart() noexcept;
    void MarkAcquireEnd() noexcept;

    void SetGpuRenderBounds(u64 startUs, u64 endUs) noexcept;
    void SetReflexMode(int mode) noexcept { Pending.ReflexMode = mode; }
    void SetFrameContext(
        u64 logicalFrameId,
        u64 reflexPresentId,
        u32 swapchainImageIndex,
        u32 frameSlot,
        u32 swapchainImageCount,
        u32 distinctSwapchainImagesAcquiredSinceRecreate,
        u64 logicalFramesSinceLastAcceptedPresent,
        u64 unretiredFrameRingSubmissionDepth) noexcept;

    // Closes the frame and stores it. Called once per accepted present.
    void Commit(u64 presentId, const VulkanPresentPacer::StateSnapshot& pacing) noexcept;

    // Writes the CSV and clears the buffer. Safe to call with nothing captured.
    bool Flush();

private:
    [[nodiscard]] u64 NowUs() const noexcept;

    bool Enabled = false;
    std::string RunId;
    std::string OutputPath;
    u64 StartTicks = 0;
    u64 NextSampleIndex = 0;
    u64 AcquireStartUs = 0;
    bool AcquireOpen = false;
    VulkanLatencySample Pending{};
    std::vector<VulkanLatencySample> Samples;
};

#else // !MELONPRIME_VULKAN_LATENCY_CAPTURE

// Disabled build: every call compiles away, and the object is stateless so an
// A/B-capable binary and a normal one differ by nothing but the logging code.
class VulkanPresentLatencyCapture
{
public:
    [[nodiscard]] constexpr bool IsEnabled() const noexcept { return false; }

    void MarkInputSample() noexcept {}
    void MarkSimulationStart() noexcept {}
    void MarkSimulationEnd() noexcept {}
    void MarkRenderSubmitStart() noexcept {}
    void MarkRenderSubmitEnd() noexcept {}
    void MarkPresentStart() noexcept {}
    void MarkPresentEnd() noexcept {}
    void MarkAcquireStart() noexcept {}
    void MarkAcquireEnd() noexcept {}

    void SetGpuRenderBounds(u64, u64) noexcept {}
    void SetReflexMode(int) noexcept {}
    void SetFrameContext(u64, u64, u32, u32, u32, u32, u64, u64) noexcept {}
    void Commit(u64, const VulkanPresentPacer::StateSnapshot&) noexcept {}
    bool Flush() { return true; }
};

#endif // MELONPRIME_VULKAN_LATENCY_CAPTURE

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_PRESENT_LATENCY_CAPTURE_H
