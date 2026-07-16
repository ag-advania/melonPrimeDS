#pragma once

#include <functional>

#include "types.h"
#include "VulkanReference/FrameQueue.h"

namespace MelonDSAndroid
{

class SapphireFrameQueueCore;

class DesktopFrameLifetimeTracker
{
public:
    void setActiveGenerations(melonDS::u64 rendererGeneration, melonDS::u64 surfaceGeneration);
    void synchronizeHistoryReferences(
        SapphireFrameQueueCore& core,
        const std::function<bool(const Frame*)>& isReferenced);
    void synchronizePresentationCompletion(
        SapphireFrameQueueCore& core,
        melonDS::u64 completedTimelineValue);

    void prepareForSelection(SapphireFrameQueueCore& core);
    bool allowRenderAcquisition(const Frame* frame) const;
    bool allowPresentationAcquisition(const Frame* frame) const;
    void onRenderAcquired(Frame* frame, SapphireFrameQueueCore& core);
    void undoRenderAcquisition(Frame* frame, SapphireFrameQueueCore& core);
    void onPresentationAcquired(Frame* frame, SapphireFrameQueueCore& core);
    void onPresentationCommitted(Frame* frame, SapphireFrameQueueCore& core);
    void onPresentationResync(SapphireFrameQueueCore& core);
    void onFastForwardPresentationTransition(SapphireFrameQueueCore& core);
    void onPresentationDeferred(Frame* frame, SapphireFrameQueueCore& core);
    bool onPushRendered(Frame* frame, SapphireFrameQueueCore& core);
    void onRecycleRender(Frame* frame, SapphireFrameQueueCore& core);
    void onDiscardRendered(Frame* frame, SapphireFrameQueueCore& core);
    void onValidateRender(Frame* frame, int requiredWidth, int requiredHeight, FrameBackend backend);
    void onClear(SapphireFrameQueueCore& core);

private:
    bool transitionFrameLocked(
        Frame* frame,
        FrameQueueState expected,
        FrameQueueState next,
        FrameQueueStats& stats);
    void retireFrameLocked(Frame* frame, SapphireFrameQueueCore& core);
    void enqueueRetiredFrameLocked(Frame* frame, SapphireFrameQueueCore& core);
    void rebuildFreeQueueLocked(SapphireFrameQueueCore& core);
    void sanitizeFreeQueueLocked(SapphireFrameQueueCore& core);
    void detachPresentationOwnershipLocked(SapphireFrameQueueCore& core);
    bool frameMatchesActiveGenerations(const Frame* frame) const;
    void discardGenerationMismatches(SapphireFrameQueueCore& core);

    melonDS::u64 activeRendererGeneration_ = 0;
    melonDS::u64 activeSurfaceGeneration_ = 0;
};

} // namespace MelonDSAndroid
