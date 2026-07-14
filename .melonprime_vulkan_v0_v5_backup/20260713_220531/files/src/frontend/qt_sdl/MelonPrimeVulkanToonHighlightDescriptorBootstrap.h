#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanToonHighlightDescriptorBootstrap is owned by the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_TOON_HIGHLIGHT_DESCRIPTOR_RUNTIME_BOOTSTRAP_V1
// MELONPRIME_VULKAN_TOON_HIGHLIGHT_GPU_DRAW_BOOTSTRAP_V1

class QString;

namespace MelonPrime::Vulkan
{

// Allocates the real uniform-buffer descriptor runtime, binds it to opaque and
// translucent graphics pipelines, draws deterministic toon/highlight samples,
// and validates color attachment readback. Game rendering remains Software.
int RunToonHighlightDescriptorRuntimeHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
