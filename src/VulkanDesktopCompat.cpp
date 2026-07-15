#include "VulkanDesktopCompat.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
#include <atomic>

namespace
{
std::atomic_bool gVulkanFastForwardActive{false};
std::atomic_bool gVulkanUnlimitedActive{false};
} // namespace
#endif

namespace MelonDSAndroid {
bool isFastForwardActive()
{
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    return gVulkanFastForwardActive.load(std::memory_order_acquire)
        || gVulkanUnlimitedActive.load(std::memory_order_acquire);
#else
    return false;
#endif
}

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
void PublishVulkanDesktopPacingState(bool fastForward, bool unlimited)
{
    gVulkanFastForwardActive.store(fastForward, std::memory_order_release);
    gVulkanUnlimitedActive.store(unlimited, std::memory_order_release);
}
#endif

bool areRendererDebugToolsEnabled(){return false;} bool areRendererDebugBgObjLogsEnabled(){return false;}
bool areRendererDebugLatchTraceLogsEnabled(){return false;}
bool isRenderer3DDebugFeatureEnabled(melonDS::u32){return true;} bool areRenderer3DDebugControlsActive(){return false;} melonDS::u32 getVulkanDiagnosticFlags(){return 0;}
int getRenderer2DDebugForcedMode(melonDS::u32){return -1;} int getRenderer2DDebugForcedCompMode(bool){return -1;} bool isRenderer2DDebugBgLayerEnabled(melonDS::u32,melonDS::u32){return true;}
bool areRenderer2DDebugControlsActive(){return false;}
bool isRenderer2DDebugBgPriorityEnabled(melonDS::u32,melonDS::u32){return true;} bool isRenderer2DDebugBackgroundKindEnabled(melonDS::u32){return true;}
bool areRenderer2DDebugObjectsEnabled(melonDS::u32){return true;} bool isRenderer2DDebugObjectPriorityEnabled(melonDS::u32,melonDS::u32){return true;}
bool isRenderer2DDebugObjectOrderEnabled(melonDS::u32,melonDS::u32){return true;} bool isRenderer2DDebugObjectFeatureEnabled(melonDS::u32){return true;}
}
