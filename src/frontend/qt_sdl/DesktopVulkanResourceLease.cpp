#include "DesktopVulkanResourceLease.h"

#include <algorithm>

namespace MelonDSAndroid
{

void DesktopVulkanResourceLease::recordPresentationCommit(Frame* frame)
{
    if (frame == nullptr)
        return;

    DesktopPresentationLease lease;
    lease.frame = frame;
    lease.image = frame->image;
    lease.imageView = frame->imageView;
    lease.compositionCompletionSemaphore = frame->compositionCompletionSemaphore;
    lease.presentTimelineValue = frame->presentTimelineValue;
    lease.rendererGeneration = frame->rendererGeneration;
    lease.surfaceGeneration = frame->surfaceGeneration;
    activeLeases_.push_back(lease);
}

void DesktopVulkanResourceLease::releaseCompleted(u64 completedTimelineValue)
{
    activeLeases_.erase(
        std::remove_if(
            activeLeases_.begin(),
            activeLeases_.end(),
            [&](const DesktopPresentationLease& lease)
            {
                if (lease.presentTimelineValue == 0)
                    return false;
                return lease.presentTimelineValue <= completedTimelineValue;
            }),
        activeLeases_.end());
}

bool DesktopVulkanResourceLease::isFrameLeased(const Frame* frame) const
{
    if (frame == nullptr)
        return false;

    return std::any_of(
        activeLeases_.begin(),
        activeLeases_.end(),
        [&](const DesktopPresentationLease& lease) { return lease.frame == frame; });
}

void DesktopVulkanResourceLease::clear()
{
    activeLeases_.clear();
}

} // namespace MelonDSAndroid
