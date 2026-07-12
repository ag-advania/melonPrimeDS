#include "Vulkan_Phase12UiContract.h"

#include <algorithm>

namespace melonDS::Vulkan
{

namespace
{

VulkanBackendChoiceState BuildChoice(
    const VulkanUiFeatureSnapshot& snapshot,
    bool compute) noexcept
{
    VulkanBackendChoiceState choice;
    choice.Generated = snapshot.BuildEnabled &&
        (snapshot.Platform != VulkanUiPlatform::MacOS || snapshot.MoltenVkBuildEnabled);
    if (!choice.Generated)
    {
        choice.Reason = snapshot.Platform == VulkanUiPlatform::MacOS && snapshot.BuildEnabled
            ? VulkanUiReason::MoltenVkBuildRequired
            : VulkanUiReason::BuildDisabled;
        return choice;
    }
    if (!snapshot.InstanceAvailable)
    {
        choice.Reason = VulkanUiReason::InstanceUnavailable;
        return choice;
    }
    if (!snapshot.PresentationAvailable)
    {
        choice.Reason = VulkanUiReason::PresentationUnavailable;
        return choice;
    }
    if (!snapshot.RasterFeaturesAvailable)
    {
        choice.Reason = VulkanUiReason::RasterFeaturesUnavailable;
        return choice;
    }
    if (compute && !snapshot.ComputeFeaturesAvailable)
    {
        choice.Reason = VulkanUiReason::ComputeFeaturesUnavailable;
        return choice;
    }
    // MELONPRIME_VULKAN_PHASE12_UI_ACTIVATION_V2
    // UI availability is governed by the runtime Vulkan feature probe. ROM pixel-parity
    // acceptance remains diagnostic information and must not hide or disable a backend
    // that the user explicitly wants to test. Runtime creation still owns fallback.
    choice.Enabled = true;
    choice.Reason = VulkanUiReason::Available;
    return choice;
}

} // namespace

VulkanPhase12UiState EvaluateVulkanPhase12UiState(
    const VulkanUiFeatureSnapshot& snapshot) noexcept
{
    return {BuildChoice(snapshot, false), BuildChoice(snapshot, true)};
}

const char* VulkanUiReasonName(VulkanUiReason reason) noexcept
{
    switch (reason)
    {
    case VulkanUiReason::Available: return "available";
    case VulkanUiReason::BuildDisabled: return "build-disabled";
    case VulkanUiReason::MoltenVkBuildRequired: return "moltenvk-build-required";
    case VulkanUiReason::InstanceUnavailable: return "instance-unavailable";
    case VulkanUiReason::PresentationUnavailable: return "presentation-unavailable";
    case VulkanUiReason::RasterFeaturesUnavailable: return "raster-features-unavailable";
    case VulkanUiReason::ComputeFeaturesUnavailable: return "compute-features-unavailable";
    case VulkanUiReason::RasterRomIntegrationPending: return "raster-rom-integration-pending";
    case VulkanUiReason::ComputeRomIntegrationPending: return "compute-rom-integration-pending";
    }
    return "unknown";
}

int NormalizePhase12RendererId(int renderer) noexcept
{
    return renderer >= kPhase12RendererSoftware && renderer <= kPhase12RendererVulkanCompute
        ? renderer : kPhase12RendererSoftware;
}

VulkanHardwareSettings BuildVulkanPhase12Preset(bool compute) noexcept
{
    VulkanHardwareSettings result;
    result.Renderer = compute ? kPhase12RendererVulkanCompute : kPhase12RendererVulkanRaster;
    result.ScaleFactor = 4;
    result.BetterPolygons = !compute;
    result.HiresCoordinates = compute;
    result.VSync = false;
    return result;
}

VulkanHardwareSettings ResolveVulkanHardwareSettings(
    const VulkanPersistedHardwareSettings& persisted,
    int renderer,
    bool vsync) noexcept
{
    VulkanHardwareSettings result;
    result.Renderer = NormalizePhase12RendererId(renderer);
    result.ScaleFactor = std::clamp(
        persisted.NewScaleFactor.value_or(persisted.LegacyScaleFactor), 1, 16);
    result.BetterPolygons = persisted.NewBetterPolygons.value_or(
        persisted.LegacyBetterPolygons);
    result.HiresCoordinates = persisted.NewHiresCoordinates.value_or(
        persisted.LegacyHiresCoordinates);
    result.VSync = vsync;
    return result;
}

VulkanDualWriteHardwareSettings BuildVulkanDualWriteSettings(
    const VulkanHardwareSettings& settings) noexcept
{
    const int scale = std::clamp(settings.ScaleFactor, 1, 16);
    return {
        scale,
        settings.BetterPolygons,
        settings.HiresCoordinates,
        scale,
        settings.BetterPolygons,
        settings.HiresCoordinates,
    };
}

VulkanRendererOptionMatrix BuildVulkanRendererOptionMatrix(
    int renderer,
    bool presenterSupportsVsync) noexcept
{
    renderer = NormalizePhase12RendererId(renderer);
    VulkanRendererOptionMatrix result;
    result.SoftwareThreaded = renderer == kPhase12RendererSoftware;
    result.OpenGlDisplay = renderer == kPhase12RendererSoftware;
    result.VSync = presenterSupportsVsync;
    switch (renderer)
    {
    case 1: // OpenGL
        result.InternalResolution = true;
        result.BetterPolygons = true;
        break;
    case 2: // OpenGL Compute
        result.InternalResolution = true;
        result.HiresCoordinates = true;
        break;
    case 3: // Metal raster
        result.InternalResolution = true;
        result.BetterPolygons = true;
        result.HiresCoordinates = true;
        break;
    case 4: // Metal compute
        result.InternalResolution = true;
        result.BetterPolygons = true;
        result.HiresCoordinates = true;
        break;
    case kPhase12RendererVulkanRaster:
        result.InternalResolution = true;
        result.BetterPolygons = true;
        break;
    case kPhase12RendererVulkanCompute:
        result.InternalResolution = true;
        result.HiresCoordinates = true;
        break;
    default:
        break;
    }
    return result;
}

bool ValidateVulkanPhase12ExitContract() noexcept
{
    VulkanUiFeatureSnapshot full;
    full.Platform = VulkanUiPlatform::Windows;
    full.BuildEnabled = true;
    full.InstanceAvailable = true;
    full.PresentationAvailable = true;
    full.RasterFeaturesAvailable = true;
    full.ComputeFeaturesAvailable = true;
    // Phase 12 v2: missing ROM acceptance must not grey out feature-capable backends.
    full.RasterRomIntegrationReady = false;
    full.ComputeRomIntegrationReady = false;
    const auto available = EvaluateVulkanPhase12UiState(full);
    if (!available.Raster.Generated || !available.Raster.Enabled ||
        !available.Compute.Generated || !available.Compute.Enabled)
        return false;

    full.ComputeFeaturesAvailable = false;
    const auto rasterOnly = EvaluateVulkanPhase12UiState(full);
    if (!rasterOnly.Raster.Enabled || rasterOnly.Compute.Enabled ||
        rasterOnly.Compute.Reason != VulkanUiReason::ComputeFeaturesUnavailable)
        return false;

    full.Platform = VulkanUiPlatform::MacOS;
    full.MoltenVkBuildEnabled = false;
    const auto hiddenMac = EvaluateVulkanPhase12UiState(full);
    if (hiddenMac.Raster.Generated || hiddenMac.Compute.Generated)
        return false;

    const auto rasterPreset = BuildVulkanPhase12Preset(false);
    const auto computePreset = BuildVulkanPhase12Preset(true);
    if (rasterPreset.Renderer != kPhase12RendererVulkanRaster ||
        rasterPreset.ScaleFactor != 4 || !rasterPreset.BetterPolygons ||
        rasterPreset.HiresCoordinates || rasterPreset.VSync)
        return false;
    if (computePreset.Renderer != kPhase12RendererVulkanCompute ||
        computePreset.ScaleFactor != 4 || computePreset.BetterPolygons ||
        !computePreset.HiresCoordinates || computePreset.VSync)
        return false;

    VulkanPersistedHardwareSettings persisted;
    persisted.LegacyScaleFactor = 8;
    persisted.LegacyBetterPolygons = true;
    persisted.LegacyHiresCoordinates = false;
    auto migrated = ResolveVulkanHardwareSettings(persisted, 999, true);
    if (migrated.Renderer != kPhase12RendererSoftware || migrated.ScaleFactor != 8)
        return false;
    persisted.NewScaleFactor = 2;
    persisted.NewBetterPolygons = false;
    persisted.NewHiresCoordinates = true;
    migrated = ResolveVulkanHardwareSettings(persisted, kPhase12RendererVulkanCompute, false);
    if (migrated.ScaleFactor != 2 || migrated.BetterPolygons ||
        !migrated.HiresCoordinates)
        return false;
    const auto dual = BuildVulkanDualWriteSettings(migrated);
    return dual.NewScaleFactor == dual.LegacyScaleFactor &&
        dual.NewBetterPolygons == dual.LegacyBetterPolygons &&
        dual.NewHiresCoordinates == dual.LegacyHiresCoordinates;
}

} // namespace melonDS::Vulkan
