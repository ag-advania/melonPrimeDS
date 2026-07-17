#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <queue>
#include <vulkan/vulkan.h>
#include "types.h"

namespace MelonPrime
{

using namespace melonDS;

// 9 frames should allow the emulator to run up 8x speed. This includes 8 frames ready to present, plus one frame currently being rendered to.
constexpr std::size_t MELONPRIME_VULKAN_FRAME_QUEUE_SIZE = 9;

struct MelonPrimeVulkanFrameQueuePolicy
{
    u64 MaxBacklogDepth = MELONPRIME_VULKAN_FRAME_QUEUE_SIZE - 1;
    bool AllowStealPending = true;
    bool AllowPreviousFrameReuse = true;
    bool AllowDropForDeadline = false;
    bool PreferOldestFrame = false;
    bool PreserveBacklogOnPresent = false;
    bool TreatBacklogTrimAsFastForwardSkip = false;
    bool UseLegacyOpenGlQueue = false;
};

enum class FrameBackend : u8 {
    OpenGlTexture = 0,
    VulkanImage = 1,
};

struct MelonPrimeVulkanFrameQueueStats
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
};

struct VulkanFrame {
    FrameBackend backend{FrameBackend::OpenGlTexture};
    VkImage frameImage{VK_NULL_HANDLE};
    u32 width{};
    u32 height{};
    u64 frameId{};
    VkFence renderFence{VK_NULL_HANDLE};
    VkFence presentFence{VK_NULL_HANDLE};
    u64 renderTimelineValue{};
    u64 presentTimelineValue{};
    u64 queuedAtNs{};
};

class MelonPrimeVulkanFrameQueue
{
public:
    MelonPrimeVulkanFrameQueue();
    VulkanFrame* getRenderFrame(const MelonPrimeVulkanFrameQueuePolicy& policy);
    VulkanFrame* getPresentFrame(const MelonPrimeVulkanFrameQueuePolicy& policy, std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline);
    VulkanFrame* getPresentCandidate(const MelonPrimeVulkanFrameQueuePolicy& policy, std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline);
    VulkanFrame* getReusablePreviousFrame(const MelonPrimeVulkanFrameQueuePolicy& policy);
    void recycleRenderFrame(VulkanFrame* frame);
    void commitPresentedFrame(VulkanFrame* frame, const MelonPrimeVulkanFrameQueuePolicy& policy);
    void deferPresentedFrame(VulkanFrame* frame, const MelonPrimeVulkanFrameQueuePolicy& policy);
    void validateRenderFrame(VulkanFrame* frame, int requiredWidth, int requiredHeight, FrameBackend backend);
    void pushRenderedFrame(VulkanFrame* frame, const MelonPrimeVulkanFrameQueuePolicy& policy);
    void discardRenderedFrame(VulkanFrame* frame);
    void requestPresentationResync();
    void requestFastForwardPresentationTransition();
    void clear();
    MelonPrimeVulkanFrameQueueStats takeStatsSnapshotAndReset();

private:
    enum class PresentDropCause : u8
    {
        Stale = 0,
        StealForRender = 1,
        Deadline = 2,
        BacklogTrim = 3,
    };

    static MelonPrimeVulkanFrameQueuePolicy sanitizePolicy(MelonPrimeVulkanFrameQueuePolicy policy);
    void rebuildFreeQueueLocked();
    void dropPendingFramesToBacklogLocked(u64 maxBacklogDepth, bool treatAsFastForwardSkip);
    void updateBacklogStatsLocked();
    void recordPresentedFrameAgeLocked(VulkanFrame* frame, u64 nowNs);
    void recordDroppedFrameLocked(VulkanFrame* frame, PresentDropCause cause, u64 nowNs);

private:
    std::mutex frameLock;
    std::condition_variable presentFrameReadyCondition;
    std::array<VulkanFrame, MELONPRIME_VULKAN_FRAME_QUEUE_SIZE> frames{};
    std::queue<VulkanFrame*> freeQueue{};
    std::deque<VulkanFrame*> presentQueue{};
    VulkanFrame* previousFrame = nullptr;
    VulkanFrame* pendingPresentFrame = nullptr;
    bool suppressPreviousFrameReuse = false;
    u64 nextFrameId = 1;
    MelonPrimeVulkanFrameQueueStats stats{};
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
