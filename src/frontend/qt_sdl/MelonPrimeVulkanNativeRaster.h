#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanNativeRaster requires the MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1

#include <QImage>
#include <QSize>
#include <QVulkanWindow>

#include <memory>

#include "GPU_Vulkan.h"

class QVulkanDeviceFunctions;

namespace MelonPrime::Vulkan
{

struct NativeRasterViews
{
    VkImageView HighResolution = VK_NULL_HANDLE;
    VkImageView NativeReference = VK_NULL_HANDLE;
    VkSampler Sampler = VK_NULL_HANDLE;
    bool Valid = false;
};

class NativeRasterGpu
{
public:
    NativeRasterGpu();
    ~NativeRasterGpu();

    NativeRasterGpu(const NativeRasterGpu&) = delete;
    NativeRasterGpu& operator=(const NativeRasterGpu&) = delete;

    bool Render(
        QVulkanWindow* window,
        QVulkanDeviceFunctions* functions,
        VkCommandBuffer command,
        int frameSlot,
        const NativeRasterFrame& frame,
        NativeRasterViews& views);

    void Release();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace MelonPrime::Vulkan
