// Desktop FrameQueue wrapper: Sapphire selection core + lifetime tracker (S75-5).

#include "FrameQueue.h"

#include <functional>
#include <memory>

#include "DesktopFrameLifetimeTracker.h"
#include "SapphireGenerated/SapphireFrameQueueCore.h"

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
    if (!impl_->lifetime.allowRenderAcquisition(frame))
    {
        impl_->lifetime.undoRenderAcquisition(frame, impl_->core);
        return nullptr;
    }
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
    if (frame != nullptr && !impl_->lifetime.allowPresentationAcquisition(frame))
        return nullptr;
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
    impl_->lifetime.onPushRendered(frame, impl_->core);
    impl_->core.pushRenderedFrame(frame, policy);
}

void FrameQueue::discardRenderedFrame(Frame* frame)
{
    impl_->lifetime.onDiscardRendered(frame, impl_->core);
    impl_->core.discardRenderedFrame(frame);
}

void FrameQueue::requestPresentationResync()
{
    impl_->core.requestPresentationResync();
}

void FrameQueue::requestFastForwardPresentationTransition()
{
    impl_->core.requestFastForwardPresentationTransition();
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
