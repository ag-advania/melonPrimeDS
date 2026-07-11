#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanFeatureCheck.h is owned by the Vulkan build gate"
#endif

namespace MelonPrime::Vulkan
{

inline constexpr bool kFeatureCheckShellCompiled = true;

} // namespace MelonPrime::Vulkan
