/*
    Copyright 2016-2026 melonDS team

    Allocation-free scheduling state for VK_GOOGLE_display_timing.
*/

#ifndef VULKAN_GOOGLE_DISPLAY_TIMING_MODEL_H
#define VULKAN_GOOGLE_DISPLAY_TIMING_MODEL_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <limits>

#include "types.h"

namespace melonDS
{

constexpr u32 NextGooglePresentId(u32 current) noexcept
{
    const u32 next = current + 1;
    return next == 0 ? 1 : next;
}

struct VulkanGooglePresentRequest
{
    u32 PresentId = 0;
    u64 DesiredPresentTimeNs = 0;
};

struct VulkanGooglePresentationFeedback
{
    u32 PresentId = 0;
    u64 DesiredPresentTimeNs = 0;
    u64 ActualPresentTimeNs = 0;
    u64 EarliestPresentTimeNs = 0;
    u64 PresentMarginNs = 0;
};

enum class VulkanGoogleQueryStatus : int
{
    Success = 0,
    Incomplete,
    OutOfDate,
    DeviceLost,
    SurfaceLost,
    Failure,
};

enum class VulkanGoogleQueryAction : int
{
    Continue = 0,
    RebuildSwapchain,
    FailDevice,
    FailSurface,
    DisableBackend,
};

constexpr VulkanGoogleQueryAction VulkanGoogleActionFor(
    VulkanGoogleQueryStatus status) noexcept
{
    switch (status)
    {
    case VulkanGoogleQueryStatus::Success:
    case VulkanGoogleQueryStatus::Incomplete:
        return VulkanGoogleQueryAction::Continue;
    case VulkanGoogleQueryStatus::OutOfDate:
        return VulkanGoogleQueryAction::RebuildSwapchain;
    case VulkanGoogleQueryStatus::DeviceLost:
        return VulkanGoogleQueryAction::FailDevice;
    case VulkanGoogleQueryStatus::SurfaceLost:
        return VulkanGoogleQueryAction::FailSurface;
    case VulkanGoogleQueryStatus::Failure:
        return VulkanGoogleQueryAction::DisableBackend;
    }
    return VulkanGoogleQueryAction::DisableBackend;
}

class VulkanGoogleDisplayTimingModel
{
public:
    [[nodiscard]] VulkanGooglePresentRequest Prepare(
        u64 nowNs, u64 frameIntervalNs, bool requestTarget) noexcept
    {
        if (Pending)
            return PendingRequest;

        PendingRequest.PresentId = NextGooglePresentId(CommittedPresentId);
        PendingRequest.DesiredPresentTimeNs = requestTarget && frameIntervalNs != 0
            ? NextDesiredTime(nowNs, frameIntervalNs)
            : 0;
        Pending = true;
        return PendingRequest;
    }

    void Commit() noexcept
    {
        if (!Pending)
            return;
        CommittedPresentId = PendingRequest.PresentId;
        if (PendingRequest.DesiredPresentTimeNs != 0)
            CommittedDesiredPresentTimeNs = PendingRequest.DesiredPresentTimeNs;
        PendingRequest = {};
        Pending = false;
    }

    void Abandon() noexcept
    {
        PendingRequest = {};
        Pending = false;
    }

    void Reset() noexcept
    {
        CommittedPresentId = 0;
        CommittedDesiredPresentTimeNs = 0;
        PendingRequest = {};
        Pending = false;
        Feedback = {};
    }

    void RecordFeedback(const VulkanGooglePresentationFeedback& feedback) noexcept
    {
        Feedback = feedback;
    }

    [[nodiscard]] u32 GetCommittedPresentId() const noexcept { return CommittedPresentId; }
    [[nodiscard]] u64 GetCommittedDesiredPresentTimeNs() const noexcept
    {
        return CommittedDesiredPresentTimeNs;
    }
    [[nodiscard]] const VulkanGooglePresentationFeedback& GetFeedback() const noexcept
    {
        return Feedback;
    }

private:
    [[nodiscard]] u64 NextDesiredTime(u64 nowNs, u64 intervalNs) const noexcept
    {
        const u64 max = std::numeric_limits<u64>::max();
        if (CommittedDesiredPresentTimeNs == 0)
            return nowNs > max - intervalNs ? max : nowNs + intervalNs;

        u64 next = CommittedDesiredPresentTimeNs > max - intervalNs
            ? max
            : CommittedDesiredPresentTimeNs + intervalNs;
        if (next > nowNs || next == max)
            return next;

        const u64 behind = nowNs - next;
        const u64 intervals = behind / intervalNs + 1;
        if (intervals > (max - next) / intervalNs)
            return max;
        return next + intervals * intervalNs;
    }

    u32 CommittedPresentId = 0;
    u64 CommittedDesiredPresentTimeNs = 0;
    VulkanGooglePresentRequest PendingRequest{};
    bool Pending = false;
    VulkanGooglePresentationFeedback Feedback{};
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_GOOGLE_DISPLAY_TIMING_MODEL_H
