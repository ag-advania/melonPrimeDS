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

// Opaque renderer -> presenter handoff. The backing buffer contains two
// tightly packed BGRA8 screens and belongs to a leased compositor ring slot.
// Both producers and consumers submit to the shared VulkanDevice main queue;
// the presenter's compute-write -> transfer-read barrier provides the GPU-side
// dependency, while RendererOutputLease provides the CPU-side lifetime.
struct VulkanPresentedFrame
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceSize TopOffset = 0;
    VkDeviceSize BottomOffset = 0;
    u32 Width = 0;
    u32 Height = 0;
    u64 Serial = 0;
    u64 Generation = 0;
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_PRESENTED_FRAME_H
