#include "DesktopFrameLifetimeTracker.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <queue>

#include "SapphireGenerated/SapphireFrameQueueCore.h"
#include "VulkanPerfStats.h"

namespace MelonDSAndroid
{

namespace
{

void resetQueueContainersLocked(SapphireFrameQueueCore& core)
{
    std::unique_lock lock(core.frameLock);
    core.presentQueue.clear();
    core.previousFrame = nullptr;
    core.pendingPresentFrame = nullptr;
    core.suppressPreviousFrameReuse = false;
    std::queue<Frame*> emptyQueue;
    std::swap(core.freeQueue, emptyQueue);
}

void sanitizeCoreFreeQueueLocked(SapphireFrameQueueCore& core)
{
    std::unique_lock lock(core.frameLock);
    std::queue<Frame*> sanitized;
    std::array<bool, FRAME_QUEUE_SIZE> seen{};
    while (!core.freeQueue.empty())
    {
        Frame* frame = core.freeQueue.front();
        core.freeQueue.pop();
        if (frame == nullptr)
            continue;

        const std::size_t index = static_cast<std::size_t>(frame - &core.frames_[0]);
        if (index >= FRAME_QUEUE_SIZE || seen[index])
            continue;

        if (frame->queueState() != FrameQueueState::Free
            || frame->historyReferenceCount() != 0
            || frame->presentationReferenceCount() != 0)
        {
            continue;
        }

        seen[index] = true;
        sanitized.push(frame);
    }
    core.freeQueue = std::move(sanitized);
}

} // namespace

bool DesktopFrameLifetimeTracker::transitionFrameLocked(
    Frame* frame,
    FrameQueueState expected,
    FrameQueueState next,
    FrameQueueStats& stats)
{
    if (frame == nullptr || frame->queueState() != expected)
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

void DesktopFrameLifetimeTracker::retireFrameLocked(Frame* frame, SapphireFrameQueueCore& core)
{
    if (frame == nullptr || frame->queueState() == FrameQueueState::Free)
        return;

    frame->queuedAtNs = 0;
    if (frame->historyReferenceCount() != 0 || frame->presentationReferenceCount() != 0)
    {
        transitionFrameLocked(
            frame,
            frame->queueState(),
            FrameQueueState::HistoryReferenced,
            core.stats);
        core.stats.ReferenceBlockedReuse++;
        return;
    }

    if (transitionFrameLocked(frame, frame->queueState(), FrameQueueState::Free, core.stats))
        return;
}

void DesktopFrameLifetimeTracker::enqueueRetiredFrameLocked(
    Frame* frame,
    SapphireFrameQueueCore& core)
{
    if (frame == nullptr || frame->queueState() != FrameQueueState::Free)
        return;

    core.discardRenderedFrame(frame);
}

void DesktopFrameLifetimeTracker::sanitizeFreeQueueLocked(SapphireFrameQueueCore& core)
{
    sanitizeCoreFreeQueueLocked(core);
}

void DesktopFrameLifetimeTracker::detachPresentationOwnershipLocked(
    SapphireFrameQueueCore& core)
{
    auto detachFrame = [&](Frame* frame)
    {
        if (frame == nullptr)
            return;

        frame->queuedAtNs = 0;
        if (frame->historyReferenceCount() != 0
            || frame->presentationReferenceCount() != 0)
        {
            if (frame->queueState() != FrameQueueState::HistoryReferenced)
            {
                transitionFrameLocked(
                    frame,
                    frame->queueState(),
                    FrameQueueState::HistoryReferenced,
                    core.stats);
            }
            return;
        }

        if (frame->queueState() != FrameQueueState::Free)
            transitionFrameLocked(frame, frame->queueState(), FrameQueueState::Free, core.stats);
    };

    for (Frame* frame : core.presentQueue)
        detachFrame(frame);
    detachFrame(core.pendingPresentFrame);
    detachFrame(core.previousFrame);
}

void DesktopFrameLifetimeTracker::onPresentationResync(SapphireFrameQueueCore& core)
{
    detachPresentationOwnershipLocked(core);
    core.requestPresentationResync();
    sanitizeFreeQueueLocked(core);
    core.updateBacklogStatsLocked();
}

void DesktopFrameLifetimeTracker::onFastForwardPresentationTransition(
    SapphireFrameQueueCore& core)
{
    detachPresentationOwnershipLocked(core);
    core.requestFastForwardPresentationTransition();
    sanitizeFreeQueueLocked(core);
    core.updateBacklogStatsLocked();
}

void DesktopFrameLifetimeTracker::rebuildFreeQueueLocked(SapphireFrameQueueCore& core)
{
    core.rebuildFreeQueueLocked();
}

void DesktopFrameLifetimeTracker::setActiveGenerations(
    u64 rendererGeneration,
    u64 surfaceGeneration)
{
    activeRendererGeneration_ = rendererGeneration;
    activeSurfaceGeneration_ = surfaceGeneration;
}

bool DesktopFrameLifetimeTracker::frameMatchesActiveGenerations(const Frame* frame) const
{
    if (frame == nullptr)
        return false;
    if (activeRendererGeneration_ == 0 && activeSurfaceGeneration_ == 0)
        return true;
    if (frame->frameSerial == 0)
        return false;
    if (activeRendererGeneration_ != 0
        && frame->rendererGeneration != activeRendererGeneration_)
    {
        return false;
    }
    if (activeSurfaceGeneration_ != 0
        && frame->surfaceGeneration != activeSurfaceGeneration_)
    {
        return false;
    }
    return true;
}

void DesktopFrameLifetimeTracker::discardGenerationMismatches(SapphireFrameQueueCore& core)
{
    if (core.pendingPresentFrame != nullptr
        && !frameMatchesActiveGenerations(core.pendingPresentFrame))
    {
        Frame* frame = core.pendingPresentFrame;
        if (core.previousFrame == frame)
            core.previousFrame = nullptr;
        frame->presentationReferences = frame->presentTimelineValue != 0 ? 1u : 0u;
        core.pendingPresentFrame = nullptr;
        core.stats.GenerationMismatchDropped++;
        retireFrameLocked(frame, core);
        enqueueRetiredFrameLocked(frame, core);
    }

    if (core.previousFrame != nullptr
        && !frameMatchesActiveGenerations(core.previousFrame))
    {
        Frame* frame = core.previousFrame;
        core.previousFrame = nullptr;
        core.stats.GenerationMismatchDropped++;
        retireFrameLocked(frame, core);
        enqueueRetiredFrameLocked(frame, core);
    }

    auto iterator = core.presentQueue.begin();
    while (iterator != core.presentQueue.end())
    {
        Frame* frame = *iterator;
        if (frameMatchesActiveGenerations(frame))
        {
            ++iterator;
            continue;
        }

        iterator = core.presentQueue.erase(iterator);
        core.stats.GenerationMismatchDropped++;
        retireFrameLocked(frame, core);
        enqueueRetiredFrameLocked(frame, core);
    }
    core.updateBacklogStatsLocked();
}

void DesktopFrameLifetimeTracker::prepareForSelection(SapphireFrameQueueCore& core)
{
    discardGenerationMismatches(core);
}

bool DesktopFrameLifetimeTracker::allowRenderAcquisition(const Frame* frame) const
{
    return frame != nullptr
        && frame->historyReferenceCount() == 0
        && frame->presentationReferenceCount() == 0;
}

bool DesktopFrameLifetimeTracker::allowPresentationAcquisition(const Frame* frame) const
{
    return frame != nullptr && frame->presentationReferenceCount() == 0;
}

void DesktopFrameLifetimeTracker::onRenderAcquired(Frame* frame, SapphireFrameQueueCore& core)
{
    if (frame == nullptr)
        return;

    if (frame->queueState() == FrameQueueState::Free)
        transitionFrameLocked(frame, FrameQueueState::Free, FrameQueueState::Rendering, core.stats);
    else if (frame->queueState() == FrameQueueState::Ready)
        transitionFrameLocked(frame, FrameQueueState::Ready, FrameQueueState::Rendering, core.stats);

    frame->frameSerial = 0;
    frame->rendererGeneration = activeRendererGeneration_;
    frame->surfaceGeneration = activeSurfaceGeneration_;
}

void DesktopFrameLifetimeTracker::undoRenderAcquisition(
    Frame* frame,
    SapphireFrameQueueCore& core)
{
    if (frame == nullptr)
        return;

    core.stats.ReferenceBlockedReuse++;
    core.stats.RenderFramesDroppedByPolicy++;
    retireFrameLocked(frame, core);
    enqueueRetiredFrameLocked(frame, core);
}

void DesktopFrameLifetimeTracker::onPresentationAcquired(
    Frame* frame,
    SapphireFrameQueueCore& core)
{
    if (frame == nullptr)
        return;

    if (frame == core.previousFrame)
    {
        transitionFrameLocked(
            frame,
            FrameQueueState::Previous,
            FrameQueueState::AcquiredForPresentation,
            core.stats);
    }
    else if (frame->queueState() == FrameQueueState::Ready)
    {
        transitionFrameLocked(
            frame,
            FrameQueueState::Ready,
            FrameQueueState::AcquiredForPresentation,
            core.stats);
    }

    frame->presentationReferences++;
}

void DesktopFrameLifetimeTracker::onPresentationCommitted(
    Frame* frame,
    SapphireFrameQueueCore& core)
{
    if (frame == nullptr)
        return;

    if (frame->queueState() == FrameQueueState::AcquiredForPresentation)
    {
        transitionFrameLocked(
            frame,
            FrameQueueState::AcquiredForPresentation,
            FrameQueueState::Previous,
            core.stats);
    }

    frame->presentationReferences = frame->presentTimelineValue != 0 ? 1u : 0u;
}

void DesktopFrameLifetimeTracker::onPresentationDeferred(
    Frame* frame,
    SapphireFrameQueueCore& core)
{
    if (frame == nullptr)
        return;
    if (frame->presentationReferences != 0)
        frame->presentationReferences--;

    // Core deferPresentedFrame requeues to presentQueue. Never retire into freeQueue here.
    if (frame->queueState() == FrameQueueState::AcquiredForPresentation)
    {
        transitionFrameLocked(
            frame,
            FrameQueueState::AcquiredForPresentation,
            FrameQueueState::Ready,
            core.stats);
    }
}

bool DesktopFrameLifetimeTracker::onPushRendered(Frame* frame, SapphireFrameQueueCore& core)
{
    if (frame == nullptr || frame->queueState() != FrameQueueState::Rendering)
    {
        core.stats.StateTransitionFailures++;
        return false;
    }
    if (!frameMatchesActiveGenerations(frame))
    {
        core.stats.GenerationMismatchDropped++;
        core.stats.RenderFramesDiscarded++;
        retireFrameLocked(frame, core);
        enqueueRetiredFrameLocked(frame, core);
        return false;
    }
    transitionFrameLocked(frame, FrameQueueState::Rendering, FrameQueueState::Ready, core.stats);
    return true;
}

void DesktopFrameLifetimeTracker::onRecycleRender(Frame* frame, SapphireFrameQueueCore& core)
{
    if (frame == nullptr || frame->queueState() != FrameQueueState::Rendering)
    {
        core.stats.StateTransitionFailures++;
        return;
    }

    frame->queuedAtNs = 0;
    if (frame->historyReferenceCount() != 0 || frame->presentationReferenceCount() != 0)
    {
        transitionFrameLocked(
            frame,
            FrameQueueState::Rendering,
            FrameQueueState::HistoryReferenced,
            core.stats);
        core.stats.ReferenceBlockedReuse++;
        return;
    }

    // SapphireFrameQueueCore owns freeQueue membership on recycle/discard.
    transitionFrameLocked(frame, FrameQueueState::Rendering, FrameQueueState::Free, core.stats);
}

void DesktopFrameLifetimeTracker::onDiscardRendered(Frame* frame, SapphireFrameQueueCore& core)
{
    onRecycleRender(frame, core);
    core.stats.RenderFramesDiscarded++;
}

void DesktopFrameLifetimeTracker::onValidateRender(
    Frame* frame,
    int requiredWidth,
    int requiredHeight,
    FrameBackend backend)
{
    if (frame == nullptr || frame->queueState() != FrameQueueState::Rendering)
        return;

    if (frame->backend != backend)
    {
        frame->backend = backend;
        frame->frameTexture = 0;
        frame->width = 0;
        frame->height = 0;
        frame->frameSerial = 0;
        frame->surfaceGeneration = activeSurfaceGeneration_;
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

    if (frame->width != static_cast<u32>(requiredWidth)
        || frame->height != static_cast<u32>(requiredHeight))
    {
        frame->width = static_cast<u32>(requiredWidth);
        frame->height = static_cast<u32>(requiredHeight);
    }

    if (backend == FrameBackend::VulkanImage)
        frame->frameTexture = 0;
    if (backend == FrameBackend::OpenGlTexture)
        frame->renderTimelineValue = 0;
}

void DesktopFrameLifetimeTracker::onClear(SapphireFrameQueueCore& core)
{
    resetQueueContainersLocked(core);
    core.stats = FrameQueueStats{};

    for (auto& frame : core.frames_)
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
        frame.state = FrameQueueState::Free;
    }

    activeRendererGeneration_ = 0;
    activeSurfaceGeneration_ = 0;
    rebuildFreeQueueLocked(core);
}

void DesktopFrameLifetimeTracker::synchronizeHistoryReferences(
    SapphireFrameQueueCore& core,
    const std::function<bool(const Frame*)>& isReferenced)
{
    for (auto& frame : core.frames_)
    {
        const bool referenced = isReferenced && isReferenced(&frame);
        frame.historyReferences = referenced ? 1u : 0u;
        if (referenced && frame.queueState() == FrameQueueState::Free)
            frame.state = FrameQueueState::HistoryReferenced;
        else if (!referenced
            && frame.queueState() == FrameQueueState::HistoryReferenced
            && frame.presentationReferenceCount() == 0)
        {
            frame.state = FrameQueueState::Free;
        }
    }
    rebuildFreeQueueLocked(core);
}

void DesktopFrameLifetimeTracker::synchronizePresentationCompletion(
    SapphireFrameQueueCore& core,
    u64 completedTimelineValue)
{
    for (auto& frame : core.frames_)
    {
        if (frame.presentationReferenceCount() == 0
            || frame.queueState() == FrameQueueState::AcquiredForPresentation)
        {
            continue;
        }
        if (frame.presentTimelineValue != 0
            && frame.presentTimelineValue > completedTimelineValue)
        {
            continue;
        }

        frame.presentTimelineValue = 0;
        frame.presentationReferences = 0;
        if (frame.queueState() == FrameQueueState::HistoryReferenced
            && frame.historyReferenceCount() == 0)
        {
            frame.state = FrameQueueState::Free;
        }
    }
    rebuildFreeQueueLocked(core);
}

} // namespace MelonDSAndroid
