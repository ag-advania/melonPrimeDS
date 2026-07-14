#include "FrameQueue.h"

#include <algorithm>
#include <iterator>

#include <Platform.h>
#include "VulkanPerfStats.h"

// Reference: SapphireRhodonite/melonDS-android
// app/src/main/cpp/renderer/FrameQueue.cpp @ tag 0.7.0.rc4.
// Android EGL texture allocation/destruction is intentionally excluded: the
// desktop queue transports Vulkan frame identity and synchronization metadata,
// while VulkanOutput/presenter resource owners manage the actual images.

FrameQueue::FrameQueue()
{
    for (auto& frame : frames)
    {
        freeQueue.push(&frame);
    }
}

FrameQueuePolicy FrameQueue::sanitizePolicy(FrameQueuePolicy policy)
{
    policy.MaxBacklogDepth = std::max<u64>(1u, std::min<u64>(policy.MaxBacklogDepth, FRAME_QUEUE_SIZE - 1));
    return policy;
}

bool FrameQueue::transitionFrameLocked(
    Frame* frame,
    FrameQueueState expected,
    FrameQueueState next)
{
    if (frame == nullptr || frame->state != expected)
    {
        stats.StateTransitionFailures++;
        return false;
    }

    const bool allowed = expected == next
        || (expected == FrameQueueState::Free
            && (next == FrameQueueState::Rendering
                || next == FrameQueueState::HistoryReferenced))
        || (expected == FrameQueueState::Rendering
            && (next == FrameQueueState::Ready
                || next == FrameQueueState::Free
                || next == FrameQueueState::HistoryReferenced))
        || (expected == FrameQueueState::Ready
            && (next == FrameQueueState::Rendering
                || next == FrameQueueState::AcquiredForPresentation
                || next == FrameQueueState::Previous
                || next == FrameQueueState::Free
                || next == FrameQueueState::HistoryReferenced))
        || (expected == FrameQueueState::AcquiredForPresentation
            && (next == FrameQueueState::Ready
                || next == FrameQueueState::Previous
                || next == FrameQueueState::Free
                || next == FrameQueueState::HistoryReferenced))
        || (expected == FrameQueueState::Previous
            && (next == FrameQueueState::AcquiredForPresentation
                || next == FrameQueueState::Free
                || next == FrameQueueState::HistoryReferenced))
        || (expected == FrameQueueState::HistoryReferenced
            && next == FrameQueueState::Free);
    if (!allowed)
    {
        stats.StateTransitionFailures++;
        return false;
    }

    frame->state = next;
    return true;
}

bool FrameQueue::frameMatchesActiveGenerationsLocked(const Frame* frame) const
{
    if (frame == nullptr)
        return false;
    if (frame->frameSerial == 0)
        return false;
    if (activeRendererGeneration != 0
        && frame->rendererGeneration != activeRendererGeneration)
    {
        return false;
    }
    if (activeSurfaceGeneration != 0
        && frame->surfaceGeneration != activeSurfaceGeneration)
    {
        return false;
    }
    return true;
}

void FrameQueue::acquireRenderFrameLocked(Frame* frame)
{
    if (frame == nullptr)
        return;

    frame->frameId = nextFrameId++;
    if (nextFrameId == 0)
        nextFrameId = 1;
    frame->frameSerial = 0;
    frame->rendererGeneration = activeRendererGeneration;
    frame->surfaceGeneration = activeSurfaceGeneration;
    frame->queuedAtNs = 0;
    frame->presentTimelineValue = 0;
}

void FrameQueue::retireFrameLocked(Frame* frame)
{
    if (frame == nullptr)
        return;
    if (frame->state == FrameQueueState::Free)
        return;

    frame->queuedAtNs = 0;
    if (frame->historyReferences != 0 || frame->presentationReferences != 0)
    {
        transitionFrameLocked(frame, frame->state, FrameQueueState::HistoryReferenced);
        stats.ReferenceBlockedReuse++;
        return;
    }

    transitionFrameLocked(frame, frame->state, FrameQueueState::Free);
    freeQueue.push(frame);
}

void FrameQueue::discardGenerationMismatchesLocked()
{
    const u64 nowNs = MelonDSAndroid::PerfNowNs();
    if (pendingPresentFrame != nullptr
        && !frameMatchesActiveGenerationsLocked(pendingPresentFrame))
    {
        Frame* frame = pendingPresentFrame;
        if (previousFrame == frame)
            previousFrame = nullptr;
        frame->presentationReferences = frame->presentTimelineValue != 0 ? 1u : 0u;
        pendingPresentFrame = nullptr;
        pendingPresentReusesPrevious = false;
        recordDroppedFrameLocked(frame, PresentDropCause::Stale, nowNs);
        stats.StaleFramesDropped++;
        stats.PresentFramesDroppedByPolicy++;
        stats.GenerationMismatchDropped++;
        retireFrameLocked(frame);
    }

    if (previousFrame != nullptr
        && !frameMatchesActiveGenerationsLocked(previousFrame))
    {
        Frame* frame = previousFrame;
        previousFrame = nullptr;
        stats.GenerationMismatchDropped++;
        retireFrameLocked(frame);
    }

    auto iterator = presentQueue.begin();
    while (iterator != presentQueue.end())
    {
        Frame* frame = *iterator;
        if (frameMatchesActiveGenerationsLocked(frame))
        {
            ++iterator;
            continue;
        }

        iterator = presentQueue.erase(iterator);
        recordDroppedFrameLocked(frame, PresentDropCause::Stale, nowNs);
        stats.StaleFramesDropped++;
        stats.PresentFramesDroppedByPolicy++;
        stats.GenerationMismatchDropped++;
        retireFrameLocked(frame);
    }
    updateBacklogStatsLocked();
}

Frame* FrameQueue::getRenderFrame(const FrameQueuePolicy& requestedPolicy)
{
    std::unique_lock lock(frameLock);
    stats.RenderFramesAcquired++;
    const FrameQueuePolicy policy = sanitizePolicy(requestedPolicy);
    discardGenerationMismatchesLocked();

    if (!freeQueue.empty())
    {
        Frame* frame = freeQueue.front();
        freeQueue.pop();
        if (!transitionFrameLocked(frame, FrameQueueState::Free, FrameQueueState::Rendering))
            return nullptr;
        acquireRenderFrameLocked(frame);
        return frame;
    }

    if (policy.UseLegacyOpenGlQueue)
    {
        if (presentQueue.empty())
        {
            stats.RenderFramesDroppedByPolicy++;
            return nullptr;
        }

        const auto reusable = std::find_if(
            presentQueue.rbegin(), presentQueue.rend(), [](const Frame* candidate) {
                return candidate->historyReferences == 0
                    && candidate->presentationReferences == 0;
            });
        if (reusable == presentQueue.rend())
        {
            stats.ReferenceBlockedReuse++;
            stats.RenderFramesDroppedByPolicy++;
            return nullptr;
        }
        Frame* frame = *reusable;
        presentQueue.erase(std::next(reusable).base());
        if (!transitionFrameLocked(frame, FrameQueueState::Ready, FrameQueueState::Rendering))
            return nullptr;
        acquireRenderFrameLocked(frame);
        stats.PendingFramesStolenForRender++;
        updateBacklogStatsLocked();
        return frame;
    }

    if (policy.AllowStealPending && !presentQueue.empty())
    {
        const u64 nowNs = MelonDSAndroid::PerfNowNs();
        const auto reusable = std::find_if(
            presentQueue.rbegin(), presentQueue.rend(), [](const Frame* candidate) {
                return candidate->historyReferences == 0
                    && candidate->presentationReferences == 0;
            });
        if (reusable == presentQueue.rend())
        {
            stats.ReferenceBlockedReuse++;
            stats.RenderFramesDroppedByPolicy++;
            return nullptr;
        }
        Frame* frame = *reusable;
        presentQueue.erase(std::next(reusable).base());
        if (!transitionFrameLocked(frame, FrameQueueState::Ready, FrameQueueState::Rendering))
            return nullptr;
        stats.PendingFramesStolenForRender++;
        stats.PresentFramesDroppedByPolicy++;
        recordDroppedFrameLocked(frame, PresentDropCause::StealForRender, nowNs);
        acquireRenderFrameLocked(frame);
        updateBacklogStatsLocked();
        return frame;
    }

    stats.RenderFramesDroppedByPolicy++;
    return nullptr;
}

Frame* FrameQueue::getPresentFrame(
    const FrameQueuePolicy& requestedPolicy,
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline)
{
    std::unique_lock lock(frameLock);
    const FrameQueuePolicy policy = sanitizePolicy(requestedPolicy);
    discardGenerationMismatchesLocked();

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
            retireFrameLocked(previousFrame);
            previousFrame = nullptr;
        }

        const u64 nowNs = MelonDSAndroid::PerfNowNs();
        Frame* frame = presentQueue.front();
        presentQueue.pop_front();
        stats.PresentFramesReturned++;

        const u64 staleFrameCount = static_cast<u64>(presentQueue.size());
        for (auto f : presentQueue)
        {
            recordDroppedFrameLocked(f, PresentDropCause::Stale, nowNs);
            retireFrameLocked(f);
        }
        stats.StaleFramesDropped += staleFrameCount;
        stats.PresentFramesDroppedByPolicy += staleFrameCount;

        presentQueue.clear();
        if (!transitionFrameLocked(frame, FrameQueueState::Ready, FrameQueueState::Previous))
            return nullptr;
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
        retireFrameLocked(previousFrame);
        previousFrame = nullptr;
    }

    const u64 nowNs = MelonDSAndroid::PerfNowNs();
    Frame* frame = presentQueue.front();
    presentQueue.pop_front();
    stats.PresentFramesReturned++;
    suppressPreviousFrameReuse = false;

    const u64 staleFrameCount = static_cast<u64>(presentQueue.size());
    for (auto f : presentQueue)
    {
        recordDroppedFrameLocked(f, PresentDropCause::Stale, nowNs);
        retireFrameLocked(f);
    }
    stats.StaleFramesDropped += staleFrameCount;
    stats.PresentFramesDroppedByPolicy += staleFrameCount;

    presentQueue.clear();
    if (!transitionFrameLocked(frame, FrameQueueState::Ready, FrameQueueState::Previous))
        return nullptr;
    previousFrame = frame;
    recordPresentedFrameAgeLocked(frame, nowNs);
    updateBacklogStatsLocked();
    return frame;
}

Frame* FrameQueue::getPresentCandidate(
    const FrameQueuePolicy& requestedPolicy,
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline)
{
    std::unique_lock lock(frameLock);
    const FrameQueuePolicy policy = sanitizePolicy(requestedPolicy);
    discardGenerationMismatchesLocked();

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
            if (previousFrame != nullptr && previousFrame->presentationReferences == 0)
            {
                stats.PreviousFrameReused++;
                if (!transitionFrameLocked(
                        previousFrame,
                        FrameQueueState::Previous,
                        FrameQueueState::AcquiredForPresentation))
                {
                    return nullptr;
                }
                previousFrame->presentationReferences++;
                pendingPresentFrame = previousFrame;
                pendingPresentReusesPrevious = true;
            }
            return pendingPresentFrame;
        }
    }

    if (presentQueue.empty())
        return nullptr;

    Frame* frame = nullptr;
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
    if (!transitionFrameLocked(
            frame,
            FrameQueueState::Ready,
            FrameQueueState::AcquiredForPresentation))
    {
        return nullptr;
    }
    frame->presentationReferences++;
    pendingPresentFrame = frame;
    pendingPresentReusesPrevious = false;
    stats.PresentFramesReturned++;
    suppressPreviousFrameReuse = false;

    if (!policy.AllowDropForDeadline && !policy.PreserveBacklogOnPresent)
    {
        const u64 nowNs = MelonDSAndroid::PerfNowNs();
        const u64 staleFrameCount = static_cast<u64>(presentQueue.size());
        for (auto f : presentQueue)
        {
            recordDroppedFrameLocked(f, PresentDropCause::Stale, nowNs);
            retireFrameLocked(f);
        }
        stats.StaleFramesDropped += staleFrameCount;
        stats.PresentFramesDroppedByPolicy += staleFrameCount;
        presentQueue.clear();
    }
    updateBacklogStatsLocked();
    return frame;
}

Frame* FrameQueue::getReusablePreviousFrame(const FrameQueuePolicy& requestedPolicy)
{
    std::unique_lock lock(frameLock);
    const FrameQueuePolicy policy = sanitizePolicy(requestedPolicy);
    if (suppressPreviousFrameReuse
        || !policy.AllowPreviousFrameReuse
        || previousFrame == nullptr
        || previousFrame->presentationReferences != 0)
        return nullptr;

    if (pendingPresentFrame != nullptr)
        return pendingPresentFrame;
    if (!transitionFrameLocked(
            previousFrame,
            FrameQueueState::Previous,
            FrameQueueState::AcquiredForPresentation))
    {
        return nullptr;
    }
    previousFrame->presentationReferences++;
    pendingPresentFrame = previousFrame;
    pendingPresentReusesPrevious = true;
    stats.PreviousFrameReused++;
    return pendingPresentFrame;
}

void FrameQueue::recycleRenderFrame(Frame* frame)
{
    std::unique_lock lock(frameLock);
    if (frame == nullptr)
        return;

    if (frame->state != FrameQueueState::Rendering)
    {
        stats.StateTransitionFailures++;
        return;
    }
    retireFrameLocked(frame);
}

void FrameQueue::commitPresentedFrame(Frame* frame, const FrameQueuePolicy& requestedPolicy)
{
    std::unique_lock lock(frameLock);
    if (frame == nullptr)
        return;

    const FrameQueuePolicy policy = sanitizePolicy(requestedPolicy);
    if (frame != pendingPresentFrame)
    {
        if (frame == previousFrame)
            suppressPreviousFrameReuse = false;
        return;
    }

    frame->presentationReferences = frame->presentTimelineValue != 0 ? 1u : 0u;
    if (pendingPresentReusesPrevious)
    {
        transitionFrameLocked(
            frame,
            FrameQueueState::AcquiredForPresentation,
            FrameQueueState::Previous);
        pendingPresentFrame = nullptr;
        pendingPresentReusesPrevious = false;
        suppressPreviousFrameReuse = false;
        recordPresentedFrameAgeLocked(frame, MelonDSAndroid::PerfNowNs());
        return;
    }

    if (previousFrame != nullptr && previousFrame != frame)
    {
        retireFrameLocked(previousFrame);
    }

    if (!transitionFrameLocked(
            frame,
            FrameQueueState::AcquiredForPresentation,
            FrameQueueState::Previous))
    {
        return;
    }
    previousFrame = frame;
    pendingPresentFrame = nullptr;
    pendingPresentReusesPrevious = false;
    suppressPreviousFrameReuse = false;
    const u64 nowNs = MelonDSAndroid::PerfNowNs();
    recordPresentedFrameAgeLocked(frame, nowNs);

    if (!policy.PreserveBacklogOnPresent)
    {
        for (auto f : presentQueue)
        {
            if (policy.TreatBacklogTrimAsFastForwardSkip)
            {
                f->queuedAtNs = 0;
                stats.FastForwardFramesSkipped++;
            }
            else
            {
                recordDroppedFrameLocked(f, PresentDropCause::Stale, nowNs);
            }
            retireFrameLocked(f);
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

void FrameQueue::deferPresentedFrame(Frame* frame, const FrameQueuePolicy& requestedPolicy)
{
    std::unique_lock lock(frameLock);
    if (frame == nullptr || frame != pendingPresentFrame)
        return;

    const FrameQueuePolicy policy = sanitizePolicy(requestedPolicy);
    if (frame->presentationReferences != 0)
        frame->presentationReferences--;

    if (pendingPresentReusesPrevious)
    {
        transitionFrameLocked(
            frame,
            FrameQueueState::AcquiredForPresentation,
            FrameQueueState::Previous);
        pendingPresentFrame = nullptr;
        pendingPresentReusesPrevious = false;
        stats.PresentDeferredByDeadline++;
        return;
    }

    if (!policy.AllowDropForDeadline)
    {
        // In realtime mode, don't keep a failed candidate pinned as pending.
        // Requeue it so the next present attempt can pick a fresher frame.
        if (!transitionFrameLocked(
                pendingPresentFrame,
                FrameQueueState::AcquiredForPresentation,
                FrameQueueState::Ready))
        {
            return;
        }
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
            if (!transitionFrameLocked(
                    pendingPresentFrame,
                    FrameQueueState::AcquiredForPresentation,
                    FrameQueueState::Ready))
            {
                return;
            }
            presentQueue.push_back(pendingPresentFrame);
            stats.PresentDeferredByDeadline++;
        }
        else
        {
            stats.PresentFramesDroppedByPolicy++;
            recordDroppedFrameLocked(pendingPresentFrame, PresentDropCause::Deadline, nowNs);
            retireFrameLocked(pendingPresentFrame);
        }
        pendingPresentFrame = nullptr;
        pendingPresentReusesPrevious = false;
        updateBacklogStatsLocked();
    }
}

void FrameQueue::validateRenderFrame(Frame* frame, int requiredWidth, int requiredHeight, FrameBackend backend)
{
    std::unique_lock lock(frameLock);
    if (frame == nullptr)
        return;
    if (frame->state != FrameQueueState::Rendering)
    {
        stats.StateTransitionFailures++;
        return;
    }

    if (frame->backend != backend)
    {
        frame->backend = backend;
        frame->frameTexture = 0;
        frame->width = 0;
        frame->height = 0;
        frame->frameSerial = 0;
        frame->surfaceGeneration = activeSurfaceGeneration;
        frame->image = VK_NULL_HANDLE;
        frame->imageView = VK_NULL_HANDLE;
        frame->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        frame->compositionCompletionSemaphore = VK_NULL_HANDLE;
        frame->renderFence = VK_NULL_HANDLE;
        frame->presentFence = VK_NULL_HANDLE;
        frame->renderTimelineValue = 0;
        frame->presentTimelineValue = 0;
        frame->queuedAtNs = 0;
    }

    if (frame->width != requiredWidth || frame->height != requiredHeight)
    {
        frame->width = static_cast<u32>(requiredWidth);
        frame->height = static_cast<u32>(requiredHeight);
    }

    if (backend == FrameBackend::VulkanImage)
        frame->frameTexture = 0;

    if (backend == FrameBackend::OpenGlTexture)
        frame->renderTimelineValue = 0;
}

void FrameQueue::pushRenderedFrame(Frame* frame, const FrameQueuePolicy& requestedPolicy)
{
    std::unique_lock lock(frameLock);
    if (frame == nullptr || frame->state != FrameQueueState::Rendering)
    {
        stats.StateTransitionFailures++;
        return;
    }
    if (!frameMatchesActiveGenerationsLocked(frame))
    {
        stats.GenerationMismatchDropped++;
        stats.RenderFramesDiscarded++;
        retireFrameLocked(frame);
        return;
    }
    if (!transitionFrameLocked(frame, FrameQueueState::Rendering, FrameQueueState::Ready))
        return;

    const FrameQueuePolicy policy = sanitizePolicy(requestedPolicy);
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

void FrameQueue::discardRenderedFrame(Frame* frame)
{
    std::unique_lock lock(frameLock);
    if (frame == nullptr || frame->state != FrameQueueState::Rendering)
    {
        stats.StateTransitionFailures++;
        return;
    }
    retireFrameLocked(frame);
    stats.RenderFramesDiscarded++;
}

void FrameQueue::requestPresentationResync()
{
    std::unique_lock lock(frameLock);

    for (auto f : presentQueue)
        retireFrameLocked(f);

    presentQueue.clear();
    if (pendingPresentFrame != nullptr)
    {
        Frame* pendingFrame = pendingPresentFrame;
        if (previousFrame == pendingFrame)
            previousFrame = nullptr;
        pendingPresentFrame->presentationReferences =
            pendingPresentFrame->presentTimelineValue != 0 ? 1u : 0u;
        retireFrameLocked(pendingFrame);
        pendingPresentFrame = nullptr;
        pendingPresentReusesPrevious = false;
    }

    if (previousFrame != nullptr)
    {
        retireFrameLocked(previousFrame);
        previousFrame = nullptr;
    }

    // A resync invalidates the presentation contract for all in-flight frames:
    // scale, backend, packed buffers, and 3D source image may all have changed.
    // Reusing the previous frame after this point mixes old frame ownership with
    // the new configuration and reopens flicker/corruption on IR changes.
    suppressPreviousFrameReuse = true;
    updateBacklogStatsLocked();
}

void FrameQueue::requestFastForwardPresentationTransition()
{
    std::unique_lock lock(frameLock);

    for (auto f : presentQueue)
        retireFrameLocked(f);

    presentQueue.clear();
    if (pendingPresentFrame != nullptr)
    {
        if (pendingPresentFrame->presentationReferences != 0)
            pendingPresentFrame->presentationReferences--;
        if (pendingPresentReusesPrevious)
        {
            transitionFrameLocked(
                pendingPresentFrame,
                FrameQueueState::AcquiredForPresentation,
                FrameQueueState::Previous);
        }
        else
        {
            retireFrameLocked(pendingPresentFrame);
        }
        pendingPresentFrame = nullptr;
        pendingPresentReusesPrevious = false;
    }

    suppressPreviousFrameReuse = false;
    updateBacklogStatsLocked();
}

void FrameQueue::setActiveGenerations(u64 rendererGeneration, u64 surfaceGeneration)
{
    std::unique_lock lock(frameLock);
    activeRendererGeneration = rendererGeneration;
    activeSurfaceGeneration = surfaceGeneration;
    discardGenerationMismatchesLocked();
}

void FrameQueue::synchronizeHistoryReferences(
    const std::function<bool(const Frame*)>& isReferenced)
{
    std::unique_lock lock(frameLock);
    for (auto& frame : frames)
    {
        const bool referenced = isReferenced && isReferenced(&frame);
        frame.historyReferences = referenced ? 1u : 0u;
        if (referenced && frame.state == FrameQueueState::Free)
        {
            transitionFrameLocked(
                &frame, FrameQueueState::Free, FrameQueueState::HistoryReferenced);
        }
        else if (!referenced
            && frame.state == FrameQueueState::HistoryReferenced
            && frame.presentationReferences == 0)
        {
            transitionFrameLocked(
                &frame, FrameQueueState::HistoryReferenced, FrameQueueState::Free);
        }
    }
    rebuildFreeQueueLocked();
}

void FrameQueue::synchronizePresentationCompletion(
    const std::function<bool(Frame*)>& isComplete)
{
    std::unique_lock lock(frameLock);
    for (auto& frame : frames)
    {
        if (frame.presentationReferences == 0
            || frame.state == FrameQueueState::AcquiredForPresentation)
        {
            continue;
        }
        if (!isComplete || !isComplete(&frame))
            continue;

        frame.presentationReferences = 0;
        if (frame.state == FrameQueueState::HistoryReferenced
            && frame.historyReferences == 0)
        {
            transitionFrameLocked(
                &frame, FrameQueueState::HistoryReferenced, FrameQueueState::Free);
        }
    }
    rebuildFreeQueueLocked();
}

void FrameQueue::clear()
{
    std::unique_lock lock(frameLock);

    presentQueue.clear();
    previousFrame = nullptr;
    pendingPresentFrame = nullptr;
    pendingPresentReusesPrevious = false;
    suppressPreviousFrameReuse = false;
    stats = FrameQueueStats{};

    for (auto& frame : frames)
    {
        frame.backend = FrameBackend::VulkanImage;
        frame.frameTexture = 0;
        frame.width = 0;
        frame.height = 0;
        frame.frameId = 0;
        frame.frameSerial = 0;
        frame.rendererGeneration = 0;
        frame.surfaceGeneration = 0;
        frame.image = VK_NULL_HANDLE;
        frame.imageView = VK_NULL_HANDLE;
        frame.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        frame.compositionCompletionSemaphore = VK_NULL_HANDLE;
        frame.renderFence = VK_NULL_HANDLE;
        frame.presentFence = VK_NULL_HANDLE;
        frame.renderTimelineValue = 0;
        frame.presentTimelineValue = 0;
        frame.queuedAtNs = 0;
        frame.historyReferences = 0;
        frame.presentationReferences = 0;
        transitionFrameLocked(&frame, frame.state, FrameQueueState::Free);
    }
    activeRendererGeneration = 0;
    activeSurfaceGeneration = 0;
    rebuildFreeQueueLocked();
}

FrameQueueStats FrameQueue::takeStatsSnapshotAndReset()
{
    std::unique_lock lock(frameLock);
    stats.CurrentBacklogDepth = static_cast<u64>(presentQueue.size());
    FrameQueueStats snapshot = stats;
    stats = FrameQueueStats{};
    return snapshot;
}

void FrameQueue::updateBacklogStatsLocked()
{
    const u64 backlogDepth = static_cast<u64>(presentQueue.size());
    stats.CurrentBacklogDepth = backlogDepth;
    stats.MaxBacklogDepth = std::max(stats.MaxBacklogDepth, backlogDepth);
}

void FrameQueue::rebuildFreeQueueLocked()
{
    std::queue<Frame*> emptyQueue;
    std::swap(freeQueue, emptyQueue);

    for (auto& frame : frames)
    {
        if (frame.state == FrameQueueState::Free
            && frame.historyReferences == 0
            && frame.presentationReferences == 0)
        {
            freeQueue.push(&frame);
        }
    }
}

void FrameQueue::dropPendingFramesToBacklogLocked(u64 maxBacklogDepth, bool treatAsFastForwardSkip)
{
    const u64 nowNs = MelonDSAndroid::PerfNowNs();
    while (static_cast<u64>(presentQueue.size()) > maxBacklogDepth && !presentQueue.empty())
    {
        Frame* frame = presentQueue.back();
        presentQueue.pop_back();
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
        retireFrameLocked(frame);
    }
    updateBacklogStatsLocked();
}

void FrameQueue::recordPresentedFrameAgeLocked(Frame* frame, u64 nowNs)
{
    if (frame == nullptr || frame->queuedAtNs == 0 || nowNs < frame->queuedAtNs)
        return;

    const u64 ageNs = nowNs - frame->queuedAtNs;
    stats.PresentedFrameAgeTotalNs += ageNs;
    stats.PresentedFrameAgeMaxNs = std::max(stats.PresentedFrameAgeMaxNs, ageNs);
    stats.PresentedFrameAgeSamples++;
}

void FrameQueue::recordDroppedFrameLocked(Frame* frame, PresentDropCause cause, u64 nowNs)
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
