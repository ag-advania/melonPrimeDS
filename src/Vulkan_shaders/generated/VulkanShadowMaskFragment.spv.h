#pragma once

#include <cstddef>
#include <cstdint>

namespace melonDS::Vulkan::Shaders
{

inline constexpr char kVulkanShadowMaskFragmentSourceSha256[] = "79e2a1de01d21c7db376599841d6bb25497141a0bd3d850a139174e7f81fb2e6";
inline constexpr char kVulkanShadowMaskFragmentSpirvSha256[] = "e0ba54aafd81dd44cc181b935f2f8b1d61f439b0168b12d6ba64c7f1d07098d4";
alignas(4) inline constexpr std::uint32_t kVulkanShadowMaskFragmentSpirv[] = {
    0x07230203u, 0x00010300u, 0x0008000bu, 0x00000006u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0005000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00030010u, 0x00000004u, 0x00000007u,
    0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00020013u,
    0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u,
    0x00000003u, 0x000200f8u, 0x00000005u, 0x000100fdu, 0x00010038u,
};
inline constexpr std::size_t kVulkanShadowMaskFragmentSpirvSize = sizeof(kVulkanShadowMaskFragmentSpirv);

} // namespace melonDS::Vulkan::Shaders
