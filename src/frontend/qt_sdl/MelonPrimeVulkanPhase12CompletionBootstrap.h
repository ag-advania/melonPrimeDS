#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "Phase 12 Vulkan UI integration requires the Vulkan build gate"
#endif

#include <QString>

#include "Vulkan_Phase12UiContract.h"
#include "MelonPrimeLocalization.h"

namespace Config { class Table; }

namespace MelonPrime::Vulkan
{

// MELONPRIME_VULKAN_PHASE12_COMPLETION_BOOTSTRAP_V1

struct Phase12LocalizedStrings
{
    QString VulkanName;
    QString VulkanComputeName;
    QString RasterDescription;
    QString ComputeDescription;
    QString RasterPreset;
    QString ComputePreset;
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

[[nodiscard]] Phase12LocalizedStrings Phase12StringsForLanguage(
    MelonPrime::UiText::MenuLangId language);
[[nodiscard]] Phase12RuntimeUiState BuildPhase12RuntimeUiState(
    MelonPrime::UiText::MenuLangId language);
void MigratePhase12HardwareSettings(Config::Table& config);
[[nodiscard]] melonDS::Vulkan::VulkanHardwareSettings ReadPhase12HardwareSettings(
    Config::Table& config);
void WritePhase12HardwareSettings(
    Config::Table& config,
    const melonDS::Vulkan::VulkanHardwareSettings& settings);
void ApplyPhase12VulkanPreset(Config::Table& config, bool compute);
int RunPhase12CompletionHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
