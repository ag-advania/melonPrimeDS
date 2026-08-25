/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "VulkanGpu2DComposer.h"

#include "Platform.h"

namespace melonDS
{

using namespace VulkanGpu2D;

bool VulkanGpu2DComposer::ComposeWorkSlot::EnsureDiagnosticResources(
    const VulkanDevice& device,
    VkDeviceSize outputBytes,
    bool needDiagnosticComposed,
    bool needStructuredReadback)
{
    if (needDiagnosticComposed && !DiagnosticComposed.IsValid()
        && !DiagnosticComposed.Create(device, outputBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            "MelonPrime Vulkan diagnostic composed output"))
        return false;
    if (!NativeReadback.IsValid()
        && !NativeReadback.Create(device, outputBytes,
            "MelonPrime Vulkan native GPU2D diagnostic readback"))
        return false;
    if (needStructuredReadback && !StructuredReadback.IsValid()
        && !StructuredReadback.Create(device, StructuredInputBytes,
            "MelonPrime Vulkan GPU2D Stage A diagnostic readback"))
        return false;
    return true;
}

bool VulkanGpu2DComposer::Create(
    const VulkanDevice& device, u32 width, u32 height,
    u64 resourceGeneration, u64 epoch)
{
    Device = device;
    ResourceGeneration = resourceGeneration;
    const VkDeviceSize screenBytes =
        static_cast<VkDeviceSize>(width) * height * sizeof(u32);

    VkFormatProperties directProperties{};
    Device.InstanceFns().GetPhysicalDeviceFormatProperties(
        Device.GetPhysicalDevice(), DirectCompositorFormat, &directProperties);
    DirectImageEnabled =
        (directProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0
        && (directProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    if (!DirectImageEnabled)
    {
        Platform::Log(
            Platform::LogLevel::Warn,
            "[Vulkan] compositor direct image disabled: RGBA8 lacks storage or sampled support\n");
    }

    for (u32 i = 0; i < Slots.size(); ++i)
    {
        Slot& slot = Slots[i];
        if (!slot.StructuredStaging.Create(Device,
                StructuredInputBytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                "MelonPrime Vulkan structured staging slot"))
            return false;
        if (!slot.StructuredStaging.Map())
            return false;
        if (!slot.StructuredInput.Create(Device,
                StructuredInputBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                    | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                    | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                "MelonPrime Vulkan structured input slot"))
            return false;
        if (!slot.Composed.Create(Device,
                screenBytes * 2u,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                "MelonPrime Vulkan composed output slot"))
            return false;
    }

    if (DirectImageEnabled)
    {
        for (Slot& slot : Slots)
        {
            Vk::Image::CreateInfo directInfo{};
            directInfo.Format = DirectCompositorFormat;
            directInfo.Width = width;
            directInfo.Height = height;
            directInfo.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            directInfo.ViewType = VK_IMAGE_VIEW_TYPE_2D;
            directInfo.DebugName = "MelonPrime Vulkan direct compositor output";
            if (!slot.DirectImageTop.Create(Device, directInfo)
                || !slot.DirectImageBottom.Create(Device, directInfo))
            {
                DirectImageEnabled = false;
                break;
            }
        }
    }
    if (!DirectImageEnabled)
    {
        for (Slot& slot : Slots)
        {
            slot.DirectImageTop.Destroy();
            slot.DirectImageBottom.Destroy();
        }
    }

    for (Slot& slot : Slots)
    {
        slot.Frame.Buffer = slot.Composed.GetHandle();
        slot.Frame.DirectImageTop = DirectImageEnabled
            ? slot.DirectImageTop.GetHandle() : VK_NULL_HANDLE;
        slot.Frame.DirectImageViewTop = DirectImageEnabled
            ? slot.DirectImageTop.GetView() : VK_NULL_HANDLE;
        slot.Frame.DirectImageBottom = DirectImageEnabled
            ? slot.DirectImageBottom.GetHandle() : VK_NULL_HANDLE;
        slot.Frame.DirectImageViewBottom = DirectImageEnabled
            ? slot.DirectImageBottom.GetView() : VK_NULL_HANDLE;
        slot.Frame.TopOffset = 0;
        slot.Frame.BottomOffset = screenBytes;
        slot.Frame.Width = width;
        slot.Frame.Height = height;
        slot.Frame.Epoch = epoch;
        slot.Frame.ResourceGeneration = ResourceGeneration;
        slot.Frame.DirectContentValid = false;
    }

    for (u32 i = 0; i < WorkSlots.size(); ++i)
    {
        ComposeWorkSlot& slot = WorkSlots[i];
        if (!slot.NativeStaging.Create(Device,
                NativeGPU2DInputBytes,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                "MelonPrime Vulkan native GPU2D work staging slot"))
            return false;
        if (!slot.NativeStaging.Map())
            return false;
        if (!slot.NativeInput.Create(Device,
                NativeGPU2DInputBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                "MelonPrime Vulkan native GPU2D work input slot"))
            return false;
        if (!slot.StructuredInput.Create(Device,
                StructuredInputBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                "MelonPrime Vulkan native GPU2D work structured slot"))
            return false;
    }
    return true;
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
