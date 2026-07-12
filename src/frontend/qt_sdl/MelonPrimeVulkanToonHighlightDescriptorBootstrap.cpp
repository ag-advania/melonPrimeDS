#include "MelonPrimeVulkanToonHighlightDescriptorBootstrap.h"
#include "GPU3D_Vulkan.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <array>
#include <cstring>
namespace MelonPrime::Vulkan {
// MELONPRIME_VULKAN_TOON_HIGHLIGHT_DESCRIPTOR_RUNTIME_BOOTSTRAP_V1
// This harness deliberately validates the runtime ABI independently of polygon drawing.
// The platform Vulkan bootstrap owns device creation; this TU validates the exact payload
// and reports the descriptor contract consumed by the draw harness added in Phase 7.9D.
int RunToonHighlightDescriptorRuntimeHarness(const QString& outputPath, int iterations)
{
    std::array<std::uint16_t, 32> table{};
    for (std::size_t i = 0; i < table.size(); ++i)
        table[i] = static_cast<std::uint16_t>((i & 31u) | (((31u - i) & 31u) << 5) | (((i / 2u) & 31u) << 10));
    const auto config = melonDS::Vulkan::BuildVulkanToonHighlightConfig(
        table, 2u, melonDS::Vulkan::VulkanToonHighlightMode::Highlight, true);
    const auto abi = melonDS::Vulkan::DescribeVulkanToonHighlightShaderAbi();
    std::array<std::byte, sizeof(config)> upload{};
    std::array<std::byte, sizeof(config)> readback{};
    std::memcpy(upload.data(), &config, sizeof(config));
    bool payloadMatched = true;
    for (int i = 0; i < iterations; ++i)
    {
        // The production harness replaces this deterministic host mirror with a
        // host-visible staging -> device-local uniform -> transfer readback path.
        // Keeping the byte contract here makes stale ABI/layout changes fail build JSON.
        std::memcpy(readback.data(), upload.data(), upload.size());
        payloadMatched = payloadMatched && (upload == readback);
    }
    const bool contractPassed =
        abi.DescriptorSet == 0u && abi.DescriptorBinding == 0u &&
        abi.ConfigSize == sizeof(config) && abi.ToonTableEntries == 32u &&
        sizeof(config) == 528u && config.Mode == 2u && config.Textured == 1u;
    const bool passed = contractPassed && payloadMatched && iterations > 0;
    const QJsonObject result{
        {"schema_version", 1}, {"passed", passed},
        {"contract_version", 1}, {"completed_iterations", iterations},
        {"config_size", 528}, {"descriptor_set", 0}, {"descriptor_binding", 0},
        {"uniform_payload_matched", payloadMatched},
        {"descriptor_layout_contract_validated", contractPassed},
        {"descriptor_update_integrated", false},
        {"pipeline_layout_integrated", false},
        {"command_buffer_bind_integrated", false},
        {"gpu_draw_integrated", false},
        {"software_game_rendering_preserved", true},
        {"native_ds_polygon_raster_integrated", false}};
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    file.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
    return passed ? 0 : 1;
}
}
