#pragma once
// MELONPRIME_VULKAN_REFERENCE_PORT_V0_V5_V1 desktop-only queue
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <queue>
#include <volk.h>
#include "types.h"
using namespace melonDS;
constexpr std::size_t FRAME_QUEUE_SIZE=9;
struct FrameQueuePolicy{u64 MaxBacklogDepth=FRAME_QUEUE_SIZE-1;bool AllowStealPending=true,AllowPreviousFrameReuse=true,AllowDropForDeadline=false,PreferOldestFrame=false,PreserveBacklogOnPresent=false,TreatBacklogTrimAsFastForwardSkip=false,UseLegacyOpenGlQueue=false;};
enum class FrameBackend:u8{OpenGlTexture=0,VulkanImage=1};
struct FrameQueueStats{u64 RenderFramesAcquired=0,RenderFramesQueued=0,RenderFramesDiscarded=0,PresentFramesReturned=0,StaleFramesDropped=0,PendingFramesStolenForRender=0,RenderFramesDroppedByPolicy=0,PresentFramesDroppedByPolicy=0,PresentDroppedByStale=0,PresentDroppedBySteal=0,PresentDroppedByDeadline=0,PresentDroppedByBacklogTrim=0,PresentDeferredByDeadline=0,FastForwardFramesSkipped=0,PreviousFrameReused=0,MaxBacklogDepth=0,CurrentBacklogDepth=0,PresentedFrameAgeTotalNs=0,PresentedFrameAgeMaxNs=0,PresentedFrameAgeSamples=0,DroppedFrameAgeTotalNs=0,DroppedFrameAgeMaxNs=0,DroppedFrameAgeSamples=0;};
struct Frame{FrameBackend backend=FrameBackend::VulkanImage;unsigned int frameTexture=0;u32 width=0,height=0;u64 frameId=0;VkFence renderFence=VK_NULL_HANDLE;VkFence presentFence=VK_NULL_HANDLE;u64 renderTimelineValue=0,presentTimelineValue=0,queuedAtNs=0;};
class FrameQueue{public:FrameQueue();Frame* getRenderFrame(const FrameQueuePolicy&);Frame* getPresentFrame(const FrameQueuePolicy&,std::optional<std::chrono::time_point<std::chrono::steady_clock>>);Frame* getPresentCandidate(const FrameQueuePolicy&,std::optional<std::chrono::time_point<std::chrono::steady_clock>>);Frame* getReusablePreviousFrame(const FrameQueuePolicy&);void recycleRenderFrame(Frame*);void commitPresentedFrame(Frame*,const FrameQueuePolicy&);void deferPresentedFrame(Frame*,const FrameQueuePolicy&);void validateRenderFrame(Frame*,int,int,FrameBackend);void pushRenderedFrame(Frame*,const FrameQueuePolicy&);void discardRenderedFrame(Frame*);void requestPresentationResync();void requestFastForwardPresentationTransition();void clear();FrameQueueStats takeStatsSnapshotAndReset();private:std::mutex m;std::condition_variable cv;std::array<Frame,FRAME_QUEUE_SIZE> frames{};std::queue<Frame*> freeQ;std::deque<Frame*> presentQ;Frame* previous=nullptr;Frame* pending=nullptr;bool suppress=false;u64 nextId=1;FrameQueueStats stats{};};
