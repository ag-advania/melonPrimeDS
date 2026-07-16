#pragma once

// Source: SapphireRhodonite/melonDS-android
// app/src/main/cpp/renderer/FrameQueue.h @ tag 0.7.0.rc4
// S75: Sapphire selection core + desktop lifetime tracker.

#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <volk.h>
#include "types.h"

using namespace melonDS;

namespace MelonDSAndroid
{
class DesktopFrameLifetimeTracker;
}

constexpr std::size_t FRAME_QUEUE_SIZE = 9;

struct FrameQueuePolicy
{
    u64 MaxBacklogDepth = FRAME_QUEUE_SIZE - 1;
    bool AllowStealPending = true;
    bool AllowPreviousFrameReuse = true;
    bool AllowDropForDeadline = false;
    bool PreferOldestFrame = false;
    bool PreserveBacklogOnPresent = false;
    bool TreatBacklogTrimAsFastForwardSkip = false;
    bool UseLegacyOpenGlQueue = false;
};

enum class FrameBackend : u8
{
    OpenGlTexture = 0,
    VulkanImage = 1,
};

enum class FrameQueueState : u8
{
    Free = 0,
    Rendering = 1,
    Ready = 2,
    AcquiredForPresentation = 3,
    Previous = 4,
    HistoryReferenced = 5,
};

struct FrameQueueStats
{
    u64 RenderFramesAcquired = 0;
    u64 RenderFramesQueued = 0;
    u64 RenderFramesDiscarded = 0;
    u64 PresentFramesReturned = 0;
    u64 StaleFramesDropped = 0;
    u64 PendingFramesStolenForRender = 0;
    u64 RenderFramesDroppedByPolicy = 0;
    u64 PresentFramesDroppedByPolicy = 0;
    u64 PresentDroppedByStale = 0;
    u64 PresentDroppedBySteal = 0;
    u64 PresentDroppedByDeadline = 0;
    u64 PresentDroppedByBacklogTrim = 0;
    u64 PresentDeferredByDeadline = 0;
    u64 FastForwardFramesSkipped = 0;
    u64 PreviousFrameReused = 0;
    u64 MaxBacklogDepth = 0;
    u64 CurrentBacklogDepth = 0;
    u64 PresentedFrameAgeTotalNs = 0;
    u64 PresentedFrameAgeMaxNs = 0;
    u64 PresentedFrameAgeSamples = 0;
    u64 DroppedFrameAgeTotalNs = 0;
    u64 DroppedFrameAgeMaxNs = 0;
    u64 DroppedFrameAgeSamples = 0;
    u64 GenerationMismatchDropped = 0;
    u64 ReferenceBlockedReuse = 0;
    u64 StateTransitionFailures = 0;
};

struct Frame
{
    FrameBackend backend{FrameBackend::VulkanImage};
    unsigned int frameTexture{};
    u32 width{};
    u32 height{};
    u64 frameId{};
    u64 frameSerial{};
    u64 rendererGeneration{};
    u64 surfaceGeneration{};
    VkImage image{VK_NULL_HANDLE};
    VkImageView imageView{VK_NULL_HANDLE};
    VkImageLayout imageLayout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkSemaphore compositionCompletionSemaphore{VK_NULL_HANDLE};
    VkFence renderFence{VK_NULL_HANDLE};
    VkFence presentFence{VK_NULL_HANDLE};
    u64 renderTimelineValue{};
    u64 presentTimelineValue{};
    u64 queuedAtNs{};

    [[nodiscard]] FrameQueueState queueState() const { return state; }
    [[nodiscard]] u32 historyReferenceCount() const { return historyReferences; }
    [[nodiscard]] u32 presentationReferenceCount() const { return presentationReferences; }

private:
    friend class FrameQueue;
    friend class MelonDSAndroid::DesktopFrameLifetimeTracker;
    FrameQueueState state{FrameQueueState::Free};
    u32 historyReferences{};
    u32 presentationReferences{};
};

class FrameQueue
{
public:
    FrameQueue();
    ~FrameQueue();
    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;

    Frame* getRenderFrame(const FrameQueuePolicy& policy);
    Frame* getPresentFrame(
        const FrameQueuePolicy& policy,
        std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline);
    Frame* getPresentCandidate(
        const FrameQueuePolicy& policy,
        std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline);
    Frame* getReusablePreviousFrame(const FrameQueuePolicy& policy);
    void recycleRenderFrame(Frame* frame);
    void commitPresentedFrame(Frame* frame, const FrameQueuePolicy& policy);
    void deferPresentedFrame(Frame* frame, const FrameQueuePolicy& policy);
    void validateRenderFrame(Frame* frame, int requiredWidth, int requiredHeight, FrameBackend backend);
    void pushRenderedFrame(Frame* frame, const FrameQueuePolicy& policy);
    void discardRenderedFrame(Frame* frame);
    void requestPresentationResync();
    void requestFastForwardPresentationTransition();
    void setActiveGenerations(u64 rendererGeneration, u64 surfaceGeneration);
    void synchronizeHistoryReferences(const std::function<bool(const Frame*)>& isReferenced);
    void synchronizePresentationCompletion(u64 completedTimelineValue);
    void clear();
    FrameQueueStats takeStatsSnapshotAndReset();
    void assertMembershipInvariant(const Frame* renderingFrame = nullptr) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
