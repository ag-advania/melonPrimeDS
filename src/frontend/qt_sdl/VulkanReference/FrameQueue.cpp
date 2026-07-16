// Desktop FrameQueue wrapper: Sapphire selection core + lifetime tracker (S75-5).
// MELONPRIME_SAPPHIRE_REBUILD bypasses DesktopFrameLifetimeTracker (pure Sapphire).

#include "FrameQueue.h"

#include <array>
#include <cstdio>
#include <functional>
#include <memory>

#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
#include "DesktopFrameLifetimeTracker.h"
#endif
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

#if defined(MELONPRIME_SAPPHIRE_REBUILD)

struct FrameQueue::Impl
{
    MelonDSAndroid::SapphireFrameQueueCore core;
};

#else

struct FrameQueue::Impl
{
    MelonDSAndroid::SapphireFrameQueueCore core;
    MelonDSAndroid::DesktopFrameLifetimeTracker lifetime;
};

#endif

FrameQueue::FrameQueue()
    : impl_(std::make_unique<Impl>())
{
}

FrameQueue::~FrameQueue() = default;

Frame* FrameQueue::getRenderFrame(const FrameQueuePolicy& policy)
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.prepareForSelection(impl_->core);
#endif
    Frame* frame = impl_->core.getRenderFrame(policy);
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    if (frame == nullptr)
        return nullptr;
    impl_->lifetime.onRenderAcquired(frame, impl_->core);
#endif
    return frame;
}

Frame* FrameQueue::getPresentFrame(
    const FrameQueuePolicy& policy,
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline)
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.prepareForSelection(impl_->core);
#endif
    return impl_->core.getPresentFrame(policy, deadline);
}

Frame* FrameQueue::getPresentCandidate(
    const FrameQueuePolicy& policy,
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline)
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.prepareForSelection(impl_->core);
#endif
    Frame* frame = impl_->core.getPresentCandidate(policy, deadline);
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    if (frame != nullptr)
        impl_->lifetime.onPresentationAcquired(frame, impl_->core);
#endif
    return frame;
}

Frame* FrameQueue::getReusablePreviousFrame(const FrameQueuePolicy& policy)
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.prepareForSelection(impl_->core);
#endif
    return impl_->core.getReusablePreviousFrame(policy);
}

void FrameQueue::recycleRenderFrame(Frame* frame)
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.onRecycleRender(frame, impl_->core);
#endif
    impl_->core.recycleRenderFrame(frame);
}

void FrameQueue::commitPresentedFrame(Frame* frame, const FrameQueuePolicy& policy)
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.onPresentationCommitted(frame, impl_->core);
#endif
    impl_->core.commitPresentedFrame(frame, policy);
}

void FrameQueue::deferPresentedFrame(Frame* frame, const FrameQueuePolicy& policy)
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.onPresentationDeferred(frame, impl_->core);
#endif
    impl_->core.deferPresentedFrame(frame, policy);
}

void FrameQueue::validateRenderFrame(
    Frame* frame,
    int requiredWidth,
    int requiredHeight,
    FrameBackend backend)
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.onValidateRender(frame, requiredWidth, requiredHeight, backend);
#else
    (void)frame;
    (void)requiredWidth;
    (void)requiredHeight;
    (void)backend;
#endif
}

void FrameQueue::pushRenderedFrame(Frame* frame, const FrameQueuePolicy& policy)
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    if (!impl_->lifetime.onPushRendered(frame, impl_->core))
        return;
#endif
    impl_->core.pushRenderedFrame(frame, policy);
}

void FrameQueue::discardRenderedFrame(Frame* frame)
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.onDiscardRendered(frame, impl_->core);
#endif
    impl_->core.discardRenderedFrame(frame);
}

void FrameQueue::requestPresentationResync()
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.onPresentationResync(impl_->core);
#else
    impl_->core.requestPresentationResync();
#endif
}

void FrameQueue::requestFastForwardPresentationTransition()
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.onFastForwardPresentationTransition(impl_->core);
#else
    impl_->core.requestFastForwardPresentationTransition();
#endif
}

void FrameQueue::setActiveGenerations(u64 rendererGeneration, u64 surfaceGeneration)
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.setActiveGenerations(rendererGeneration, surfaceGeneration);
    impl_->lifetime.prepareForSelection(impl_->core);
#else
    (void)rendererGeneration;
    (void)surfaceGeneration;
#endif
}

void FrameQueue::synchronizeHistoryReferences(
    const std::function<bool(const Frame*)>& isReferenced)
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.synchronizeHistoryReferences(impl_->core, isReferenced);
#else
    (void)isReferenced;
#endif
}

void FrameQueue::synchronizePresentationCompletion(u64 completedTimelineValue)
{
#if !defined(MELONPRIME_SAPPHIRE_REBUILD)
    impl_->lifetime.synchronizePresentationCompletion(impl_->core, completedTimelineValue);
#else
    (void)completedTimelineValue;
#endif
}

void FrameQueue::clear()
{
#if defined(MELONPRIME_SAPPHIRE_REBUILD)
    auto& core = impl_->core;
    std::unique_lock lock(core.frameLock);

    for (Frame* queued : core.presentQueue)
    {
        queued->queuedAtNs = 0;
        core.freeQueue.push(queued);
    }

    core.presentQueue.clear();
    core.previousFrame = nullptr;
    core.pendingPresentFrame = nullptr;
    core.suppressPreviousFrameReuse = false;
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

    core.rebuildFreeQueueLocked();
#else
    impl_->lifetime.onClear(impl_->core);
#endif
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
