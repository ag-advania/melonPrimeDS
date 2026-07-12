#pragma once
#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanToonHighlightDescriptorBootstrap is owned by the Vulkan build gate"
#endif
// MELONPRIME_VULKAN_TOON_HIGHLIGHT_DESCRIPTOR_RUNTIME_BOOTSTRAP_V1
class QString;
namespace MelonPrime::Vulkan {
int RunToonHighlightDescriptorRuntimeHarness(const QString& outputPath, int iterations = 3);
}
