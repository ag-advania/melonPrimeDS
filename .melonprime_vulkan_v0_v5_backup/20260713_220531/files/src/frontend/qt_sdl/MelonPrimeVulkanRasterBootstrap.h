#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanRasterBootstrap is owned by the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_RASTER_BOOTSTRAP_V1

class QString;

namespace MelonPrime::Vulkan
{

// Executes a real offscreen Vulkan graphics draw using the Phase 4 presenter
// shaders, then copies the 256x192 color attachment back to host memory and
// validates three pixels. This is a native-raster bring-up gate only: game
// rendering remains on the Phase 6 Software correctness baseline.
int RunRasterBootstrapHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
