#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanTexturedPolygonBootstrap is owned by the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_TEXTURED_POLYGON_BOOTSTRAP_V1

class QString;

namespace MelonPrime::Vulkan
{

// Draws deterministic textured opaque/translucent polygons through real Vulkan
// graphics pipelines and validates modulate/decal/toon/highlight plus sampler
// address modes by color attachment readback. ROM rendering remains Software.
int RunTexturedPolygonBootstrapHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
