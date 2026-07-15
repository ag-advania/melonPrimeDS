#ifndef VULKANFILTERMODE_H
#define VULKANFILTERMODE_H

#include "types.h"

namespace MelonDSAndroid
{

enum class VulkanFilterMode : melonDS::u32
{
    Nearest = 0,
    Linear = 1,
    Xbr2 = 2,
    Hq2x = 3,
    Hq4x = 4,
    Quilez = 5,
    Lcd = 6,
    Scanlines = 7,
    RetroArch = 8,
};

inline bool IsVulkanPostProcessFilter(VulkanFilterMode filter)
{
    return filter == VulkanFilterMode::Xbr2
        || filter == VulkanFilterMode::Hq2x
        || filter == VulkanFilterMode::Hq4x
        || filter == VulkanFilterMode::Quilez
        || filter == VulkanFilterMode::Lcd
        || filter == VulkanFilterMode::Scanlines
        || filter == VulkanFilterMode::RetroArch;
}

}

#endif
