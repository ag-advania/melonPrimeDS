#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanClearPlaneBootstrap is owned by the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_CLEAR_PLANE_BOOTSTRAP_V1

class QString;

namespace MelonPrime::Vulkan
{

// Creates native Vulkan color, attribute and depth-stencil targets, applies the
// DS plain clear-plane state through render-pass load-op clears, and reads the
// two color targets back for deterministic validation. ROM rendering remains
// on the Software correctness baseline until later Phase 7 subphases.
int RunClearPlaneBootstrapHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
