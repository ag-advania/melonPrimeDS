#pragma once
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
    // R25 persistent pipeline-cache identity. Bump an owner ABI whenever its
    // descriptor layout, push constants, shader set, or fixed pipeline state changes.
    inline constexpr const char* kSapphireShaderManifestIdentity =
        "git-blob:a9b505e8d017482e7e6f5f67e30b118c6d465a43";
    inline constexpr unsigned kPipelineCacheDiskFormatVersion = 1;
    inline constexpr unsigned kRenderer3DPipelineAbiVersion = 1;
    inline constexpr unsigned kCompositorPipelineAbiVersion = 1;
    inline constexpr unsigned kPresenterPipelineAbiVersion = 1;
} // namespace MelonPrime::Vulkan

// Historical marker comments below are retained only as git-blame/code-search
// breadcrumbs for the incremental patches that built up the current Vulkan
// port. They do not gate any runtime behavior and must not be treated as
// evidence that a feature is complete (plan phase R0).
// MELONPRIME_SAPPHIRE_VULKAN_FACTORY_NAMESPACE_FIX_V1
// MELONPRIME_SAPPHIRE_VULKAN_COMPOSITOR_ODR_FIX_V1
// MELONPRIME_SAPPHIRE_COMPOSITOR_SHADER_INLINE_DATA_V1
