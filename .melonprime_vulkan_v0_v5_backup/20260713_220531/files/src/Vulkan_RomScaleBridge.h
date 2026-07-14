#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "Vulkan_RomScaleBridge.h is owned by the MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_ROM_SCALE_BRIDGE_V1

#include "types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace melonDS
{
class GPU3D;

namespace Vulkan
{

struct VulkanRomScaleSettings
{
    int ScaleFactor = 1;
    bool BetterPolygons = false;
    bool HiresCoordinates = false;

    // MELONPRIME_VULKAN_COVERAGE_ONLY_V2
    // The realtime compatibility path only needs high-resolution polygon
    // coverage. Skipping the high-resolution color reconstruction removes one
    // scale-squared allocation and one full-frame CPU pass.
    bool CoverageOnly = false;
};

struct VulkanRomScaleResult
{
    int Width = 0;
    int Height = 0;
    std::vector<u32> HighResolution3D;
    std::vector<std::uint8_t> Coverage;
    std::uint64_t InputPolygonCount = 0;
    std::uint64_t RasterizedTriangleCount = 0;
    std::uint64_t CoveredPixelCount = 0;
    bool BetterPolygonPathUsed = false;
    bool HiresCoordinatePathUsed = false;
    bool Valid = false;
    std::string FailureReason;

    void Clear() noexcept;
};

bool BuildVulkanRomScaleBridge(
    const GPU3D& gpu3D,
    const u32* native3D,
    const VulkanRomScaleSettings& settings,
    VulkanRomScaleResult& result) noexcept;

} // namespace Vulkan
} // namespace melonDS
