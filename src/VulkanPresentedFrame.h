/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef VULKAN_PRESENTED_FRAME_H
#define VULKAN_PRESENTED_FRAME_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "VulkanCommon.h"

namespace melonDS
{

// Opaque renderer -> presenter handoff. The fallback backing buffer contains
// two tightly packed BGRA8 screens; when the device supports the direct path,
// the two sampleable RGBA8 images are the primary output. All resources belong
// to a leased compositor ring slot. Both producers and consumers submit to the
// shared VulkanDevice main queue, while RendererOutputLease provides the
// CPU-side lifetime.
struct VulkanPresentedFrame
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkImage DirectImageTop = VK_NULL_HANDLE;
    VkImageView DirectImageViewTop = VK_NULL_HANDLE;
    VkImage DirectImageBottom = VK_NULL_HANDLE;
    VkImageView DirectImageViewBottom = VK_NULL_HANDLE;
    VkDeviceSize TopOffset = 0;
    VkDeviceSize BottomOffset = 0;
    u32 Width = 0;
    u32 Height = 0;
    // Serial is the published frame sequence. Generation is the structured /
    // composition content generation and may change every DS frame. Keep the
    // resource lifetime generation separate: it changes only when the
    // renderer recreates the compositor output resources.
    u64 Serial = 0;
    u64 Generation = 0;
    u64 Epoch = 0;
    u64 ResourceGeneration = 0;
    // Handles describe allocated storage; this bit describes whether the
    // current published frame actually wrote that storage.  Diagnostic
    // readback may deliberately use the composed-buffer path while the
    // direct images remain allocated.
    bool DirectContentValid = false;

    [[nodiscard]] bool HasDirectSampledOutput() const noexcept
    {
        return DirectContentValid
            && DirectImageTop != VK_NULL_HANDLE
            && DirectImageViewTop != VK_NULL_HANDLE
            && DirectImageBottom != VK_NULL_HANDLE
            && DirectImageViewBottom != VK_NULL_HANDLE;
    }
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_PRESENTED_FRAME_H
