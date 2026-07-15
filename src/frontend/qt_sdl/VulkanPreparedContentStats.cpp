#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "VulkanPreparedContentStats.h"

#include "GPU.h"
#include "Platform.h"
#include "VulkanStructuredControlAbi.inc"

using melonDS::u32;
using melonDS::u64;

namespace
{
constexpr u32 kPackedStride = MP_VK_PACKED_SCREEN_STRIDE;
constexpr u32 kScreenHeight = MP_VK_NATIVE_SCREEN_HEIGHT;
constexpr u32 kScreenWidth = MP_VK_NATIVE_SCREEN_WIDTH;
constexpr u32 k3dSlotFlag = MP_VK_PACKED_CONTROL_3D_SLOT_FLAG;

struct ScreenScanStats
{
    u32 plane0NonZero = 0;
    u32 plane1NonZero = 0;
    u32 controlNonZero = 0;
    u32 lineMetaNonZero = 0;
    u32 structured3dSlots = 0;
    u32 displayModeCounts[4]{};
};

void ScanPackedScreen(const u32* buffer, ScreenScanStats& out) noexcept
{
    if (buffer == nullptr)
        return;

    for (u32 line = 0; line < kScreenHeight; ++line)
    {
        const u32* row = buffer + static_cast<size_t>(line) * kPackedStride;
        const u32 meta = row[kPackedStride - 1u];
        if (meta != 0u)
            ++out.lineMetaNonZero;

        const u32 displayMode = (meta >> 16u) & 0x3u;
        if (displayMode < 4u)
            ++out.displayModeCounts[displayMode];

        for (u32 x = 0; x < kScreenWidth; ++x)
        {
            if (row[x] != 0u)
                ++out.plane0NonZero;
            if (row[kScreenWidth + x] != 0u)
                ++out.plane1NonZero;

            const u32 control = row[(kScreenWidth * 2u) + x];
            if (control != 0u)
                ++out.controlNonZero;
            if ((control >> 24u) & k3dSlotFlag)
                ++out.structured3dSlots;
        }
    }
}
} // namespace

VulkanPreparedContentStats CollectVulkanPreparedContentStats(
    const melonDS::GPU& gpu,
    int frontBuffer,
    bool renderer3dSnapshotValid) noexcept
{
    VulkanPreparedContentStats stats{};
    stats.renderer3dSnapshotValid = renderer3dSnapshotValid;

    if (frontBuffer < 0 || frontBuffer > 1)
        return stats;

    ScreenScanStats top{};
    ScreenScanStats bottom{};
    ScanPackedScreen(gpu.Framebuffer[frontBuffer][0], top);
    ScanPackedScreen(gpu.Framebuffer[frontBuffer][1], bottom);

    stats.topPlane0NonZero = top.plane0NonZero;
    stats.topPlane1NonZero = top.plane1NonZero;
    stats.topControlNonZero = top.controlNonZero;
    stats.topLineMetaNonZero = top.lineMetaNonZero;
    stats.topStructured3dSlots = top.structured3dSlots;
    for (u32 mode = 0; mode < 4u; ++mode)
        stats.topDisplayModeCounts[mode] = top.displayModeCounts[mode];

    stats.bottomPlane0NonZero = bottom.plane0NonZero;
    stats.bottomPlane1NonZero = bottom.plane1NonZero;
    stats.bottomControlNonZero = bottom.controlNonZero;
    stats.bottomLineMetaNonZero = bottom.lineMetaNonZero;
    stats.bottomStructured3dSlots = bottom.structured3dSlots;
    for (u32 mode = 0; mode < 4u; ++mode)
        stats.bottomDisplayModeCounts[mode] = bottom.displayModeCounts[mode];

    return stats;
}

void LogVulkanPreparedContentStats(
    u64 frameId,
    int frontBuffer,
    bool screenSwap,
    const VulkanPreparedContentStats& stats) noexcept
{
    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Info,
        "[VulkanFrameContent] frameId=%llu frontBuffer=%d screenSwap=%d "
        "topP0=%u topP1=%u topCtl=%u topMeta=%u "
        "bottomP0=%u bottomP1=%u bottomCtl=%u bottomMeta=%u "
        "top3dSlots=%u bottom3dSlots=%u rendererSnapshot=%d\n",
        static_cast<unsigned long long>(frameId),
        frontBuffer,
        screenSwap ? 1 : 0,
        stats.topPlane0NonZero,
        stats.topPlane1NonZero,
        stats.topControlNonZero,
        stats.topLineMetaNonZero,
        stats.bottomPlane0NonZero,
        stats.bottomPlane1NonZero,
        stats.bottomControlNonZero,
        stats.bottomLineMetaNonZero,
        stats.topStructured3dSlots,
        stats.bottomStructured3dSlots,
        stats.renderer3dSnapshotValid ? 1 : 0);
}

#endif
