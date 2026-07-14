#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanPolygonBatchBootstrap is owned by the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_POLYGON_BATCH_BOOTSTRAP_V1

class QString;

namespace MelonPrime::Vulkan
{

// Builds deterministic adjacent-only Vulkan polygon batches from the Phase 7.4
// packed upload, copies the fixed batch records through a device-local buffer,
// and validates exact GPU readback. ROM rendering remains on Software.
int RunPolygonBatchBootstrapHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
