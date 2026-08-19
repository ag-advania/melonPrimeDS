/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef VULKAN_SYNC_H
#define VULKAN_SYNC_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <mutex>
#include <vector>

#include "VulkanCommon.h"
#include "VulkanDevice.h"
#include "VulkanLoader.h"
#include "VulkanPresenterFrameBudget.h"

namespace melonDS::Vk
{

// Frames the CPU is allowed to run ahead of the GPU.
//
// Two, deliberately. This is a 60 Hz emulator whose emulation thread produces
// exactly one frame of work per DS frame: a third in-flight frame would add a
// full frame of input-to-photon latency (the thing MelonPrime's Reflex /
// Anti-Lag integration exists to remove) in exchange for throughput this
// workload does not need.
//
// This number is independent of the swapchain image count and must not be
// conflated with it:
//   * frames in flight  = how many sets of per-frame CPU-side resources
//                         (command buffer, descriptors, staging space) exist,
//                         and therefore how deep the CPU may run ahead;
//   * swapchain images  = how many presentable images the surface owns, chosen
//                         from VkSurfaceCapabilitiesKHR::minImageCount by the
//                         presenter (typically 3 for FIFO).
// A swapchain with more images than this simply means some images sit idle;
// a swapchain with fewer means vkAcquireNextImageKHR blocks earlier. Neither
// requires changing this value.
inline constexpr u32 FramesInFlight = 2;

// The frame number used for deferred destruction depends on which side of the
// recording boundary the invalidation happens.  AbsoluteFrame is the number
// assigned to the next frame that will be recorded, so it is deliberately not
// used as a lifetime tag while the ring is between submissions.
struct VulkanResourceRetireFrameState
{
    u64 CompletedFrame = 0;
    u64 LastSubmittedFrame = 0;
    u64 CurrentRecordingFrame = 0;
    bool HasSubmittedFrame = false;
    bool Recording = false;
};

[[nodiscard]] constexpr u64 VulkanResourceRetireFrame(
    const VulkanResourceRetireFrameState& state) noexcept
{
    if (state.Recording)
        return state.CurrentRecordingFrame;
    if (state.HasSubmittedFrame)
        return state.LastSubmittedFrame;
    return state.CompletedFrame;
}


// Object kinds the deferred destruction queue can retire. Every one of these is
// a non-dispatchable handle, which is what lets the queue store them uniformly
// as u64 and hand them back through Vk::FromObjectHandle<T>().
enum class DeferredObject : u32
{
    Buffer,
    BufferView,
    Image,
    ImageView,
    Sampler,
    DeviceMemory,
    Pipeline,
    PipelineLayout,
    DescriptorPool,
    DescriptorSetLayout,
    ShaderModule,
    Framebuffer,
    RenderPass,
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    QueryPool,
#endif
    CommandPool,
    Semaphore,
    Fence,
    SwapchainKHR,
};


// The single chokepoint for destroying device objects.
//
// Vulkan makes the application responsible for not destroying an object while
// the GPU may still read it, and "may still read it" is decided by fences, not
// by C++ lifetime. So nothing in this backend calls vkDestroy* on a resource
// directly once rendering has started: it enqueues the handle together with the
// absolute frame number that last referenced it, and Collect() retires it only
// after that frame's fence has signalled.
//
// Enqueue() may be called from the emulation thread (renderer resources) and
// from the GUI thread (swapchain and surface-sized resources), so the queue
// takes a lock. Collect() runs once per frame; the lock cost is far below the
// noise floor of a frame.
class DeferredDestroyQueue
{
public:
    DeferredDestroyQueue() = default;
    ~DeferredDestroyQueue();

    DeferredDestroyQueue(const DeferredDestroyQueue&) = delete;
    DeferredDestroyQueue& operator=(const DeferredDestroyQueue&) = delete;

    void Init(const DeviceDispatch& fns, VkDevice device) noexcept;

    // Queues `handle` for destruction once frame `lastUsedFrame` has retired.
    // Callers must pass the last frame that may legally reference the object;
    // the scheduler's next-frame counter is not a lifetime tag.
    template <typename HandleT>
    void Enqueue(DeferredObject type, HandleT handle, u64 lastUsedFrame)
    {
        if (handle == VK_NULL_HANDLE)
            return;
        EnqueueRaw(type, ToObjectHandle(handle), lastUsedFrame);
    }

    // Destroys everything whose last-used frame is <= `completedFrame`.
    void Collect(u64 completedFrame);

    // Shutdown path only. The caller must already have waited for the device to
    // go idle -- this destroys pending objects regardless of their frame number.
    void DestroyAll();

    [[nodiscard]] size_t GetPendingCount() const;

private:
    struct Entry
    {
        DeferredObject Type;
        u64 Handle;
        u64 LastUsedFrame;
    };

    void EnqueueRaw(DeferredObject type, u64 handle, u64 lastUsedFrame);
    void DestroyEntry(const Entry& entry) const noexcept;

    mutable std::mutex Mutex;
    const DeviceDispatch* Fns = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    std::vector<Entry> Pending;
};


// Everything one in-flight frame owns.
struct FrameContext
{
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;

    // Signalled by the submission for this frame; waited on before the slot is
    // reused. This is the fence the deferred destruction queue keys off.
    VkFence InFlightFence = VK_NULL_HANDLE;

    // Signalled by vkAcquireNextImageKHR, waited on by the submission. Safe to
    // keep per frame slot because the acquire for a slot cannot be issued until
    // the slot's fence has signalled.
    VkSemaphore ImageAvailable = VK_NULL_HANDLE;

    // Signalled by the submission, waited on by vkQueuePresentKHR.
    //
    // Note for the presenter: a per-frame-slot semaphore is only sufficient
    // while a slot's previous present has completed before the slot is reused.
    // The frame fence does not cover the present operation itself, so a
    // presenter that keeps more swapchain images than frames in flight must own
    // an additional per-swapchain-image semaphore rather than reusing this one.
    VkSemaphore RenderFinished = VK_NULL_HANDLE;

    // Absolute frame number this slot last carried; 0 before first use.
    u64 SubmittedFrame = 0;
    bool HasPendingSubmission = false;
    bool Recording = false;

    // Optional renderer telemetry. The query pool and flag are absent from a
    // shipping FrameContext, so the normal synchronization layout contains no
    // performance-only state.
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    VkQueryPool TimestampQueryPool = VK_NULL_HANDLE;
    bool TimestampQueriesEnabled = false;
    // Query results belong to the completed use of this slot. Keep them
    // readable until the first timestamp of the new recording is emitted;
    // resetting the pool in BeginFrame would make that reset command pending
    // while the CPU tries to read the previous submission.
    bool TimestampResultsAvailable = false;
    bool TimestampQueriesReset = false;
    bool TimestampQueriesWritten = false;
#endif
};

enum class FrameWaitResult : u32
{
    Ready = 0,
    Timeout,
    Error,
};

enum class FrameBeginResult : u32
{
    Ready = 0,
    Busy,
    Error,
};


// The frames-in-flight ring: per-frame command buffers, fences and semaphores,
// plus the deferred destruction queue keyed to the same frame numbering.
//
// The only blocking call in the normal frame path is the vkWaitForFences at the
// top of BeginFrame(), which throttles the CPU to `framesInFlight` frames ahead
// of the GPU. vkDeviceWaitIdle / vkQueueWaitIdle appear nowhere in this class
// except WaitIdle(), which is restricted to teardown/resource-recreation
// boundaries and never used in the steady-state frame path.
class FrameRing
{
public:
    FrameRing() = default;
    ~FrameRing();

    FrameRing(const FrameRing&) = delete;
    FrameRing& operator=(const FrameRing&) = delete;

    bool Create(const VulkanDevice& device, u32 queueFamily, u32 framesInFlight = FramesInFlight);

    // Waits for every in-flight frame, retires the destruction queue and
    // destroys the ring. Safe to call more than once.
    void Destroy();

    // Resource creation is a correctness contract, not just a vector-size
    // contract. Keep the hot-path validity check to one cached boolean; the
    // per-slot handle audit runs once at the end of Create().
    [[nodiscard]] bool IsValid() const noexcept { return CoreResourcesReady; }

    [[nodiscard]] bool HasValidCoreFrameResources() const noexcept;

    // Waits for the slot about to be reused, retires everything that frame was
    // holding, resets the command pool and opens the command buffer.
    //
    // `recordRasterBegin` is reserved for the main 3D renderer entry point.
    // Presenter/capture command rings use the default so their waits do not
    // contaminate the renderer's RasterBeginWait telemetry.
    //
    // Returns nullptr on failure (device lost, out of memory); the caller must
    // not record anything in that case.
    FrameContext* BeginFrame(bool recordRasterBegin = false);

    // Strict presenter pacing waits the most recently submitted presenter
    // frame as a bounded pre-input back-pressure point. This is deliberately
    // not the next reusable ring slot: on a two-slot ring that slot is usually
    // two submissions old and therefore does not enforce a one-frame budget.
    // The fence and deferred resources are retired, but the fence and command
    // pool are not reset; BeginFrame() performs those recording-boundary
    // operations immediately afterward.
    FrameWaitResult WaitForLatestSubmittedFrame(u64 timeoutNanoseconds);

    // Non-blocking counterpart for work that is allowed to drop a frame. If
    // the next slot's fence is not signalled, returns nullptr immediately
    // without resetting or changing the ring. `result` distinguishes a busy
    // slot from a device/error result so callers do not hide real failures as
    // intentional latency skips.
    FrameContext* TryBeginFrame(FrameBeginResult* result = nullptr);

    // Ends and submits the open command buffer.
    //
    // `waitSemaphore` / `waitStageMask` describe the acquire dependency and may
    // be VK_NULL_HANDLE / 0 for a frame that does not present.
    // `signalSemaphore` may be VK_NULL_HANDLE likewise.
    //
    // `submitPNext` is chained onto VkSubmitInfo::pNext and must stay alive for
    // the duration of the call. It exists for VkLatencySubmissionPresentIdNV:
    // VK_NV_low_latency2 correlates a submission with its present by tagging the
    // submit itself, so there is no way to attach it from outside this function.
    // Null -- the default -- produces exactly the submission this ring made
    // before the parameter existed.
    bool SubmitFrame(
        VkQueue queue,
        VkSemaphore waitSemaphore,
        VkPipelineStageFlags waitStageMask,
        VkSemaphore signalSemaphore,
        const void* submitPNext = nullptr);

    // Convenience for a frame that neither waits on an acquire nor signals a
    // present: the compute-only path used when the renderer produces a frame
    // the presenter will read back rather than present directly.
    bool SubmitFrame(VkQueue queue) { return SubmitFrame(queue, VK_NULL_HANDLE, 0, VK_NULL_HANDLE); }

    [[nodiscard]] FrameContext* GetCurrentFrame() noexcept;
    [[nodiscard]] VkCommandBuffer GetCommandBuffer() const noexcept;

    // Slot index, 0 .. framesInFlight-1. This is the index descriptor sets and
    // per-frame buffers are addressed by.
    [[nodiscard]] u32 GetFrameIndex() const noexcept { return CurrentIndex; }
    [[nodiscard]] u32 GetFramesInFlight() const noexcept { return static_cast<u32>(Frames.size()); }

    [[nodiscard]] u32 GetNextFrameIndex() const noexcept
    {
        if (Frames.empty())
            return 0;
        return VulkanFrameRingIndexForAbsoluteFrame(
            AbsoluteFrame, static_cast<u32>(Frames.size()));
    }

    [[nodiscard]] bool LatestSubmittedFrameHasPendingSubmission() const noexcept
    {
        return HasSubmittedFrame && !Frames.empty()
            && Frames[LastSubmittedIndex].HasPendingSubmission;
    }

    [[nodiscard]] bool NextFrameHasPendingSubmission() const noexcept
    {
        return !Frames.empty() && Frames[GetNextFrameIndex()].HasPendingSubmission;
    }

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    // Renderer GPU timestamp seam. Query indices are owned by the caller so
    // the same FrameRing can measure raster, compositor, capture, or presenter
    // command spans without putting stage knowledge into synchronization code.
    [[nodiscard]] bool HasTimestampQueries() const noexcept
    {
        return TimestampPeriodNs > 0.0f;
    }
    [[nodiscard]] float GetTimestampPeriodNs() const noexcept { return TimestampPeriodNs; }
    void WriteTimestamp(VkPipelineStageFlagBits stage, u32 queryIndex) noexcept;
    [[nodiscard]] bool ReadCurrentFrameTimestamps(
        u32 firstQuery, u32 queryCount, u64* values) const noexcept;
#else
    [[nodiscard]] inline constexpr bool HasTimestampQueries() const noexcept
    {
        return false;
    }
    [[nodiscard]] inline constexpr float GetTimestampPeriodNs() const noexcept
    {
        return 0.0f;
    }
    inline constexpr void WriteTimestamp(VkPipelineStageFlagBits, u32) noexcept {}
    [[nodiscard]] inline constexpr bool ReadCurrentFrameTimestamps(
        u32, u32, u64*) const noexcept
    {
        return false;
    }
#endif

    // Monotonic scheduler counter: the absolute number assigned to the next
    // recording frame. Use the explicit lifetime helpers below for deferred
    // destruction tags.
    [[nodiscard]] u64 GetAbsoluteFrame() const noexcept { return AbsoluteFrame; }

    // Number assigned to the frame whose command buffer is currently open, or
    // zero when the ring is not recording.
    [[nodiscard]] u64 GetCurrentRecordingFrameNumber() const noexcept;

    // Number carried by the most recently submitted frame, or zero before the
    // first successful submission.
    [[nodiscard]] u64 GetLastSubmittedFrameNumber() const noexcept;

    // Last frame that may legally reference a resource invalidated at this
    // point in the ring lifecycle. This is the only tag RetireEntry should use.
    [[nodiscard]] u64 GetResourceRetireFrame() const noexcept;

    // Highest absolute frame known to have completed on the GPU.
    [[nodiscard]] u64 GetCompletedFrame() const noexcept { return CompletedFrame; }

    [[nodiscard]] DeferredDestroyQueue& GetDestroyQueue() noexcept { return DestroyQueue; }

    // Teardown / resource-recreation helper. This is one of the three places
    // the backend may call vkDeviceWaitIdle; it is not used in the steady-state
    // frame path. If called after a frame has been admitted, the open recording
    // frame remains excluded from CompletedFrame bookkeeping until it submits.
    void WaitIdle();

private:
    FrameContext* BeginFrameInternal(
        bool waitForSlot,
        bool recordRasterBegin,
        FrameBeginResult* result = nullptr);
    void DestroyFrames();

    const VulkanDevice* Device = nullptr;
    u32 QueueFamily = 0;
    std::vector<FrameContext> Frames;
    DeferredDestroyQueue DestroyQueue;

    bool CoreResourcesReady = false;
    u32 CurrentIndex = 0;
    u64 AbsoluteFrame = 1;      // 1-based so that 0 means "never used"
    u64 CompletedFrame = 0;
    u32 LastSubmittedIndex = 0;
    bool HasSubmittedFrame = false;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    float TimestampPeriodNs = 0.0f;
#endif
};

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_SYNC_H
