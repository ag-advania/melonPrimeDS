#pragma once

#include <deque>

#include <volk.h>

#include "VulkanReference/FrameQueue.h"

namespace MelonDSAndroid
{

struct DesktopPresentationLease
{
    Frame* frame = nullptr;
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSemaphore compositionCompletionSemaphore = VK_NULL_HANDLE;
    u64 presentTimelineValue = 0;
    u64 rendererGeneration = 0;
    u64 surfaceGeneration = 0;
};

class DesktopVulkanResourceLease
{
public:
    void recordPresentationCommit(Frame* frame);
    void releaseCompleted(u64 completedTimelineValue);
    [[nodiscard]] bool isFrameLeased(const Frame* frame) const;
    void clear();

private:
    std::deque<DesktopPresentationLease> activeLeases_;
};

} // namespace MelonDSAndroid
