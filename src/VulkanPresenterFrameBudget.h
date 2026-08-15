/*
    Copyright 2016-2026 melonDS team

    Pure frame-index helpers for the presenter latency-budget contract.
*/

#ifndef MELONPRIME_VULKAN_PRESENTER_FRAME_BUDGET_H
#define MELONPRIME_VULKAN_PRESENTER_FRAME_BUDGET_H

#include "types.h"

namespace melonDS
{

// FrameRing's AbsoluteFrame is the one-based number of the frame that will be
// recorded next. Keeping this mapping pure makes the distinction between the
// latest submitted slot and the next reusable slot executable in unit tests.
constexpr u32 VulkanFrameRingIndexForAbsoluteFrame(
    u64 absoluteFrame, u32 frameCount) noexcept
{
    return frameCount == 0 ? 0u
        : static_cast<u32>((absoluteFrame - 1u) % frameCount);
}

} // namespace melonDS

#endif // MELONPRIME_VULKAN_PRESENTER_FRAME_BUDGET_H
