#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanTranslucentBootstrap is owned by the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_TRANSLUCENT_PIPELINE_BOOTSTRAP_V1

class QString;

namespace MelonPrime::Vulkan
{

// Draws deterministic untextured translucent DS polygons with Vulkan blend,
// depth and stencil state. The harness verifies same-polyID suppression,
// different-polyID overlap, depth-write ON/OFF, LESS/LEQUAL, W-buffer,
// translucent alpha rejection and fog-attribute write masks. ROM rendering
// remains on the Software correctness baseline.
int RunTranslucentPipelineBootstrapHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
