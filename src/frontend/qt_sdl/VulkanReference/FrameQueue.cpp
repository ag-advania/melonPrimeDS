#include "FrameQueue.h"
#include <algorithm>
static u64 nowNs(){return (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
FrameQueue::FrameQueue(){for(auto&f:frames)freeQ.push(&f);}
Frame* FrameQueue::getRenderFrame(const FrameQueuePolicy& p){std::lock_guard<std::mutex>l(m);Frame*f=nullptr;if(!freeQ.empty()){f=freeQ.front();freeQ.pop();}else if(p.AllowStealPending&&!presentQ.empty()){f=presentQ.front();presentQ.pop_front();stats.PendingFramesStolenForRender++;}if(f){f->frameId=nextId++;stats.RenderFramesAcquired++;}return f;}
Frame* FrameQueue::getPresentCandidate(const FrameQueuePolicy&p,std::optional<std::chrono::time_point<std::chrono::steady_clock>>){std::lock_guard<std::mutex>l(m);if(pending)return pending;if(presentQ.empty())return nullptr;pending=p.PreferOldestFrame?presentQ.front():presentQ.back();presentQ.erase(std::find(presentQ.begin(),presentQ.end(),pending));return pending;}
Frame* FrameQueue::getPresentFrame(const FrameQueuePolicy&p,std::optional<std::chrono::time_point<std::chrono::steady_clock>>d){return getPresentCandidate(p,d);}
Frame* FrameQueue::getReusablePreviousFrame(const FrameQueuePolicy&p){std::lock_guard<std::mutex>l(m);if(!suppress&&p.AllowPreviousFrameReuse&&previous){stats.PreviousFrameReused++;return previous;}return nullptr;}
void FrameQueue::recycleRenderFrame(Frame*f){if(!f)return;std::lock_guard<std::mutex>l(m);freeQ.push(f);}
void FrameQueue::commitPresentedFrame(Frame*f,const FrameQueuePolicy&){if(!f)return;std::lock_guard<std::mutex>l(m);if(previous&&previous!=f)freeQ.push(previous);previous=f;if(pending==f)pending=nullptr;suppress=false;stats.PresentFramesReturned++;}
void FrameQueue::deferPresentedFrame(Frame*f,const FrameQueuePolicy&){if(!f)return;std::lock_guard<std::mutex>l(m);if(pending==f)pending=nullptr;presentQ.push_front(f);stats.PresentDeferredByDeadline++;}
void FrameQueue::validateRenderFrame(Frame*f,int w,int h,FrameBackend b){if(f){f->width=(u32)w;f->height=(u32)h;f->backend=b;}}
void FrameQueue::pushRenderedFrame(Frame*f,const FrameQueuePolicy&p){if(!f)return;std::lock_guard<std::mutex>l(m);f->queuedAtNs=nowNs();presentQ.push_back(f);while(presentQ.size()>p.MaxBacklogDepth){freeQ.push(presentQ.front());presentQ.pop_front();stats.PresentDroppedByBacklogTrim++;}stats.RenderFramesQueued++;stats.CurrentBacklogDepth=presentQ.size();stats.MaxBacklogDepth=std::max(stats.MaxBacklogDepth,stats.CurrentBacklogDepth);cv.notify_all();}
void FrameQueue::discardRenderedFrame(Frame*f){if(!f)return;std::lock_guard<std::mutex>l(m);freeQ.push(f);stats.RenderFramesDiscarded++;}
void FrameQueue::requestPresentationResync(){std::lock_guard<std::mutex>l(m);suppress=true;}
void FrameQueue::requestFastForwardPresentationTransition(){requestPresentationResync();}
void FrameQueue::clear(){std::lock_guard<std::mutex>l(m);while(!freeQ.empty())freeQ.pop();presentQ.clear();pending=nullptr;previous=nullptr;for(auto&f:frames)freeQ.push(&f);suppress=false;}
FrameQueueStats FrameQueue::takeStatsSnapshotAndReset(){std::lock_guard<std::mutex>l(m);auto r=stats;stats={};return r;}
