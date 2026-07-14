#pragma once

// MELONPRIME_VULKAN_R26_CANONICAL_SETTINGS_V1

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanSettings is owned by the Vulkan build gate"
#endif

#include <QString>
#include <QtGlobal>

#include "Config.h"
#include "MelonPrimeLocalization.h"
#include "MelonPrimeVideoBackend.h"

namespace MelonPrime::Vulkan
{

struct VulkanLocalizedStrings
{
    QString VulkanName;
    QString RasterDescription;
    QString RasterPreset;
    QString OpenSettings;
    QString ScaleDescription;
    QString BuildUnavailable;
};

struct VulkanRuntimeUiState
{
    bool Enabled = false;
    QString Tooltip;
};

struct VulkanHardwareSettings
{
    int Renderer = 0;
    int ScaleFactor = 1;
    bool BetterPolygons = false;
    bool HiresCoordinates = false;
    bool VSync = false;
};

[[nodiscard]] inline VulkanLocalizedStrings VulkanStringsForLanguage(
    MelonPrime::UiText::MenuLangId language)
{
    (void)language;
    VulkanLocalizedStrings strings;
    strings.VulkanName = QStringLiteral("Vulkan");
    strings.RasterDescription = MelonPrime::UiText::Tr(
        "Native Vulkan raster renderer. Falls back safely if initialization fails.");
    strings.RasterPreset = MelonPrime::UiText::Tr("Apply Vulkan raster preset");
    strings.OpenSettings = MelonPrime::UiText::Tr("Open Vulkan video settings...");
    strings.ScaleDescription = MelonPrime::UiText::Tr(
        "Internal 3D render scale used by OpenGL, Metal, and Vulkan hardware renderers.");
    strings.BuildUnavailable = MelonPrime::UiText::Tr(
        "Vulkan is unavailable in this build.");
    return strings;
}

[[nodiscard]] inline VulkanRuntimeUiState BuildVulkanRuntimeUiState(
    MelonPrime::UiText::MenuLangId language,
    int canonicalVulkanRenderer)
{
    const auto strings = VulkanStringsForLanguage(language);
    VulkanRuntimeUiState result;
    result.Enabled = MelonPrime::VideoBackend::RendererIsAvailableInBuild(
        canonicalVulkanRenderer);
    result.Tooltip = result.Enabled
        ? strings.RasterDescription
        : strings.BuildUnavailable;
    return result;
}

inline void MigrateVulkanHardwareSettings(Config::Table& config)
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

[[nodiscard]] inline VulkanHardwareSettings ReadVulkanHardwareSettings(
    Config::Table& config,
    int canonicalVulkanRenderer)
{
    MigrateVulkanHardwareSettings(config);
    VulkanHardwareSettings settings;
    const int renderer = MelonPrime::VideoBackend::MigrateLegacyRendererId(
        config.GetInt("3D.Renderer"));
    settings.Renderer = renderer == canonicalVulkanRenderer ? renderer : 0;
    settings.ScaleFactor = qBound(
        1, config.GetInt("3D.Hardware.ScaleFactor"), 16);
    settings.BetterPolygons = config.GetBool("3D.Hardware.BetterPolygons");
    settings.HiresCoordinates = config.GetBool("3D.Hardware.HiresCoordinates");
    settings.VSync = config.GetBool("Screen.VSync");
    return settings;
}

inline void WriteVulkanHardwareSettings(
    Config::Table& config,
    const VulkanHardwareSettings& settings,
    int canonicalVulkanRenderer)
{
    const int renderer = settings.Renderer == canonicalVulkanRenderer
        ? canonicalVulkanRenderer
        : 0;
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

inline void ApplyVulkanPreset(
    Config::Table& config,
    int canonicalVulkanRenderer)
{
    VulkanHardwareSettings settings;
    settings.Renderer = canonicalVulkanRenderer;
    settings.ScaleFactor = 4;
    settings.BetterPolygons = true;
    settings.HiresCoordinates = false;
    settings.VSync = false;
    WriteVulkanHardwareSettings(config, settings, canonicalVulkanRenderer);
}

} // namespace MelonPrime::Vulkan
