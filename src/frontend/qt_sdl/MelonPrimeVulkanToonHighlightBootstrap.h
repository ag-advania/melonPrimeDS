#pragma once
#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanToonHighlightBootstrap is owned by the Vulkan build gate"
#endif
// MELONPRIME_VULKAN_TOON_HIGHLIGHT_BOOTSTRAP_V1
// MELONPRIME_VULKAN_TOON_HIGHLIGHT_SHADER_ABI_BOOTSTRAP_V1
class QString;
namespace MelonPrime::Vulkan { int RunToonHighlightShaderAbiHarness(const QString& outputPath); }
