#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "VulkanSupport.h is owned by the complete MelonPrime Vulkan build gate"
#endif

#include <cstdint>

namespace melonDS::Vulkan
{

// Phase 1 intentionally exposes no runtime entry point. Later phases extend
// this shell only inside MELONPRIME_VULKAN_ACTIVE.
inline constexpr std::uint32_t kBuildGateVersion = 1;

} // namespace melonDS::Vulkan
