#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanClearBitmapBootstrap is owned by the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_CLEAR_BITMAP_BOOTSTRAP_V1

class QString;

namespace MelonPrime::Vulkan
{

// Uploads deterministic DS clear-bitmap color/depth slots to integer Vulkan
// textures, samples them with nearest-repeat addressing plus the DS X/Y offset,
// writes color/attribute/depth/stencil targets, and validates GPU readback.
// Game rendering remains on the Software correctness baseline.
int RunClearBitmapBootstrapHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
