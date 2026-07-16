// Desktop FrameQueue wrapper: Sapphire selection core + lifetime tracker (S75-5).

#include "FrameQueue.h"

#include <array>
#include <cstdio>
#include <functional>
#include <memory>

#include "DesktopFrameLifetimeTracker.h"
#include "SapphireGenerated/SapphireFrameQueueCore.h"

u32 FrameQueue::membershipCountForFrame(
    const MelonDSAndroid::SapphireFrameQueueCore& core,
    const Frame* frame,
    const Frame* renderingFrame)
{
    if (frame == nullptr)
        return 0;

    u32 count = 0;
    if (core.pendingPresentFrame == frame)
        ++count;
    if (core.previousFrame == frame)
        ++count;
    if (renderingFrame == frame)
        ++count;

    for (const Frame* queued : core.presentQueue)
    {
        if (queued == frame)
            ++count;
    }

    std::queue<Frame*> freeQueueCopy = core.freeQueue;
    while (!freeQueueCopy.empty())
    {
        if (freeQueueCopy.front() == frame)
            ++count;
        freeQueueCopy.pop();
    }

    return count;
}

struct FrameQueue::Impl
{
    MelonDSAndroid::SapphireFrameQueueCore core;
    MelonDSAndroid::DesktopFrameLifetimeTracker lifetime;
};

FrameQueue::FrameQueue()
    : impl_(std::make_unique<Impl>())
{
}

FrameQueue::~FrameQueue() = default;

Frame* FrameQueue::getRenderFrame(const FrameQueuePolicy& policy)
{
    impl_->lifetime.prepareForSelection(impl_->core);
    Frame* frame = impl_->core.getRenderFrame(policy);
    if (frame == nullptr)
        return nullptr;
    impl_->lifetime.onRenderAcquired(frame, impl_->core);
    return frame;
}

Frame* FrameQueue::getPresentFrame(
    const FrameQueuePolicy& policy,
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline)
{
    impl_->lifetime.prepareForSelection(impl_->core);
    return impl_->core.getPresentFrame(policy, deadline);
}

Frame* FrameQueue::getPresentCandidate(
    const FrameQueuePolicy& policy,
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline)
{
    impl_->lifetime.prepareForSelection(impl_->core);
    Frame* frame = impl_->core.getPresentCandidate(policy, deadline);
    if (frame != nullptr)
        impl_->lifetime.onPresentationAcquired(frame, impl_->core);
    return frame;
}

Frame* FrameQueue::getReusablePreviousFrame(const FrameQueuePolicy& policy)
{
    impl_->lifetime.prepareForSelection(impl_->core);
    return impl_->core.getReusablePreviousFrame(policy);
}

void FrameQueue::recycleRenderFrame(Frame* frame)
{
    impl_->lifetime.onRecycleRender(frame, impl_->core);
    impl_->core.recycleRenderFrame(frame);
}

void FrameQueue::commitPresentedFrame(Frame* frame, const FrameQueuePolicy& policy)
{
    impl_->lifetime.onPresentationCommitted(frame, impl_->core);
    impl_->core.commitPresentedFrame(frame, policy);
}

void FrameQueue::deferPresentedFrame(Frame* frame, const FrameQueuePolicy& policy)
{
    impl_->lifetime.onPresentationDeferred(frame, impl_->core);
    impl_->core.deferPresentedFrame(frame, policy);
}

void FrameQueue::validateRenderFrame(
    Frame* frame,
    int requiredWidth,
    int requiredHeight,
    FrameBackend backend)
{
    impl_->lifetime.onValidateRender(frame, requiredWidth, requiredHeight, backend);
}

void FrameQueue::pushRenderedFrame(Frame* frame, const FrameQueuePolicy& policy)
{
    if (!impl_->lifetime.onPushRendered(frame, impl_->core))
        return;
    impl_->core.pushRenderedFrame(frame, policy);
}

void FrameQueue::discardRenderedFrame(Frame* frame)
{
    impl_->lifetime.onDiscardRendered(frame, impl_->core);
    impl_->core.discardRenderedFrame(frame);
}

void FrameQueue::requestPresentationResync()
{
    impl_->lifetime.onPresentationResync(impl_->core);
}

void FrameQueue::requestFastForwardPresentationTransition()
{
    impl_->lifetime.onFastForwardPresentationTransition(impl_->core);
}

void FrameQueue::setActiveGenerations(u64 rendererGeneration, u64 surfaceGeneration)
{
    impl_->lifetime.setActiveGenerations(rendererGeneration, surfaceGeneration);
    impl_->lifetime.prepareForSelection(impl_->core);
}

void FrameQueue::synchronizeHistoryReferences(
    const std::function<bool(const Frame*)>& isReferenced)
{
    impl_->lifetime.synchronizeHistoryReferences(impl_->core, isReferenced);
}

void FrameQueue::synchronizePresentationCompletion(u64 completedTimelineValue)
{
    impl_->lifetime.synchronizePresentationCompletion(impl_->core, completedTimelineValue);
}

void FrameQueue::clear()
{
    impl_->lifetime.onClear(impl_->core);
}

FrameQueueStats FrameQueue::takeStatsSnapshotAndReset()
{
    return impl_->core.takeStatsSnapshotAndReset();
}

void FrameQueue::assertMembershipInvariant(const Frame* renderingFrame) const
{
#ifndef NDEBUG
    const auto& core = impl_->core;
    std::array<u32, FRAME_QUEUE_SIZE> freeCounts{};
    std::array<u32, FRAME_QUEUE_SIZE> presentCounts{};

    std::queue<Frame*> freeQueueCopy = core.freeQueue;
    while (!freeQueueCopy.empty())
    {
        Frame* frame = freeQueueCopy.front();
        freeQueueCopy.pop();
        if (frame == nullptr)
            continue;
        const std::size_t index = static_cast<std::size_t>(frame - &core.frames_[0]);
        if (index < FRAME_QUEUE_SIZE)
            ++freeCounts[index];
    }

    for (const Frame* frame : core.presentQueue)
    {
        if (frame == nullptr)
            continue;
        const std::size_t index = static_cast<std::size_t>(frame - &core.frames_[0]);
        if (index < FRAME_QUEUE_SIZE)
            ++presentCounts[index];
    }

    for (const Frame& frame : core.frames_)
    {
        if (FrameQueue::membershipCountForFrame(core, &frame, renderingFrame) > 1)
        {
            std::fprintf(stderr, "FrameQueue membership invariant failed\n");
            std::abort();
        }
    }

    for (std::size_t index = 0; index < FRAME_QUEUE_SIZE; ++index)
    {
        if (freeCounts[index] > 1 || presentCounts[index] > 1)
        {
            std::fprintf(stderr, "FrameQueue duplicate queue entry invariant failed\n");
            std::abort();
        }
    }

    if (core.pendingPresentFrame != nullptr && presentCounts[
            static_cast<std::size_t>(core.pendingPresentFrame - &core.frames_[0])] != 0)
    {
        std::fprintf(stderr, "FrameQueue pending/present overlap invariant failed\n");
        std::abort();
    }

    if (core.previousFrame != nullptr
        && freeCounts[static_cast<std::size_t>(core.previousFrame - &core.frames_[0])] != 0)
    {
        std::fprintf(stderr, "FrameQueue previous/free overlap invariant failed\n");
        std::abort();
    }
#else
    (void)renderingFrame;
#endif
}
