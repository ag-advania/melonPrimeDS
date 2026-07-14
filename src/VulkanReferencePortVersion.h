#pragma once
// MELONPRIME_VULKAN_REFERENCE_PORT_V0_V5_V1
//
// Pinned upstream reference points for the Vulkan backend reconstruction
// plan (.claude/rules/plan/melonPrimeDS_Vulkan_GPU3D_...md, "重大計画修正版").
// These are the sole source of truth for which upstream snapshot the Vulkan
// port targets; do not bump them to a newer release/commit without redoing
// the plan's R1 file-by-file diff pass first. Runtime code must not use
// these constants (or the historical marker comments below) to decide
// whether any Vulkan feature is actually working -- see
// MelonPrime::VideoBackend::VulkanRuntimeCapabilities in
// MelonPrimeRendererFactory.h for that.
namespace MelonPrime::Vulkan
{
    inline constexpr const char* kSapphireFrontendRepo = "SapphireRhodonite/melonDS-android";
    inline constexpr const char* kSapphireFrontendTag = "0.7.0.rc4";

    inline constexpr const char* kSapphireCoreRepo = "SapphireRhodonite/melonDS-android-lib";
    inline constexpr const char* kSapphireCoreCommit =
        "d77944275fa61f9b79cfcead2c3e98993429a023";
} // namespace MelonPrime::Vulkan

// Historical marker comments below are retained only as git-blame/code-search
// breadcrumbs for the incremental patches that built up the current Vulkan
// port. They do not gate any runtime behavior and must not be treated as
// evidence that a feature is complete (plan phase R0).
// MELONPRIME_SAPPHIRE_VULKAN_RENDERER3D_OWNERSHIP_A1
// MELONPRIME_SAPPHIRE_VULKAN_STRUCTURED_2D_A2
// MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_INPUT_A3
// MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_RESOURCES_A4
// MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_COMMAND_A5
// MELONPRIME_SAPPHIRE_VULKAN_COMPOSITOR_SHADER_MODULE_A6
// MELONPRIME_SAPPHIRE_VULKAN_FACTORY_NAMESPACE_FIX_V1
// MELONPRIME_SAPPHIRE_VULKAN_COMPOSITOR_EXACT_ABI_A7
// MELONPRIME_SAPPHIRE_VULKAN_COMPOSITOR_ODR_FIX_V1
// MELONPRIME_SAPPHIRE_COMPOSITOR_SHADER_INLINE_DATA_V1
