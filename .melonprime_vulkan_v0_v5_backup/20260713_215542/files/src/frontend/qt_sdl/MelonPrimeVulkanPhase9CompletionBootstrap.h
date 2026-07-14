#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "Phase 9 Vulkan completion harness requires the Vulkan build gate"
#endif

class QString;

namespace MelonPrime::Vulkan
{

// MELONPRIME_VULKAN_PHASE9_COMPLETION_BOOTSTRAP_V1
int RunPhase9CompletionHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
