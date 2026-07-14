#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanTextureSamplingBootstrap is owned by the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_TEXTURE_SAMPLING_BOOTSTRAP_V1

class QString;

namespace MelonPrime::Vulkan
{

// Uploads a deterministic DS RGB6A5 texture to a Vulkan integer image, validates
// clamp/repeat/mirror samplers, and evaluates raw/modulate/decal/toon/highlight
// texture combiners in a compute pipeline. Game rendering remains on Software.
int RunTextureSamplingBootstrapHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
