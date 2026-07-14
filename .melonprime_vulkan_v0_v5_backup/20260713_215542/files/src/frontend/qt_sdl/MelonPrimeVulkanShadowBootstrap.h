#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanShadowBootstrap is owned by the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_SHADOW_PIPELINE_BOOTSTRAP_V1

class QString;

namespace MelonPrime::Vulkan
{

// Exercises the two-stage DS shadow path: depth-fail mask writes stencil bit 7
// without changing lower polygon-ID bits, then visible-shadow self-rejection and
// blended stencil update. Game rendering remains on the Software baseline.
int RunShadowPipelineBootstrapHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
