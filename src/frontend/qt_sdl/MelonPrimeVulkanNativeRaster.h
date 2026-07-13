#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanNativeRaster requires the MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1

#include <QImage>
#include <QSize>
#include <QVulkanWindow>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "GPU3D_Vulkan.h"

class QVulkanDeviceFunctions;

namespace melonDS
{
class GPU;
class VulkanRenderer;
}

namespace MelonPrime::Vulkan
{

struct NativeRasterTexture
{
    std::uint64_t Key = 0;
    std::uint64_t ContentHash = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t SamplerIndex = 0;
    std::shared_ptr<const std::vector<std::uint32_t>> Rgb6a5;
};

struct NativeRasterFrame
{
    bool Valid = false;
    int Scale = 1;
    std::uint32_t EngineAScreen = 0;
    std::uint16_t MasterBrightnessA = 0;
    std::uint32_t RenderDispCnt = 0;
    std::uint32_t RenderClearAttr1 = 0;
    std::uint32_t RenderClearAttr2 = 0;
    std::uint32_t RenderAlphaRef = 0;
    std::uint16_t RenderXPos = 0;
    std::uint32_t RenderFogColor = 0;
    std::uint32_t RenderFogOffset = 0;
    std::uint32_t RenderFogShift = 0;
    std::array<std::uint8_t, 34> RenderFogDensityTable{};
    std::array<std::uint16_t, 8> RenderEdgeTable{};
    std::array<std::uint16_t, 32> RenderToonTable{};
    std::uint64_t FrameSerial = 0;
    std::uint64_t Generation = 0;

    melonDS::Vulkan::VulkanRasterUpload Upload;
    std::vector<NativeRasterTexture> Textures;
    std::vector<std::uint32_t> ClearBitmapColorRgba6a5;
    std::vector<std::uint32_t> ClearBitmapDepthFog;
    std::vector<std::uint32_t> NativeReferenceBgra;

    void Clear() noexcept;
};

class NativeRasterSnapshotBuilder
{
public:
    NativeRasterSnapshotBuilder();
    ~NativeRasterSnapshotBuilder();

    NativeRasterSnapshotBuilder(const NativeRasterSnapshotBuilder&) = delete;
    NativeRasterSnapshotBuilder& operator=(const NativeRasterSnapshotBuilder&) = delete;

    bool Build(
        const melonDS::VulkanRenderer& renderer,
        melonDS::GPU& gpu,
        NativeRasterFrame& frame);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

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
