#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanPhase13Runtime requires the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_PHASE13_RUNTIME_V1

#include "Vulkan_Phase13Stability.h"

#include <vulkan/vulkan.h>

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <mutex>
#include <string>

namespace MelonPrime::Vulkan
{

struct Phase13PipelineCacheLoad
{
    QByteArray Payload;
    QString Path;
    QString RejectionReason;
    bool Warm = false;
};

class Phase13RuntimeState
{
public:
    melonDS::Vulkan::Phase13PacingDecision Evaluate(
        const melonDS::Vulkan::Phase13PacingInput& input);
    void OnPresented(std::uint64_t serial);
    melonDS::Vulkan::Phase13DeviceLossDecision RecordDeviceLoss(
        melonDS::Vulkan::Phase13DeviceLossStage stage,
        std::int32_t result,
        const std::string& detail);

    [[nodiscard]] melonDS::Vulkan::Phase13PacingStats PacingStats() const;
    [[nodiscard]] bool DeviceLost() const;

private:
    mutable std::mutex Mutex;
    melonDS::Vulkan::Phase13FramePacer Pacer;
    melonDS::Vulkan::Phase13DeviceLossGuard DeviceLoss;
};

[[nodiscard]] std::uint64_t Phase13ShaderManifestHash() noexcept;
[[nodiscard]] melonDS::Vulkan::Phase13PipelineCacheIdentity BuildPhase13PipelineCacheIdentity(
    const VkPhysicalDeviceProperties& properties) noexcept;
[[nodiscard]] QString Phase13PipelineCachePath(
    const melonDS::Vulkan::Phase13PipelineCacheIdentity& identity);
[[nodiscard]] Phase13PipelineCacheLoad LoadPhase13PipelineCache(
    const melonDS::Vulkan::Phase13PipelineCacheIdentity& identity);
bool SavePhase13PipelineCacheAtomic(
    const melonDS::Vulkan::Phase13PipelineCacheIdentity& identity,
    const void* data,
    std::size_t size,
    QString* error = nullptr);

[[nodiscard]] melonDS::Vulkan::Phase13DeviceLossStage Phase13StageFromName(
    const char* stage) noexcept;

} // namespace MelonPrime::Vulkan
