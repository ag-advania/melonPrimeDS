#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "MelonPrimeVulkanFrameQueue.h"

#include <algorithm>

#include <Platform.h>
#include "VulkanPerfStats.h"

namespace MelonPrime
{

MelonPrimeVulkanFrameQueue::MelonPrimeVulkanFrameQueue()
{
    for (auto& frame : frames)
    {
        freeQueue.push(&frame);
    }
}

MelonPrimeVulkanFrameQueuePolicy MelonPrimeVulkanFrameQueue::sanitizePolicy(MelonPrimeVulkanFrameQueuePolicy policy)
{
    policy.MaxBacklogDepth = std::max<u64>(1u, std::min<u64>(policy.MaxBacklogDepth, MELONPRIME_VULKAN_FRAME_QUEUE_SIZE - 1));
    return policy;
}

VulkanFrame* MelonPrimeVulkanFrameQueue::getRenderFrame(const MelonPrimeVulkanFrameQueuePolicy& requestedPolicy)
{
    std::unique_lock lock(frameLock);
    stats.RenderFramesAcquired++;
    const MelonPrimeVulkanFrameQueuePolicy policy = sanitizePolicy(requestedPolicy);

    if (!freeQueue.empty())
    {
        VulkanFrame* frame = freeQueue.front();
        freeQueue.pop();
        frame->frameId = nextFrameId++;
        frame->sourceGeneration = 0;
        frame->queuedAtNs = 0;
        frame->presentTimelineValue = 0;
        return frame;
    }

    if (policy.UseLegacyOpenGlQueue)
    {
        if (presentQueue.empty())
        {
            stats.RenderFramesDroppedByPolicy++;
            return nullptr;
        }

        VulkanFrame* frame = presentQueue.back();
        presentQueue.pop_back();
        frame->frameId = nextFrameId++;
        frame->sourceGeneration = 0;
        frame->queuedAtNs = 0;
        frame->presentTimelineValue = 0;
        stats.PendingFramesStolenForRender++;
        updateBacklogStatsLocked();
        return frame;
    }

    if (policy.AllowStealPending && !presentQueue.empty())
    {
        const u64 nowNs = MelonDSAndroid::PerfNowNs();
        VulkanFrame* frame = presentQueue.back();
        presentQueue.pop_back();
        frame->frameId = nextFrameId++;
        frame->sourceGeneration = 0;
        frame->presentTimelineValue = 0;
        stats.PendingFramesStolenForRender++;
        stats.PresentFramesDroppedByPolicy++;
        recordDroppedFrameLocked(frame, PresentDropCause::StealForRender, nowNs);
        updateBacklogStatsLocked();
        return frame;
    }

    stats.RenderFramesDroppedByPolicy++;
    return nullptr;
}

VulkanFrame* MelonPrimeVulkanFrameQueue::getPresentFrame(
    const MelonPrimeVulkanFrameQueuePolicy& requestedPolicy,
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline)
{
    std::unique_lock lock(frameLock);
    const MelonPrimeVulkanFrameQueuePolicy policy = sanitizePolicy(requestedPolicy);

    if (policy.UseLegacyOpenGlQueue)
    {
        if (presentQueue.empty())
        {
            bool hasNewFrame = false;
            if (deadline.has_value())
                hasNewFrame = presentFrameReadyCondition.wait_until(lock, *deadline, [&]{ return !presentQueue.empty(); });

            if (!hasNewFrame)
            {
                if (previousFrame != nullptr)
                    stats.PreviousFrameReused++;
                return previousFrame;
            }
        }

        if (previousFrame)
        {
            freeQueue.push(previousFrame);
            previousFrame->queuedAtNs = 0;
            previousFrame = nullptr;
        }

        const u64 nowNs = MelonDSAndroid::PerfNowNs();
        VulkanFrame* frame = presentQueue.front();
        presentQueue.pop_front();
        stats.PresentFramesReturned++;

        const u64 staleFrameCount = static_cast<u64>(presentQueue.size());
        for (auto f : presentQueue)
        {
            freeQueue.push(f);
            recordDroppedFrameLocked(f, PresentDropCause::Stale, nowNs);
        }
        stats.StaleFramesDropped += staleFrameCount;
        stats.PresentFramesDroppedByPolicy += staleFrameCount;

        presentQueue.clear();
        previousFrame = frame;
        recordPresentedFrameAgeLocked(frame, nowNs);
        updateBacklogStatsLocked();
        return frame;
    }

    if (presentQueue.empty()) {
        bool hasNewFrame = false;
        if (deadline.has_value())
            hasNewFrame = presentFrameReadyCondition.wait_until(lock, *deadline, [&]{ return !presentQueue.empty(); });

        if (!hasNewFrame)
        {
            if (suppressPreviousFrameReuse || !policy.AllowPreviousFrameReuse)
                return nullptr;
            if (previousFrame != nullptr)
                stats.PreviousFrameReused++;
            return previousFrame;
        }
    }

    if (previousFrame)
    {
        freeQueue.push(previousFrame);
        previousFrame->queuedAtNs = 0;
        previousFrame = nullptr;
    }

    const u64 nowNs = MelonDSAndroid::PerfNowNs();
    VulkanFrame* frame = presentQueue.front();
    presentQueue.pop_front();
    stats.PresentFramesReturned++;
    suppressPreviousFrameReuse = false;

    const u64 staleFrameCount = static_cast<u64>(presentQueue.size());
    for (auto f : presentQueue)
    {
        freeQueue.push(f);
        recordDroppedFrameLocked(f, PresentDropCause::Stale, nowNs);
    }
    stats.StaleFramesDropped += staleFrameCount;
    stats.PresentFramesDroppedByPolicy += staleFrameCount;

    presentQueue.clear();
    previousFrame = frame;
    recordPresentedFrameAgeLocked(frame, nowNs);
    updateBacklogStatsLocked();
    return frame;
}

VulkanFrame* MelonPrimeVulkanFrameQueue::getPresentCandidate(
    const MelonPrimeVulkanFrameQueuePolicy& requestedPolicy,
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline)
{
    std::unique_lock lock(frameLock);
    const MelonPrimeVulkanFrameQueuePolicy policy = sanitizePolicy(requestedPolicy);

    if (pendingPresentFrame != nullptr)
        return pendingPresentFrame;

    if (presentQueue.empty())
    {
        bool hasNewFrame = false;
        if (deadline.has_value())
        {
            hasNewFrame = presentFrameReadyCondition.wait_until(lock, *deadline, [&] {
                return !presentQueue.empty() || pendingPresentFrame != nullptr;
            });
        }

        if (pendingPresentFrame != nullptr)
            return pendingPresentFrame;

        if (!hasNewFrame)
        {
            if (suppressPreviousFrameReuse || !policy.AllowPreviousFrameReuse)
                return nullptr;
            if (previousFrame != nullptr)
                stats.PreviousFrameReused++;
            return previousFrame;
        }
    }

    if (presentQueue.empty())
        return nullptr;

    VulkanFrame* frame = nullptr;
    if (policy.PreferOldestFrame)
    {
        frame = presentQueue.back();
        presentQueue.pop_back();
    }
    else
    {
        frame = presentQueue.front();
        presentQueue.pop_front();
    }
    pendingPresentFrame = frame;
    stats.PresentFramesReturned++;
    suppressPreviousFrameReuse = false;

    if (!policy.AllowDropForDeadline && !policy.PreserveBacklogOnPresent)
    {
        const u64 nowNs = MelonDSAndroid::PerfNowNs();
        const u64 staleFrameCount = static_cast<u64>(presentQueue.size());
        for (auto f : presentQueue)
        {
            freeQueue.push(f);
            recordDroppedFrameLocked(f, PresentDropCause::Stale, nowNs);
        }
        stats.StaleFramesDropped += staleFrameCount;
        stats.PresentFramesDroppedByPolicy += staleFrameCount;
        presentQueue.clear();
    }
    updateBacklogStatsLocked();
    return frame;
}

VulkanFrame* MelonPrimeVulkanFrameQueue::getReusablePreviousFrame(const MelonPrimeVulkanFrameQueuePolicy& requestedPolicy)
{
    std::unique_lock lock(frameLock);
    const MelonPrimeVulkanFrameQueuePolicy policy = sanitizePolicy(requestedPolicy);
    if (suppressPreviousFrameReuse || !policy.AllowPreviousFrameReuse || previousFrame == nullptr)
        return nullptr;

    stats.PreviousFrameReused++;
    return previousFrame;
}

void MelonPrimeVulkanFrameQueue::recycleRenderFrame(VulkanFrame* frame)
{
    std::unique_lock lock(frameLock);
    if (frame == nullptr)
        return;

    frame->queuedAtNs = 0;
    freeQueue.push(frame);
}

void MelonPrimeVulkanFrameQueue::commitPresentedFrame(VulkanFrame* frame, const MelonPrimeVulkanFrameQueuePolicy& requestedPolicy)
{
    std::unique_lock lock(frameLock);
    if (frame == nullptr)
        return;

    const MelonPrimeVulkanFrameQueuePolicy policy = sanitizePolicy(requestedPolicy);
    if (frame != pendingPresentFrame)
    {
        if (frame == previousFrame)
            suppressPreviousFrameReuse = false;
        return;
    }

    if (previousFrame != nullptr && previousFrame != frame)
    {
        freeQueue.push(previousFrame);
        previousFrame->queuedAtNs = 0;
    }

    previousFrame = frame;
    pendingPresentFrame = nullptr;
    suppressPreviousFrameReuse = false;
    const u64 nowNs = MelonDSAndroid::PerfNowNs();
    recordPresentedFrameAgeLocked(frame, nowNs);

    if (!policy.PreserveBacklogOnPresent)
    {
        for (auto f : presentQueue)
        {
            freeQueue.push(f);
            if (policy.TreatBacklogTrimAsFastForwardSkip)
            {
                f->queuedAtNs = 0;
                stats.FastForwardFramesSkipped++;
            }
            else
            {
                recordDroppedFrameLocked(f, PresentDropCause::Stale, nowNs);
            }
        }
        const u64 staleFrameCount = static_cast<u64>(presentQueue.size());
        if (!policy.TreatBacklogTrimAsFastForwardSkip)
        {
            stats.StaleFramesDropped += staleFrameCount;
            stats.PresentFramesDroppedByPolicy += staleFrameCount;
        }
        presentQueue.clear();
    }

    dropPendingFramesToBacklogLocked(policy.MaxBacklogDepth, policy.TreatBacklogTrimAsFastForwardSkip);
    updateBacklogStatsLocked();
}

void MelonPrimeVulkanFrameQueue::deferPresentedFrame(VulkanFrame* frame, const MelonPrimeVulkanFrameQueuePolicy& requestedPolicy)
{
    std::unique_lock lock(frameLock);
    if (frame == nullptr || frame != pendingPresentFrame)
        return;

    const MelonPrimeVulkanFrameQueuePolicy policy = sanitizePolicy(requestedPolicy);
    if (!policy.AllowDropForDeadline)
    {
        // In realtime mode, don't keep a failed candidate pinned as pending.
        // Requeue it so the next present attempt can pick a fresher frame.
        if (policy.PreferOldestFrame)
            presentQueue.push_front(pendingPresentFrame);
        else
            presentQueue.push_back(pendingPresentFrame);

        pendingPresentFrame = nullptr;
        stats.PresentDeferredByDeadline++;
        updateBacklogStatsLocked();
        return;
    }

    if (policy.AllowDropForDeadline)
    {
        const u64 nowNs = MelonDSAndroid::PerfNowNs();
        if (policy.PreferOldestFrame)
        {
            presentQueue.push_back(pendingPresentFrame);
            stats.PresentDeferredByDeadline++;
        }
        else
        {
            freeQueue.push(pendingPresentFrame);
            stats.PresentFramesDroppedByPolicy++;
            recordDroppedFrameLocked(pendingPresentFrame, PresentDropCause::Deadline, nowNs);
        }
        pendingPresentFrame = nullptr;
        updateBacklogStatsLocked();
    }
}

void MelonPrimeVulkanFrameQueue::validateRenderFrame(VulkanFrame* frame, int requiredWidth, int requiredHeight, FrameBackend backend)
{
    if (frame == nullptr)
        return;

    // This queue is Vulkan-only on desktop. Image/fence ownership remains in
    // MelonPrimeVulkanOutput; the queue carries immutable frame identity and
    // synchronization values without destroying those resources itself.
    if (frame->backend != backend || backend != FrameBackend::VulkanImage)
    {
        frame->backend = FrameBackend::VulkanImage;
        frame->frameImage = VK_NULL_HANDLE;
        frame->width = 0;
        frame->height = 0;
        frame->frameId = 0;
        frame->sourceGeneration = 0;
        frame->renderTimelineValue = 0;
        frame->presentTimelineValue = 0;
        frame->queuedAtNs = 0;
    }

    frame->width = std::max(requiredWidth, 0);
    frame->height = std::max(requiredHeight, 0);
}

void MelonPrimeVulkanFrameQueue::pushRenderedFrame(VulkanFrame* frame, const MelonPrimeVulkanFrameQueuePolicy& requestedPolicy)
{
    std::unique_lock lock(frameLock);
    const MelonPrimeVulkanFrameQueuePolicy policy = sanitizePolicy(requestedPolicy);
    frame->queuedAtNs = MelonDSAndroid::PerfNowNs();
    if (policy.UseLegacyOpenGlQueue)
    {
        presentQueue.push_front(frame);
        stats.RenderFramesQueued++;
        updateBacklogStatsLocked();
        presentFrameReadyCondition.notify_one();
        return;
    }

    dropPendingFramesToBacklogLocked(
        policy.MaxBacklogDepth > 0 ? policy.MaxBacklogDepth - 1 : 0,
        policy.TreatBacklogTrimAsFastForwardSkip);
    presentQueue.push_front(frame);
    stats.RenderFramesQueued++;
    updateBacklogStatsLocked();
    presentFrameReadyCondition.notify_one();
}

void MelonPrimeVulkanFrameQueue::discardRenderedFrame(VulkanFrame* frame)
{
    std::unique_lock lock(frameLock);
    frame->queuedAtNs = 0;
    freeQueue.push(frame);
    stats.RenderFramesDiscarded++;
}

void MelonPrimeVulkanFrameQueue::requestPresentationResync()
{
    std::unique_lock lock(frameLock);

    for (auto f : presentQueue)
    {
        f->queuedAtNs = 0;
        freeQueue.push(f);
    }

    presentQueue.clear();
    if (pendingPresentFrame != nullptr)
    {
        pendingPresentFrame->queuedAtNs = 0;
        freeQueue.push(pendingPresentFrame);
        pendingPresentFrame = nullptr;
    }

    if (previousFrame != nullptr)
    {
        previousFrame->queuedAtNs = 0;
        freeQueue.push(previousFrame);
        previousFrame = nullptr;
    }

    // A resync invalidates the presentation contract for all in-flight frames:
    // scale, backend, packed buffers, and 3D source image may all have changed.
    // Reusing the previous frame after this point mixes old frame ownership with
    // the new configuration and reopens flicker/corruption on IR changes.
    suppressPreviousFrameReuse = true;
    updateBacklogStatsLocked();
}

void MelonPrimeVulkanFrameQueue::requestFastForwardPresentationTransition()
{
    std::unique_lock lock(frameLock);

    for (auto f : presentQueue)
    {
        f->queuedAtNs = 0;
        freeQueue.push(f);
    }

    presentQueue.clear();
    if (pendingPresentFrame != nullptr)
    {
        pendingPresentFrame->queuedAtNs = 0;
        freeQueue.push(pendingPresentFrame);
        pendingPresentFrame = nullptr;
    }

    suppressPreviousFrameReuse = false;
    updateBacklogStatsLocked();
}

void MelonPrimeVulkanFrameQueue::clear()
{
    std::unique_lock lock(frameLock);

    for (auto f : presentQueue)
    {
        f->queuedAtNs = 0;
        freeQueue.push(f);
    }

    presentQueue.clear();
    previousFrame = nullptr;
    pendingPresentFrame = nullptr;
    suppressPreviousFrameReuse = false;
    stats = MelonPrimeVulkanFrameQueueStats{};
    rebuildFreeQueueLocked();

    for (auto& frame : frames)
    {
        frame.backend = FrameBackend::VulkanImage;
        frame.frameImage = VK_NULL_HANDLE;
        frame.width = 0;
        frame.height = 0;
        frame.frameId = 0;
        frame.sourceGeneration = 0;
        frame.renderFence = 0;
        frame.presentFence = 0;
        frame.renderTimelineValue = 0;
        frame.presentTimelineValue = 0;
        frame.queuedAtNs = 0;
    }
}

MelonPrimeVulkanFrameQueueStats MelonPrimeVulkanFrameQueue::takeStatsSnapshotAndReset()
{
    std::unique_lock lock(frameLock);
    stats.CurrentBacklogDepth = static_cast<u64>(presentQueue.size());
    MelonPrimeVulkanFrameQueueStats snapshot = stats;
    stats = MelonPrimeVulkanFrameQueueStats{};
    return snapshot;
}

void MelonPrimeVulkanFrameQueue::updateBacklogStatsLocked()
{
    const u64 backlogDepth = static_cast<u64>(presentQueue.size());
    stats.CurrentBacklogDepth = backlogDepth;
    stats.MaxBacklogDepth = std::max(stats.MaxBacklogDepth, backlogDepth);
}

void MelonPrimeVulkanFrameQueue::rebuildFreeQueueLocked()
{
    std::queue<VulkanFrame*> emptyQueue;
    std::swap(freeQueue, emptyQueue);

    for (auto& frame : frames)
        freeQueue.push(&frame);
}

void MelonPrimeVulkanFrameQueue::dropPendingFramesToBacklogLocked(u64 maxBacklogDepth, bool treatAsFastForwardSkip)
{
    const u64 nowNs = MelonDSAndroid::PerfNowNs();
    while (static_cast<u64>(presentQueue.size()) > maxBacklogDepth && !presentQueue.empty())
    {
        VulkanFrame* frame = presentQueue.back();
        presentQueue.pop_back();
        freeQueue.push(frame);
        if (treatAsFastForwardSkip)
        {
            frame->queuedAtNs = 0;
            stats.FastForwardFramesSkipped++;
        }
        else
        {
            stats.PresentFramesDroppedByPolicy++;
            recordDroppedFrameLocked(frame, PresentDropCause::BacklogTrim, nowNs);
        }
    }
    updateBacklogStatsLocked();
}

void MelonPrimeVulkanFrameQueue::recordPresentedFrameAgeLocked(VulkanFrame* frame, u64 nowNs)
{
    if (frame == nullptr || frame->queuedAtNs == 0 || nowNs < frame->queuedAtNs)
        return;

    const u64 ageNs = nowNs - frame->queuedAtNs;
    stats.PresentedFrameAgeTotalNs += ageNs;
    stats.PresentedFrameAgeMaxNs = std::max(stats.PresentedFrameAgeMaxNs, ageNs);
    stats.PresentedFrameAgeSamples++;
}

void MelonPrimeVulkanFrameQueue::recordDroppedFrameLocked(VulkanFrame* frame, PresentDropCause cause, u64 nowNs)
{
    if (frame == nullptr)
        return;

    switch (cause)
    {
        case PresentDropCause::Stale:
            stats.PresentDroppedByStale++;
            break;
        case PresentDropCause::StealForRender:
            stats.PresentDroppedBySteal++;
            break;
        case PresentDropCause::Deadline:
            stats.PresentDroppedByDeadline++;
            break;
        case PresentDropCause::BacklogTrim:
            stats.PresentDroppedByBacklogTrim++;
            break;
    }

    if (frame->queuedAtNs != 0 && nowNs >= frame->queuedAtNs)
    {
        const u64 ageNs = nowNs - frame->queuedAtNs;
        stats.DroppedFrameAgeTotalNs += ageNs;
        stats.DroppedFrameAgeMaxNs = std::max(stats.DroppedFrameAgeMaxNs, ageNs);
        stats.DroppedFrameAgeSamples++;
    }

    frame->queuedAtNs = 0;
}


} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
