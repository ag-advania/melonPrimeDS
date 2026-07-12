#include "MelonPrimeVulkanPhase13Runtime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cstring>

namespace MelonPrime::Vulkan
{

melonDS::Vulkan::Phase13PacingDecision Phase13RuntimeState::Evaluate(
    const melonDS::Vulkan::Phase13PacingInput& input)
{
    std::lock_guard<std::mutex> lock(Mutex);
    return Pacer.Evaluate(input);
}

void Phase13RuntimeState::OnPresented(std::uint64_t serial)
{
    std::lock_guard<std::mutex> lock(Mutex);
    Pacer.OnPresented(serial);
}

melonDS::Vulkan::Phase13DeviceLossDecision Phase13RuntimeState::RecordDeviceLoss(
    melonDS::Vulkan::Phase13DeviceLossStage stage,
    std::int32_t result,
    const std::string& detail)
{
    std::lock_guard<std::mutex> lock(Mutex);
    return DeviceLoss.Record(stage, result, detail);
}

melonDS::Vulkan::Phase13PacingStats Phase13RuntimeState::PacingStats() const
{
    std::lock_guard<std::mutex> lock(Mutex);
    return Pacer.Stats();
}

bool Phase13RuntimeState::DeviceLost() const
{
    std::lock_guard<std::mutex> lock(Mutex);
    return DeviceLoss.Lost();
}

std::uint64_t Phase13ShaderManifestHash() noexcept
{
    static constexpr char kManifest[] =
        "melonprime-vulkan-phase13|presenter.vert|presenter.frag|"
        "phase9_2d.comp|phase9_final.comp|phase11_compute.comp|"
        "output-ring-v1|backend-v24";
    return melonDS::Vulkan::Phase13HashBytes(kManifest, sizeof(kManifest) - 1);
}

melonDS::Vulkan::Phase13PipelineCacheIdentity BuildPhase13PipelineCacheIdentity(
    const VkPhysicalDeviceProperties& properties) noexcept
{
    melonDS::Vulkan::Phase13PipelineCacheIdentity identity;
    identity.VendorId = properties.vendorID;
    identity.DeviceId = properties.deviceID;
    identity.DriverVersion = properties.driverVersion;
    std::copy_n(properties.pipelineCacheUUID, identity.PipelineCacheUuid.size(),
        identity.PipelineCacheUuid.begin());
    identity.ShaderManifestHash = Phase13ShaderManifestHash();
    identity.BackendVersion = melonDS::Vulkan::kPhase13BackendVersion;
    return identity;
}

QString Phase13PipelineCachePath(
    const melonDS::Vulkan::Phase13PipelineCacheIdentity& identity)
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (root.isEmpty())
        root = QCoreApplication::applicationDirPath() + QStringLiteral("/cache");
    const QString device = QString::fromStdString(
        melonDS::Vulkan::Phase13CacheDeviceDirectory(identity));
    const QString stem = QString::fromStdString(
        melonDS::Vulkan::Phase13CacheFileStem(identity));
    return QDir(root).filePath(
        QStringLiteral("vulkan/%1/%2.bin").arg(device, stem));
}

Phase13PipelineCacheLoad LoadPhase13PipelineCache(
    const melonDS::Vulkan::Phase13PipelineCacheIdentity& identity)
{
    Phase13PipelineCacheLoad result;
    result.Path = Phase13PipelineCachePath(identity);
    QFile file(result.Path);
    if (!file.exists())
    {
        result.RejectionReason = QStringLiteral("cold cache");
        return result;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        result.RejectionReason = file.errorString();
        return result;
    }
    const QByteArray encoded = file.readAll();
    const auto decoded = melonDS::Vulkan::Phase13DecodePipelineCache(
        identity, encoded.constData(), static_cast<std::size_t>(encoded.size()));
    if (!decoded.Valid)
    {
        result.RejectionReason = QString::fromStdString(decoded.RejectionReason);
        return result;
    }
    result.Payload = QByteArray(
        reinterpret_cast<const char*>(decoded.Payload.data()),
        static_cast<qsizetype>(decoded.Payload.size()));
    result.Warm = true;
    return result;
}

bool SavePhase13PipelineCacheAtomic(
    const melonDS::Vulkan::Phase13PipelineCacheIdentity& identity,
    const void* data,
    std::size_t size,
    QString* error)
{
    const QString path = Phase13PipelineCachePath(identity);
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
    {
        if (error)
            *error = QStringLiteral("failed to create pipeline cache directory");
        return false;
    }
    const auto encoded = melonDS::Vulkan::Phase13EncodePipelineCache(identity, data, size);
    if (encoded.empty() && size != 0)
    {
        if (error)
            *error = QStringLiteral("pipeline cache payload is too large");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.write(reinterpret_cast<const char*>(encoded.data()),
            static_cast<qint64>(encoded.size())) != static_cast<qint64>(encoded.size()))
    {
        if (error)
            *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

melonDS::Vulkan::Phase13DeviceLossStage Phase13StageFromName(const char* stage) noexcept
{
    if (!stage)
        return melonDS::Vulkan::Phase13DeviceLossStage::Unknown;
    if (std::strstr(stage, "Acquire") || std::strstr(stage, "acquire"))
        return melonDS::Vulkan::Phase13DeviceLossStage::Acquire;
    if (std::strstr(stage, "Submit") || std::strstr(stage, "submit"))
        return melonDS::Vulkan::Phase13DeviceLossStage::QueueSubmit;
    if (std::strstr(stage, "Present") || std::strstr(stage, "present"))
        return melonDS::Vulkan::Phase13DeviceLossStage::Present;
    if (std::strstr(stage, "Fence") || std::strstr(stage, "fence"))
        return melonDS::Vulkan::Phase13DeviceLossStage::FenceWait;
    if (std::strstr(stage, "Pipeline") || std::strstr(stage, "pipeline"))
        return melonDS::Vulkan::Phase13DeviceLossStage::PipelineCreation;
    if (std::strstr(stage, "logical"))
        return melonDS::Vulkan::Phase13DeviceLossStage::LogicalDevice;
    if (std::strstr(stage, "physical"))
        return melonDS::Vulkan::Phase13DeviceLossStage::PhysicalDevice;
    return melonDS::Vulkan::Phase13DeviceLossStage::Unknown;
}

} // namespace MelonPrime::Vulkan
