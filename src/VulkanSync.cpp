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

#include "VulkanSync.h"
#include "VulkanPerf.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <cstdio>

namespace melonDS::Vk
{

namespace
{

// One second. Long enough that no legitimate normal frame ever reaches it,
// short enough that a hung GPU produces a log line instead of a frozen process.
// Strict presenter pacing supplies its own frame-budget timeout instead.
constexpr u64 DefaultFenceTimeoutNanoseconds = 1000ull * 1000ull * 1000ull;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
constexpr u32 TimestampQueryCount = 10;
#endif

} // namespace


// ---------------------------------------------------------------------------
// DeferredDestroyQueue
// ---------------------------------------------------------------------------

DeferredDestroyQueue::~DeferredDestroyQueue()
{
    // Anything still pending here was never collected, which means the owner
    // skipped DestroyAll(). Destroying it now is still correct only if the
    // device is idle; the owner (FrameRing) guarantees that ordering.
    DestroyAll();
}

void DeferredDestroyQueue::Init(const DeviceDispatch& fns, VkDevice device) noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    Fns = &fns;
    Device = device;
}

void DeferredDestroyQueue::EnqueueRaw(DeferredObject type, u64 handle, u64 lastUsedFrame)
{
    std::lock_guard<std::mutex> lock(Mutex);

    if (Device == VK_NULL_HANDLE || !Fns)
    {
        // No device to destroy against. Dropping the handle would leak, so this
        // is loud: it means Enqueue() ran after the device was torn down.
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] deferred destroy of object type %u dropped: no device bound\n",
            static_cast<unsigned>(type));
        return;
    }

    Pending.push_back(Entry{type, handle, lastUsedFrame});
}

void DeferredDestroyQueue::DestroyEntry(const Entry& entry) const noexcept
{
    switch (entry.Type)
    {
    case DeferredObject::Buffer:
        Fns->DestroyBuffer(Device, FromObjectHandle<VkBuffer>(entry.Handle), nullptr);
        break;
    case DeferredObject::BufferView:
        Fns->DestroyBufferView(Device, FromObjectHandle<VkBufferView>(entry.Handle), nullptr);
        break;
    case DeferredObject::Image:
        Fns->DestroyImage(Device, FromObjectHandle<VkImage>(entry.Handle), nullptr);
        break;
    case DeferredObject::ImageView:
        Fns->DestroyImageView(Device, FromObjectHandle<VkImageView>(entry.Handle), nullptr);
        break;
    case DeferredObject::Sampler:
        Fns->DestroySampler(Device, FromObjectHandle<VkSampler>(entry.Handle), nullptr);
        break;
    case DeferredObject::DeviceMemory:
        // Freed after the buffer/image bound to it, which the enqueue order
        // guarantees: entries with an equal frame number retire in insertion
        // order because Collect() walks the vector front to back.
        Fns->FreeMemory(Device, FromObjectHandle<VkDeviceMemory>(entry.Handle), nullptr);
        break;
    case DeferredObject::Pipeline:
        Fns->DestroyPipeline(Device, FromObjectHandle<VkPipeline>(entry.Handle), nullptr);
        break;
    case DeferredObject::PipelineLayout:
        Fns->DestroyPipelineLayout(Device, FromObjectHandle<VkPipelineLayout>(entry.Handle), nullptr);
        break;
    case DeferredObject::DescriptorPool:
        Fns->DestroyDescriptorPool(Device, FromObjectHandle<VkDescriptorPool>(entry.Handle), nullptr);
        break;
    case DeferredObject::DescriptorSetLayout:
        Fns->DestroyDescriptorSetLayout(Device, FromObjectHandle<VkDescriptorSetLayout>(entry.Handle), nullptr);
        break;
    case DeferredObject::ShaderModule:
        Fns->DestroyShaderModule(Device, FromObjectHandle<VkShaderModule>(entry.Handle), nullptr);
        break;
    case DeferredObject::Framebuffer:
        Fns->DestroyFramebuffer(Device, FromObjectHandle<VkFramebuffer>(entry.Handle), nullptr);
        break;
    case DeferredObject::RenderPass:
        Fns->DestroyRenderPass(Device, FromObjectHandle<VkRenderPass>(entry.Handle), nullptr);
        break;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    case DeferredObject::QueryPool:
        Fns->DestroyQueryPool(Device, FromObjectHandle<VkQueryPool>(entry.Handle), nullptr);
        break;
#endif
    case DeferredObject::CommandPool:
        Fns->DestroyCommandPool(Device, FromObjectHandle<VkCommandPool>(entry.Handle), nullptr);
        break;
    case DeferredObject::Semaphore:
        Fns->DestroySemaphore(Device, FromObjectHandle<VkSemaphore>(entry.Handle), nullptr);
        break;
    case DeferredObject::Fence:
        Fns->DestroyFence(Device, FromObjectHandle<VkFence>(entry.Handle), nullptr);
        break;
    case DeferredObject::SwapchainKHR:
        // Only reachable when VK_KHR_swapchain was enabled; the presenter is
        // the only enqueuer and it cannot exist without the extension.
        if (Fns->DestroySwapchainKHR)
            Fns->DestroySwapchainKHR(Device, FromObjectHandle<VkSwapchainKHR>(entry.Handle), nullptr);
        break;
    }
}

void DeferredDestroyQueue::Collect(u64 completedFrame)
{
    std::lock_guard<std::mutex> lock(Mutex);

    if (Pending.empty() || Device == VK_NULL_HANDLE || !Fns)
        return;

    // Front-to-back so that objects enqueued earlier in the same frame (the
    // buffer before its memory) are destroyed in that same order.
    size_t writeIndex = 0;
    for (size_t i = 0; i < Pending.size(); i++)
    {
        if (Pending[i].LastUsedFrame <= completedFrame)
        {
            DestroyEntry(Pending[i]);
        }
        else
        {
            if (writeIndex != i)
                Pending[writeIndex] = Pending[i];
            writeIndex++;
        }
    }
    Pending.resize(writeIndex);
}

void DeferredDestroyQueue::DestroyAll()
{
    std::lock_guard<std::mutex> lock(Mutex);

    if (Device == VK_NULL_HANDLE || !Fns)
    {
        Pending.clear();
        return;
    }

    for (const Entry& entry : Pending)
        DestroyEntry(entry);

    Pending.clear();
}

size_t DeferredDestroyQueue::GetPendingCount() const
{
    std::lock_guard<std::mutex> lock(Mutex);
    return Pending.size();
}


// ---------------------------------------------------------------------------
// FrameRing
// ---------------------------------------------------------------------------

FrameRing::~FrameRing()
{
    Destroy();
}

bool FrameRing::Create(const VulkanDevice& device, u32 queueFamily, u32 framesInFlight)
{
    Destroy();

    if (!device.IsValid() || framesInFlight == 0)
        return false;

    Device = &device;
    QueueFamily = queueFamily;
    CurrentIndex = 0;
    AbsoluteFrame = 1;
    CompletedFrame = 0;
    LastSubmittedIndex = 0;
    HasSubmittedFrame = false;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    TimestampPeriodNs = 0.0f;
#endif

    DestroyQueue.Init(device.Fns(), device.GetHandle());

    const DeviceDispatch& fns = device.Fns();
    VkDevice handle = device.GetHandle();

    Frames.resize(framesInFlight);

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const VkPhysicalDeviceLimits& limits = device.GetLimits();
    const bool timestampSupport = VulkanPerf::IsEnabled()
        && limits.timestampComputeAndGraphics == VK_TRUE
        && limits.timestampPeriod > 0.0f
        && fns.CreateQueryPool != nullptr
        && fns.DestroyQueryPool != nullptr
        && fns.CmdResetQueryPool != nullptr
        && fns.CmdWriteTimestamp != nullptr
        && fns.GetQueryPoolResults != nullptr;
    bool timestampPoolsCreated = timestampSupport;
    if (timestampSupport)
        TimestampPeriodNs = limits.timestampPeriod;
#endif

    for (u32 i = 0; i < framesInFlight; i++)
    {
        FrameContext& frame = Frames[i];

        // One pool per frame slot, reset wholesale in BeginFrame().
        // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT is deliberately NOT
        // set: resetting the pool recycles its allocations in one call, while
        // per-buffer reset forces the driver to keep each buffer's blocks
        // separately reusable, which is slower for this one-buffer-per-pool
        // layout.
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = queueFamily;

        VkResult res = fns.CreateCommandPool(handle, &poolInfo, nullptr, &frame.CommandPool);
        if (!MELONPRIME_VK_CHECK("vkCreateCommandPool", res))
        {
            Destroy();
            return false;
        }

        VkCommandBufferAllocateInfo cmdInfo{};
        cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdInfo.commandPool = frame.CommandPool;
        cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdInfo.commandBufferCount = 1;

        res = fns.AllocateCommandBuffers(handle, &cmdInfo, &frame.CommandBuffer);
        if (!MELONPRIME_VK_CHECK("vkAllocateCommandBuffers", res))
        {
            Destroy();
            return false;
        }

        // Created signalled so the first BeginFrame() does not block on a
        // fence that will never be submitted.
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        res = fns.CreateFence(handle, &fenceInfo, nullptr, &frame.InFlightFence);
        if (!MELONPRIME_VK_CHECK("vkCreateFence", res))
        {
            Destroy();
            return false;
        }

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        res = fns.CreateSemaphore(handle, &semInfo, nullptr, &frame.ImageAvailable);
        if (!MELONPRIME_VK_CHECK("vkCreateSemaphore (image available)", res))
        {
            Destroy();
            return false;
        }

        res = fns.CreateSemaphore(handle, &semInfo, nullptr, &frame.RenderFinished);
        if (!MELONPRIME_VK_CHECK("vkCreateSemaphore (render finished)", res))
        {
            Destroy();
            return false;
        }

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
        if (timestampSupport)
        {
            VkQueryPoolCreateInfo queryInfo{};
            queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queryInfo.queryCount = TimestampQueryCount;
            res = fns.CreateQueryPool(handle, &queryInfo, nullptr, &frame.TimestampQueryPool);
            if (res != VK_SUCCESS)
            {
                timestampPoolsCreated = false;
                TimestampPeriodNs = 0.0f;
                for (FrameContext& created : Frames)
                {
                    if (created.TimestampQueryPool != VK_NULL_HANDLE)
                    {
                        fns.DestroyQueryPool(handle, created.TimestampQueryPool, nullptr);
                        created.TimestampQueryPool = VK_NULL_HANDLE;
                    }
                    created.TimestampQueriesEnabled = false;
                }
            }
            else
            {
                frame.TimestampQueriesEnabled = true;
            }
        }
#endif

        char name[64];
        std::snprintf(name, sizeof(name), "frame %u command pool", i);
        device.SetDebugName(VK_OBJECT_TYPE_COMMAND_POOL, frame.CommandPool, name);
        std::snprintf(name, sizeof(name), "frame %u command buffer", i);
        device.SetDebugName(VK_OBJECT_TYPE_COMMAND_BUFFER, frame.CommandBuffer, name);
        std::snprintf(name, sizeof(name), "frame %u in-flight fence", i);
        device.SetDebugName(VK_OBJECT_TYPE_FENCE, frame.InFlightFence, name);
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
        if (frame.TimestampQueryPool != VK_NULL_HANDLE)
            device.SetDebugName(VK_OBJECT_TYPE_QUERY_POOL, frame.TimestampQueryPool, "VulkanPerf.GpuTimestamps");
#endif
    }

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    if (!timestampPoolsCreated)
        TimestampPeriodNs = 0.0f;
#endif

    if (!HasValidCoreFrameResources())
    {
        Destroy();
        return false;
    }
    CoreResourcesReady = true;

    return true;
}

void FrameRing::Destroy()
{
    if (!Device)
    {
        Frames.clear();
        CoreResourcesReady = false;
        CurrentIndex = 0;
        AbsoluteFrame = 1;
        CompletedFrame = 0;
        LastSubmittedIndex = 0;
        HasSubmittedFrame = false;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
        TimestampPeriodNs = 0.0f;
#endif
        return;
    }

    // Teardown path: wait for everything, retire the whole destruction queue,
    // then delete the ring's own objects. Order matters -- the queue may still
    // hold resources that the frames' command buffers referenced.
    WaitIdle();
    DestroyQueue.DestroyAll();
    DestroyFrames();

    Frames.clear();
    Device = nullptr;
    CoreResourcesReady = false;
    CurrentIndex = 0;
    AbsoluteFrame = 1;
    CompletedFrame = 0;
    LastSubmittedIndex = 0;
    HasSubmittedFrame = false;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    TimestampPeriodNs = 0.0f;
#endif
}

bool FrameRing::HasValidCoreFrameResources() const noexcept
{
    if (Frames.empty())
        return false;

    for (const FrameContext& frame : Frames)
    {
        if (frame.CommandPool == VK_NULL_HANDLE
            || frame.CommandBuffer == VK_NULL_HANDLE
            || frame.InFlightFence == VK_NULL_HANDLE
            || frame.ImageAvailable == VK_NULL_HANDLE
            || frame.RenderFinished == VK_NULL_HANDLE)
        {
            return false;
        }
    }
    return true;
}

void FrameRing::DestroyFrames()
{
    if (!Device)
        return;

    const DeviceDispatch& fns = Device->Fns();
    VkDevice handle = Device->GetHandle();

    for (FrameContext& frame : Frames)
    {
        if (frame.RenderFinished != VK_NULL_HANDLE)
        {
            fns.DestroySemaphore(handle, frame.RenderFinished, nullptr);
            frame.RenderFinished = VK_NULL_HANDLE;
        }
        if (frame.ImageAvailable != VK_NULL_HANDLE)
        {
            fns.DestroySemaphore(handle, frame.ImageAvailable, nullptr);
            frame.ImageAvailable = VK_NULL_HANDLE;
        }
        if (frame.InFlightFence != VK_NULL_HANDLE)
        {
            fns.DestroyFence(handle, frame.InFlightFence, nullptr);
            frame.InFlightFence = VK_NULL_HANDLE;
        }
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
        if (frame.TimestampQueryPool != VK_NULL_HANDLE)
        {
            fns.DestroyQueryPool(handle, frame.TimestampQueryPool, nullptr);
            frame.TimestampQueryPool = VK_NULL_HANDLE;
        }
#endif
        // The command buffer is freed implicitly with its pool.
        if (frame.CommandPool != VK_NULL_HANDLE)
        {
            fns.DestroyCommandPool(handle, frame.CommandPool, nullptr);
            frame.CommandPool = VK_NULL_HANDLE;
        }
        frame.CommandBuffer = VK_NULL_HANDLE;
        frame.HasPendingSubmission = false;
        frame.Recording = false;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
        frame.TimestampQueriesEnabled = false;
        frame.TimestampResultsAvailable = false;
        frame.TimestampQueriesReset = false;
        frame.TimestampQueriesWritten = false;
#endif
    }
}

FrameContext* FrameRing::GetCurrentFrame() noexcept
{
    if (Frames.empty())
        return nullptr;
    return &Frames[CurrentIndex];
}

VkCommandBuffer FrameRing::GetCommandBuffer() const noexcept
{
    if (Frames.empty())
        return VK_NULL_HANDLE;
    return Frames[CurrentIndex].CommandBuffer;
}

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)

void FrameRing::WriteTimestamp(VkPipelineStageFlagBits stage, u32 queryIndex) noexcept
{
    if (!Device || Frames.empty() || queryIndex >= TimestampQueryCount)
        return;
    FrameContext& frame = Frames[CurrentIndex];
    if (!frame.Recording || !frame.TimestampQueriesEnabled)
        return;
    if (!frame.TimestampQueriesReset)
    {
        // The previous submission's results are read before this point. Defer
        // the reset until the first new timestamp so vkGetQueryPoolResults()
        // never races a pending vkCmdResetQueryPool() command in the same
        // command buffer.
        Device->Fns().CmdResetQueryPool(
            frame.CommandBuffer, frame.TimestampQueryPool,
            0, TimestampQueryCount);
        frame.TimestampQueriesReset = true;
        frame.TimestampResultsAvailable = false;
    }
    Device->Fns().CmdWriteTimestamp(
        frame.CommandBuffer, stage, frame.TimestampQueryPool, queryIndex);
    frame.TimestampQueriesWritten = true;
}

bool FrameRing::ReadCurrentFrameTimestamps(
    u32 firstQuery, u32 queryCount, u64* values) const noexcept
{
    if (!Device || Frames.empty() || !values || queryCount == 0
        || firstQuery >= TimestampQueryCount
        || queryCount > TimestampQueryCount - firstQuery)
    {
        return false;
    }
    const FrameContext& frame = Frames[CurrentIndex];
    if (!frame.TimestampQueriesEnabled || !frame.TimestampResultsAvailable)
        return false;
    const VkResult result = Device->Fns().GetQueryPoolResults(
        Device->GetHandle(), frame.TimestampQueryPool,
        firstQuery, queryCount, sizeof(u64) * queryCount, values,
        sizeof(u64), VK_QUERY_RESULT_64_BIT);
    return result == VK_SUCCESS;
}

#endif

FrameContext* FrameRing::BeginFrame(bool recordRasterBegin)
{
    return BeginFrameInternal(true, recordRasterBegin);
}

FrameWaitResult FrameRing::WaitForLatestSubmittedFrame(u64 timeoutNanoseconds)
{
    if (!Device || Frames.empty())
        return FrameWaitResult::Error;

    if (!HasSubmittedFrame)
        return FrameWaitResult::Ready;

    const DeviceDispatch& fns = Device->Fns();
    const VkDevice handle = Device->GetHandle();
    FrameContext& frame = Frames[LastSubmittedIndex];

    if (!frame.HasPendingSubmission)
        return FrameWaitResult::Ready;

    const VkResult res = fns.WaitForFences(
        handle, 1, &frame.InFlightFence, VK_TRUE, timeoutNanoseconds);
    if (res == VK_TIMEOUT)
    {
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] latest submitted presenter frame %llu did not complete within %llu ns\n",
            static_cast<unsigned long long>(frame.SubmittedFrame),
            static_cast<unsigned long long>(timeoutNanoseconds));
        return FrameWaitResult::Timeout;
    }
    if (!MELONPRIME_VK_CHECK("vkWaitForFences(strict presenter)", res))
        return FrameWaitResult::Error;

    CompletedFrame = std::max(CompletedFrame, frame.SubmittedFrame);
    // Queue submissions are ordered. Waiting the newest submitted fence also
    // retires every older frame on this ring, so clear their bookkeeping in
    // one pass instead of making the next ring reuse perform redundant waits.
    for (FrameContext& submitted : Frames)
    {
        if (submitted.HasPendingSubmission && submitted.SubmittedFrame <= CompletedFrame)
            submitted.HasPendingSubmission = false;
    }
    DestroyQueue.Collect(CompletedFrame);
    return FrameWaitResult::Ready;
}

FrameContext* FrameRing::TryBeginFrame(FrameBeginResult* result)
{
    return BeginFrameInternal(false, false, result);
}

FrameContext* FrameRing::BeginFrameInternal(
    bool waitForSlot,
    bool recordRasterBegin,
    FrameBeginResult* result)
{
    if (result)
        *result = FrameBeginResult::Error;
    if (!Device || Frames.empty())
        return nullptr;

    const DeviceDispatch& fns = Device->Fns();
    VkDevice handle = Device->GetHandle();

    // Keep CurrentIndex untouched until the slot has passed the readiness
    // probe and its recording boundary can actually be opened. In particular,
    // a low-latency busy skip must be a true no-op for the frame ring.
    const u32 nextIndex = VulkanFrameRingIndexForAbsoluteFrame(
        AbsoluteFrame, static_cast<u32>(Frames.size()));
    FrameContext& frame = Frames[nextIndex];

    if (frame.Recording)
    {
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] BeginFrame() called while frame slot %u is still recording\n", nextIndex);
        return nullptr;
    }

    if (frame.HasPendingSubmission)
    {
        // Renderer/presenter rings throttle the CPU to framesInFlight. The
        // compositor uses the non-blocking branch and keeps the last published
        // frame when all of its slots are still executing.
        VkResult res = VK_SUCCESS;
        if (waitForSlot)
        {
            VulkanPerf::ScopedRasterBeginWait rasterWait(recordRasterBegin);
            res = fns.WaitForFences(
                handle, 1, &frame.InFlightFence, VK_TRUE, DefaultFenceTimeoutNanoseconds);
        }
        else
        {
            res = fns.GetFenceStatus(handle, frame.InFlightFence);
        }
        if (!waitForSlot && res == VK_NOT_READY)
        {
            if (result)
                *result = FrameBeginResult::Busy;
            return nullptr;
        }
        if (res == VK_TIMEOUT)
        {
            if (waitForSlot && recordRasterBegin)
                VulkanPerf::RecordRasterBeginFenceTimeout();
            Platform::Log(Platform::LogLevel::Error,
                "[Vulkan] frame slot %u did not complete within 1s; the GPU is not responding\n",
                nextIndex);
            return nullptr;
        }
        if (!MELONPRIME_VK_CHECK(
                waitForSlot ? "vkWaitForFences" : "vkGetFenceStatus", res))
            return nullptr;

        // A signalled slot-N fence proves every frame up to and including the
        // one that slot carried has retired, because submissions to a single
        // queue complete in submission order.
        CompletedFrame = std::max(CompletedFrame, frame.SubmittedFrame);
        frame.HasPendingSubmission = false;
    }
    else if (waitForSlot && recordRasterBegin)
    {
        VulkanPerf::RecordRasterBeginNoWait();
    }

    // Retire everything the completed frames were holding. This is the only
    // place resources actually die during normal operation.
    DestroyQueue.Collect(CompletedFrame);

    VkResult res = fns.ResetFences(handle, 1, &frame.InFlightFence);
    if (!MELONPRIME_VK_CHECK("vkResetFences", res))
        return nullptr;

    // Resetting the pool recycles the command buffer allocated from it; the
    // buffer must not be in the pending state, which the completed-fence check
    // above guarantees.
    res = fns.ResetCommandPool(handle, frame.CommandPool, 0);
    if (!MELONPRIME_VK_CHECK("vkResetCommandPool", res))
        return nullptr;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    // ONE_TIME_SUBMIT: the buffer is re-recorded from scratch every frame, and
    // the flag lets the driver discard it after submission instead of keeping
    // it resubmittable.
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    res = fns.BeginCommandBuffer(frame.CommandBuffer, &beginInfo);
    if (!MELONPRIME_VK_CHECK("vkBeginCommandBuffer", res))
        return nullptr;

    CurrentIndex = nextIndex;
    frame.Recording = true;
    frame.SubmittedFrame = AbsoluteFrame;
    if (result)
        *result = FrameBeginResult::Ready;
    return &frame;
}

bool FrameRing::SubmitFrame(
    VkQueue queue,
    VkSemaphore waitSemaphore,
    VkPipelineStageFlags waitStageMask,
    VkSemaphore signalSemaphore,
    const void* submitPNext)
{
    if (!Device || Frames.empty() || queue == VK_NULL_HANDLE)
        return false;

    FrameContext& frame = Frames[CurrentIndex];
    if (!frame.Recording)
    {
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] SubmitFrame() called without a matching BeginFrame()\n");
        return false;
    }

    const DeviceDispatch& fns = Device->Fns();

    VkResult res = fns.EndCommandBuffer(frame.CommandBuffer);
    if (!MELONPRIME_VK_CHECK("vkEndCommandBuffer", res))
    {
        frame.Recording = false;
        return false;
    }
    frame.Recording = false;

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.pNext = submitPNext;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &frame.CommandBuffer;

    if (waitSemaphore != VK_NULL_HANDLE)
    {
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &waitSemaphore;
        // The stage mask must be supplied by the caller: for a swapchain
        // acquire it is the stage that first writes the acquired image, and
        // using ALL_COMMANDS there would stall the whole pipeline behind the
        // acquire instead of only the part that touches the image.
        submit.pWaitDstStageMask = &waitStageMask;
    }

    if (signalSemaphore != VK_NULL_HANDLE)
    {
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &signalSemaphore;
    }

    {
        std::lock_guard<std::mutex> queueLock(Device->GetQueueMutex());
        res = fns.QueueSubmit(queue, 1, &submit, frame.InFlightFence);
    }
    if (!MELONPRIME_VK_CHECK("vkQueueSubmit", res))
    {
        // The fence was reset in BeginFrame() and no submission will signal it,
        // so the slot must not be marked pending or the next BeginFrame() would
        // block forever.
        return false;
    }

    frame.HasPendingSubmission = true;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    if (frame.TimestampQueriesEnabled)
    {
        // A slot with no timestamp writes must not expose stale values from an
        // older use. A written query pool becomes readable after this fence
        // signals and before the next recording emits its deferred reset.
        frame.TimestampResultsAvailable = frame.TimestampQueriesWritten;
        frame.TimestampQueriesReset = false;
        frame.TimestampQueriesWritten = false;
    }
#endif
    LastSubmittedIndex = CurrentIndex;
    HasSubmittedFrame = true;
    AbsoluteFrame++;
    return true;
}

void FrameRing::WaitIdle()
{
    if (!Device || !Device->IsValid())
        return;

    const DeviceDispatch& fns = Device->Fns();
    if (!fns.DeviceWaitIdle)
        return;

    // Permitted WaitIdle site: teardown and swapchain recreation.
    const VkResult res = fns.DeviceWaitIdle(Device->GetHandle());
    if (res != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] vkDeviceWaitIdle: %s\n", FormatResult(res).c_str());
    }

    // Every submitted frame has retired. The current recording frame is not a
    // submission yet: resource-growth paths can call WaitIdle() after
    // admission but before they finish recording it. Do not advance
    // CompletedFrame to that unsubmitted frame, or a later Collect() could
    // destroy resources keyed to it before its eventual submission retires.
    for (FrameContext& frame : Frames)
    {
        if (frame.Recording)
            continue;
        CompletedFrame = std::max(CompletedFrame, frame.SubmittedFrame);
        frame.HasPendingSubmission = false;
    }
}

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
