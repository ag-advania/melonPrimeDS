#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "Vulkan_Phase12UiContract.h is owned by the MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_PHASE12_UI_CONTRACT_V1

#include <cstdint>
#include <optional>
#include <string>

namespace melonDS::Vulkan
{

inline constexpr std::uint32_t kPhase12UiContractVersion = 1;
inline constexpr int kPhase12RendererSoftware = 0;
inline constexpr int kPhase12RendererVulkanRaster = 5;
inline constexpr int kPhase12RendererVulkanCompute = 6;

enum class VulkanUiPlatform : std::uint32_t
{
    Windows,
    Linux,
    MacOS,
    Other,
};

enum class VulkanUiReason : std::uint32_t
{
    Available,
    BuildDisabled,
    MoltenVkBuildRequired,
    InstanceUnavailable,
    PresentationUnavailable,
    RasterFeaturesUnavailable,
    ComputeFeaturesUnavailable,
    RasterRomIntegrationPending,
    ComputeRomIntegrationPending,
};

struct VulkanUiFeatureSnapshot
{
    VulkanUiPlatform Platform = VulkanUiPlatform::Other;
    bool BuildEnabled = false;
    bool MoltenVkBuildEnabled = false;
    bool InstanceAvailable = false;
    bool PresentationAvailable = false;
    bool RasterFeaturesAvailable = false;
    bool ComputeFeaturesAvailable = false;
    bool RasterRomIntegrationReady = false;
    bool ComputeRomIntegrationReady = false;
    std::string DriverReason;
};

struct VulkanBackendChoiceState
{
    bool Generated = false;
    bool Enabled = false;
    VulkanUiReason Reason = VulkanUiReason::BuildDisabled;
};

struct VulkanPhase12UiState
{
    VulkanBackendChoiceState Raster;
    VulkanBackendChoiceState Compute;
};

struct VulkanHardwareSettings
{
    int Renderer = kPhase12RendererSoftware;
    int ScaleFactor = 1;
    bool BetterPolygons = false;
    bool HiresCoordinates = false;
    bool VSync = false;
};

struct VulkanPersistedHardwareSettings
{
    std::optional<int> NewScaleFactor;
    std::optional<bool> NewBetterPolygons;
    std::optional<bool> NewHiresCoordinates;
    int LegacyScaleFactor = 1;
    bool LegacyBetterPolygons = false;
    bool LegacyHiresCoordinates = false;
};

struct VulkanDualWriteHardwareSettings
{
    int NewScaleFactor = 1;
    bool NewBetterPolygons = false;
    bool NewHiresCoordinates = false;
    int LegacyScaleFactor = 1;
    bool LegacyBetterPolygons = false;
    bool LegacyHiresCoordinates = false;
};

struct VulkanRendererOptionMatrix
{
    bool InternalResolution = false;
    bool BetterPolygons = false;
    bool HiresCoordinates = false;
    bool SoftwareThreaded = false;
    bool OpenGlDisplay = false;
    bool VSync = false;
};

[[nodiscard]] VulkanPhase12UiState EvaluateVulkanPhase12UiState(
    const VulkanUiFeatureSnapshot& snapshot) noexcept;
[[nodiscard]] const char* VulkanUiReasonName(VulkanUiReason reason) noexcept;
[[nodiscard]] int NormalizePhase12RendererId(int renderer) noexcept;
[[nodiscard]] VulkanHardwareSettings BuildVulkanPhase12Preset(bool compute) noexcept;
[[nodiscard]] VulkanHardwareSettings ResolveVulkanHardwareSettings(
    const VulkanPersistedHardwareSettings& persisted,
    int renderer,
    bool vsync) noexcept;
[[nodiscard]] VulkanDualWriteHardwareSettings BuildVulkanDualWriteSettings(
    const VulkanHardwareSettings& settings) noexcept;
[[nodiscard]] VulkanRendererOptionMatrix BuildVulkanRendererOptionMatrix(
    int renderer,
    bool presenterSupportsVsync) noexcept;
[[nodiscard]] bool ValidateVulkanPhase12ExitContract() noexcept;

} // namespace melonDS::Vulkan
