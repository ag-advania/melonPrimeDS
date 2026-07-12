#include "VulkanSupport.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include "Vulkan_shaders/generated/VulkanShaders.h"

// MELONPRIME_VULKAN_BUILD_SHELL_V1
static_assert(VK_HEADER_VERSION > 0, "Vulkan headers must expose a version");
static_assert(melonDS::Vulkan::Shaders::kShaderCount >= 1,
              "Phase 1 shader toolchain probe must remain available");
