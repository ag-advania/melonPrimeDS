#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "GPU_Vulkan.h requires the MelonPrime Vulkan build gate"
#endif

#include <memory>
#include "GPU3D_Vulkan.h"

namespace melonDS
{
// Constructs the Vulkan Renderer3D (graphics-hardware backend) and initializes it.
// Returns nullptr on failure; the caller is responsible for falling back and
// reporting the real failure stage/reason, not a hardcoded success contract.
std::unique_ptr<Renderer3D> CreateSapphireVulkanRenderer3D(
    GPU3D& gpu3D,
    bool computeSelected) noexcept;
} // namespace melonDS
