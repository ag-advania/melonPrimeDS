#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanOpaqueBootstrap is owned by the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_OPAQUE_PIPELINE_BOOTSTRAP_V1

class QString;

namespace MelonPrime::Vulkan
{

// Builds deterministic packed DS vertices and adjacent-only batches, uploads
// them to device-local Vulkan vertex/index buffers, draws untextured opaque
// Z/W-buffer triangles with LESS/LEQUAL, alpha test and stencil polyID replace,
// then validates color/attribute/stencil readback. ROM rendering remains on the
// Software correctness baseline until the native Renderer3D integration step.
int RunOpaquePipelineBootstrapHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
