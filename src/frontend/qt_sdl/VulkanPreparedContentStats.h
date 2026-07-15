#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "types.h"

using melonDS::u32;
using melonDS::u64;

namespace melonDS
{
class GPU;
}

struct VulkanPreparedContentStats
{
    u32 topPlane0NonZero = 0;
    u32 topPlane1NonZero = 0;
    u32 topControlNonZero = 0;
    u32 topLineMetaNonZero = 0;

    u32 bottomPlane0NonZero = 0;
    u32 bottomPlane1NonZero = 0;
    u32 bottomControlNonZero = 0;
    u32 bottomLineMetaNonZero = 0;

    u32 topStructured3dSlots = 0;
    u32 bottomStructured3dSlots = 0;

    u32 topDisplayModeCounts[4]{};
    u32 bottomDisplayModeCounts[4]{};

    bool renderer3dSnapshotValid = false;
};

VulkanPreparedContentStats CollectVulkanPreparedContentStats(
    const melonDS::GPU& gpu,
    int frontBuffer,
    bool renderer3dSnapshotValid) noexcept;

void LogVulkanPreparedContentStats(
    u64 frameId,
    int frontBuffer,
    bool screenSwap,
    const VulkanPreparedContentStats& stats) noexcept;

#endif
