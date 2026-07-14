#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanUiCompat is owned by the Vulkan build gate"
#endif

#include <QString>
#include <QtGlobal>

#include "Config.h"
#include "MelonPrimeLocalization.h"
#include "Vulkan_Phase12UiContract.h"

namespace MelonPrime::Vulkan
{

// MELONPRIME_VULKAN_SETTINGS_UI_COMPAT_V1
struct Phase12LocalizedStrings
{
    QString VulkanName;
    QString VulkanComputeName;
    QString RasterDescription;
    QString ComputeDescription;
    QString RasterPreset;
    QString ComputePreset;
    QString OpenSettings;
    QString ScaleDescription;
    QString BuildUnavailable;
    QString InstanceUnavailable;
    QString PresentationUnavailable;
    QString RasterFeaturesUnavailable;
    QString ComputeFeaturesUnavailable;
    QString RasterIntegrationPending;
    QString ComputeIntegrationPending;
    QString MoltenVkRequired;
};

struct Phase12RuntimeUiState
{
    melonDS::Vulkan::VulkanPhase12UiState Contract;
    QString RasterTooltip;
    QString ComputeTooltip;
    QString DeviceName;
    QString DriverReason;
};

[[nodiscard]] inline Phase12LocalizedStrings Phase12StringsForLanguage(
    MelonPrime::UiText::MenuLangId language)
{
    (void)language;
    Phase12LocalizedStrings strings;
    strings.VulkanName = QStringLiteral("Vulkan");
    strings.VulkanComputeName = QStringLiteral("Vulkan Compute Shader");
    strings.RasterDescription = MelonPrime::UiText::Tr(
        "Native Vulkan raster renderer. Falls back safely if initialization fails.");
    strings.ComputeDescription = MelonPrime::UiText::Tr(
        "Vulkan Compute selection using the validated Vulkan reference backend.");
    strings.RasterPreset = MelonPrime::UiText::Tr("Apply Vulkan raster preset");
    strings.ComputePreset = MelonPrime::UiText::Tr("Apply Vulkan Compute preset");
    strings.OpenSettings = MelonPrime::UiText::Tr("Open Vulkan video settings...");
    strings.ScaleDescription = MelonPrime::UiText::Tr(
        "Internal 3D render scale used by OpenGL, Metal, and Vulkan hardware renderers.");
    strings.BuildUnavailable = MelonPrime::UiText::Tr("Vulkan is unavailable in this build.");
    strings.InstanceUnavailable = MelonPrime::UiText::Tr("Vulkan instance creation failed.");
    strings.PresentationUnavailable = MelonPrime::UiText::Tr("Vulkan presentation is unavailable.");
    strings.RasterFeaturesUnavailable = MelonPrime::UiText::Tr("Required Vulkan raster features are unavailable.");
    strings.ComputeFeaturesUnavailable = MelonPrime::UiText::Tr("Required Vulkan compute features are unavailable.");
    strings.RasterIntegrationPending = MelonPrime::UiText::Tr("Vulkan raster integration is not available.");
    strings.ComputeIntegrationPending = MelonPrime::UiText::Tr("Vulkan Compute integration is not available.");
    strings.MoltenVkRequired = MelonPrime::UiText::Tr("This Vulkan selection requires a MoltenVK-enabled build on macOS.");
    return strings;
}

[[nodiscard]] inline Phase12RuntimeUiState BuildPhase12RuntimeUiState(
    MelonPrime::UiText::MenuLangId language)
{
    Phase12RuntimeUiState result;
    result.Contract.Raster.Generated = true;
    result.Contract.Raster.Enabled = true;
    result.Contract.Raster.Reason = melonDS::Vulkan::VulkanUiReason::Available;
    result.Contract.Compute.Generated = true;
    result.Contract.Compute.Enabled = true;
    result.Contract.Compute.Reason = melonDS::Vulkan::VulkanUiReason::Available;
    const auto strings = Phase12StringsForLanguage(language);
    result.RasterTooltip = strings.RasterDescription;
    result.ComputeTooltip = strings.ComputeDescription;
    return result;
}

inline void MigratePhase12HardwareSettings(Config::Table& config)
{
    const int scale = config.HasKey("3D.Hardware.ScaleFactor")
        ? config.GetInt("3D.Hardware.ScaleFactor")
        : config.GetInt("3D.GL.ScaleFactor");
    const bool better = config.HasKey("3D.Hardware.BetterPolygons")
        ? config.GetBool("3D.Hardware.BetterPolygons")
        : config.GetBool("3D.GL.BetterPolygons");
    const bool hires = config.HasKey("3D.Hardware.HiresCoordinates")
        ? config.GetBool("3D.Hardware.HiresCoordinates")
        : config.GetBool("3D.GL.HiresCoordinates");
    const int boundedScale = qBound(1, scale, 16);
    config.SetInt("3D.Hardware.ScaleFactor", boundedScale);
    config.SetBool("3D.Hardware.BetterPolygons", better);
    config.SetBool("3D.Hardware.HiresCoordinates", hires);
    config.SetInt("3D.GL.ScaleFactor", boundedScale);
    config.SetBool("3D.GL.BetterPolygons", better);
    config.SetBool("3D.GL.HiresCoordinates", hires);
}

[[nodiscard]] inline melonDS::Vulkan::VulkanHardwareSettings ReadPhase12HardwareSettings(
    Config::Table& config)
{
    MigratePhase12HardwareSettings(config);
    melonDS::Vulkan::VulkanHardwareSettings settings;
    const int renderer = config.GetInt("3D.Renderer");
    settings.Renderer = renderer == melonDS::Vulkan::kPhase12RendererVulkanRaster ||
            renderer == melonDS::Vulkan::kPhase12RendererVulkanCompute
        ? renderer
        : melonDS::Vulkan::kPhase12RendererSoftware;
    settings.ScaleFactor = qBound(1, config.GetInt("3D.Hardware.ScaleFactor"), 16);
    settings.BetterPolygons = config.GetBool("3D.Hardware.BetterPolygons");
    settings.HiresCoordinates = config.GetBool("3D.Hardware.HiresCoordinates");
    settings.VSync = config.GetBool("Screen.VSync");
    return settings;
}

inline void WritePhase12HardwareSettings(
    Config::Table& config,
    const melonDS::Vulkan::VulkanHardwareSettings& settings)
{
    const int renderer = settings.Renderer == melonDS::Vulkan::kPhase12RendererVulkanCompute
        ? melonDS::Vulkan::kPhase12RendererVulkanCompute
        : settings.Renderer == melonDS::Vulkan::kPhase12RendererVulkanRaster
            ? melonDS::Vulkan::kPhase12RendererVulkanRaster
            : melonDS::Vulkan::kPhase12RendererSoftware;
    const int scale = qBound(1, settings.ScaleFactor, 16);
    config.SetInt("3D.Renderer", renderer);
    config.SetInt("3D.Hardware.ScaleFactor", scale);
    config.SetBool("3D.Hardware.BetterPolygons", settings.BetterPolygons);
    config.SetBool("3D.Hardware.HiresCoordinates", settings.HiresCoordinates);
    config.SetInt("3D.GL.ScaleFactor", scale);
    config.SetBool("3D.GL.BetterPolygons", settings.BetterPolygons);
    config.SetBool("3D.GL.HiresCoordinates", settings.HiresCoordinates);
    config.SetBool("Screen.VSync", settings.VSync);
}

inline void ApplyPhase12VulkanPreset(Config::Table& config, bool compute)
{
    melonDS::Vulkan::VulkanHardwareSettings settings;
    settings.Renderer = compute
        ? melonDS::Vulkan::kPhase12RendererVulkanCompute
        : melonDS::Vulkan::kPhase12RendererVulkanRaster;
    settings.ScaleFactor = 4;
    settings.BetterPolygons = !compute;
    settings.HiresCoordinates = compute;
    settings.VSync = false;
    WritePhase12HardwareSettings(config, settings);
}

} // namespace MelonPrime::Vulkan
