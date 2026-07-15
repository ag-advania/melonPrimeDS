#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "MelonPrimeBuildInfo.h"

namespace MelonPrime::DesktopVulkan {

// Desktop-only Vulkan presenter wrapper boundary. Keeps Qt WSI, fullscreen
// resize policy, and HUD upload ownership out of the vendor presenter core.
void LogBuildIdentity();

} // namespace MelonPrime::DesktopVulkan

#endif
