#include "DesktopVulkanResourceLease.h"

#include <algorithm>

namespace MelonDSAndroid
{

void DesktopVulkanResourceLease::recordRenderAcquisition(Frame* frame)
{
    if (frame == nullptr)
        return;

    activeRenderLeases_.erase(
        std::remove(activeRenderLeases_.begin(), activeRenderLeases_.end(), frame),
        activeRenderLeases_.end());
    activeRenderLeases_.push_back(frame);
}

void DesktopVulkanResourceLease::releaseRenderLease(Frame* frame)
{
    if (frame == nullptr)
        return;

    activeRenderLeases_.erase(
        std::remove(activeRenderLeases_.begin(), activeRenderLeases_.end(), frame),
        activeRenderLeases_.end());
}

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

bool DesktopVulkanResourceLease::isRenderLeased(const Frame* frame) const
{
    if (frame == nullptr)
        return false;

    return std::find(activeRenderLeases_.begin(), activeRenderLeases_.end(), frame)
        != activeRenderLeases_.end();
}

bool DesktopVulkanResourceLease::isFrameLeased(const Frame* frame) const
{
    if (frame == nullptr)
        return false;

    if (isRenderLeased(frame))
        return true;

    return std::any_of(
        activeLeases_.begin(),
        activeLeases_.end(),
        [&](const DesktopPresentationLease& lease) { return lease.frame == frame; });
}

void DesktopVulkanResourceLease::clear()
{
    activeLeases_.clear();
    activeRenderLeases_.clear();
}

} // namespace MelonDSAndroid
