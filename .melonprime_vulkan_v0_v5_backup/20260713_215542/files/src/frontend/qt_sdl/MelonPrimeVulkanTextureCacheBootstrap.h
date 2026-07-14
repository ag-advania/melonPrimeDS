#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanTextureCacheBootstrap is owned by the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_TEXTURE_CACHE_BOOTSTRAP_V1

class QString;

namespace MelonPrime::Vulkan
{

// Creates individual Vulkan images, the complete 3x3 nearest sampler table and
// a descriptor-set cache from ordered DS texture requests. It validates dirty
// generation invalidation and non-adjacent cache reuse by GPU sampling/readback.
int RunTextureCacheBootstrapHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
