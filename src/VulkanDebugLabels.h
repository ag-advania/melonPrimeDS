/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef MELONPRIME_VULKAN_DEBUG_LABELS_H
#define MELONPRIME_VULKAN_DEBUG_LABELS_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "VulkanLoader.h"

namespace melonDS::Vk
{

// Command labels are deliberately developer-only. The dispatch pointers are
// also optional, so a developer build on a loader without VK_EXT_debug_utils
// keeps the same command stream and simply omits the annotations.
inline void BeginCommandDebugLabel(
    const DeviceDispatch& fns, VkCommandBuffer commandBuffer, const char* name) noexcept
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (!fns.CmdBeginDebugUtilsLabelEXT || commandBuffer == VK_NULL_HANDLE || !name)
        return;

    VkDebugUtilsLabelEXT label{};
    label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    label.pLabelName = name;
    fns.CmdBeginDebugUtilsLabelEXT(commandBuffer, &label);
#else
    (void)fns;
    (void)commandBuffer;
    (void)name;
#endif
}

inline void EndCommandDebugLabel(
    const DeviceDispatch& fns, VkCommandBuffer commandBuffer) noexcept
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (!fns.CmdEndDebugUtilsLabelEXT || commandBuffer == VK_NULL_HANDLE)
        return;
    fns.CmdEndDebugUtilsLabelEXT(commandBuffer);
#else
    (void)fns;
    (void)commandBuffer;
#endif
}

} // namespace melonDS::Vk

#endif // defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#endif // MELONPRIME_VULKAN_DEBUG_LABELS_H
