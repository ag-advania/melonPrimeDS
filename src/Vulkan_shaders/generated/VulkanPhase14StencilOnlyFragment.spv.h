#pragma once

#include <cstddef>
#include <cstdint>

namespace melonDS::Vulkan::Shaders
{

inline constexpr char kVulkanPhase14StencilOnlyFragmentSourceSha256[] = "59a9039c36cde3cd2c1998b00d7c9d7f705be18820780a8872ce65ea1c653553";
inline constexpr char kVulkanPhase14StencilOnlyFragmentSpirvSha256[] = "e0ba54aafd81dd44cc181b935f2f8b1d61f439b0168b12d6ba64c7f1d07098d4";
alignas(4) inline constexpr std::uint32_t kVulkanPhase14StencilOnlyFragmentSpirv[] = {
    0x07230203u, 0x00010300u, 0x0008000bu, 0x00000006u, 0x00000000u, 0x00020011u, 0x00000001u, 0x0006000bu,
    0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u,
    0x0005000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00030010u, 0x00000004u, 0x00000007u,
    0x00030003u, 0x00000002u, 0x000001c2u, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x00020013u,
    0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u,
    0x00000003u, 0x000200f8u, 0x00000005u, 0x000100fdu, 0x00010038u,
};
inline constexpr std::size_t kVulkanPhase14StencilOnlyFragmentSpirvSize = sizeof(kVulkanPhase14StencilOnlyFragmentSpirv);

} // namespace melonDS::Vulkan::Shaders
