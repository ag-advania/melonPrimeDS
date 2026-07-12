#include "Vulkan_Phase13Stability.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace melonDS::Vulkan
{
namespace
{

constexpr std::array<std::uint8_t, 8> kCacheMagic{
    'M', 'P', 'V', 'K', 'P', '1', '3', '\0'};
constexpr std::size_t kHeaderSize =
    kCacheMagic.size() + sizeof(std::uint32_t) * 7 + 16 + sizeof(std::uint64_t) * 2;

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
}

void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
}

bool ReadU32(const std::uint8_t*& cursor, const std::uint8_t* end, std::uint32_t& value)
{
    if (static_cast<std::size_t>(end - cursor) < sizeof(value))
        return false;
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= static_cast<std::uint32_t>(*cursor++) << shift;
    return true;
}

bool ReadU64(const std::uint8_t*& cursor, const std::uint8_t* end, std::uint64_t& value)
{
    if (static_cast<std::size_t>(end - cursor) < sizeof(value))
        return false;
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t>(*cursor++) << shift;
    return true;
}

Phase13PresentMode ChooseLowLatencyMode(const Phase13PresentCapabilities& caps) noexcept
{
    if (caps.Mailbox)
        return Phase13PresentMode::Mailbox;
    if (caps.Immediate)
        return Phase13PresentMode::Immediate;
    if (caps.FifoRelaxed)
        return Phase13PresentMode::FifoRelaxed;
    return Phase13PresentMode::Fifo;
}

} // namespace

Phase13PacingDecision Phase13FramePacer::Evaluate(const Phase13PacingInput& input) noexcept
{
    ++Counters.Evaluations;
    Phase13PacingDecision decision;
    decision.EffectiveInterval = std::clamp<std::uint32_t>(
        input.Config.VSyncInterval, 1, kPhase13MaxVsyncInterval);

    const bool speedChangedToNormal =
        PreviousSpeedMode != Phase13SpeedMode::Normal &&
        input.Config.SpeedMode == Phase13SpeedMode::Normal;
    decision.RestoreConfiguredVsync = speedChangedToNormal;
    if (speedChangedToNormal)
        ++Counters.VsyncRestoreTransitions;
    PreviousSpeedMode = input.Config.SpeedMode;

    const bool accelerated = input.Config.SpeedMode != Phase13SpeedMode::Normal;
    if (input.Config.SpeedMode == Phase13SpeedMode::FastForward)
        ++Counters.FastForwardFrames;
    else if (input.Config.SpeedMode == Phase13SpeedMode::SlowMotion)
        ++Counters.SlowMotionFrames;

    if (accelerated || !input.Config.VSync)
    {
        decision.RequestedMode = ChooseLowLatencyMode(input.Capabilities);
        decision.EffectiveMode = input.Capabilities.QtFifoOnlyPresenter
            ? Phase13PresentMode::Fifo : decision.RequestedMode;
        if (decision.RequestedMode == Phase13PresentMode::Mailbox)
            decision.Strategy = Phase13PresentStrategy::LowLatencyMailbox;
        else if (decision.RequestedMode == Phase13PresentMode::Immediate)
            decision.Strategy = Phase13PresentStrategy::LowLatencyImmediate;
        else
            decision.Strategy = Phase13PresentStrategy::FifoLatestFrameDrop;
        decision.DropOlderUnpresentedOutput = true;
        decision.ReleaseSkippedLease = true;
        decision.AvoidAcquireBlocking = true;
        decision.EffectiveInterval = 1;
    }
    else
    {
        decision.RequestedMode = Phase13PresentMode::Fifo;
        decision.EffectiveMode = Phase13PresentMode::Fifo;
        decision.Strategy = Phase13PresentStrategy::ConfiguredVsync;
    }

    const std::uint64_t lastPresented = std::max(PresentedSerial, input.LastPresentedSerial);
    const bool hasNewOutput = input.CompletedOutputSerial > lastPresented;
    const bool intervalAllows = !input.Config.VSync || accelerated ||
        (input.CompletedOutputSerial % decision.EffectiveInterval) == 0;

    if (input.PresentRequestPending)
    {
        decision.RequestPresent = false;
        if (hasNewOutput)
        {
            decision.DropOlderUnpresentedOutput = true;
            decision.ReleaseSkippedLease = true;
            ++Counters.DroppedOutputs;
        }
        if (decision.AvoidAcquireBlocking)
            ++Counters.PendingAcquireAvoided;
        return decision;
    }

    decision.RequestPresent = hasNewOutput && intervalAllows;
    if (decision.RequestPresent)
        ++Counters.PresentRequests;
    return decision;
}

void Phase13FramePacer::OnPresented(std::uint64_t serial) noexcept
{
    PresentedSerial = std::max(PresentedSerial, serial);
}

void Phase13FramePacer::Reset() noexcept
{
    PresentedSerial = 0;
    PreviousSpeedMode = Phase13SpeedMode::Normal;
    Counters = {};
}

std::uint64_t Phase13FramePacer::LastPresentedSerial() const noexcept
{
    return PresentedSerial;
}

Phase13PacingStats Phase13FramePacer::Stats() const noexcept
{
    return Counters;
}

Phase13DeviceLossDecision Phase13DeviceLossGuard::Record(
    Phase13DeviceLossStage stage,
    std::int32_t result,
    std::string detail)
{
    Phase13DeviceLossDecision decision;
    decision.StopVulkanRenderer = true;
    decision.ForbidResourceReuse = true;
    decision.InvalidateOutputGeneration = true;
    decision.RequestSoftwareRenderer = true;
    decision.RequestNativeQtPresenter = true;
    decision.PreserveConfiguredRenderer = true;

    if (!DeviceLost)
    {
        DeviceLost = true;
        ReuseAllowed = false;
        FailureStage = stage;
        FailureResult = result;
        FailureDetail = std::move(detail);
        ++LossGeneration;
        decision.FirstNotification = true;
        decision.EmitOsdOnce = true;
    }
    decision.LossGeneration = LossGeneration;
    return decision;
}

bool Phase13DeviceLossGuard::Lost() const noexcept { return DeviceLost; }
bool Phase13DeviceLossGuard::ResourcesReusable() const noexcept { return ReuseAllowed; }
Phase13DeviceLossStage Phase13DeviceLossGuard::Stage() const noexcept { return FailureStage; }
std::int32_t Phase13DeviceLossGuard::Result() const noexcept { return FailureResult; }
const std::string& Phase13DeviceLossGuard::Detail() const noexcept { return FailureDetail; }
std::uint64_t Phase13DeviceLossGuard::Generation() const noexcept { return LossGeneration; }

bool Phase13PipelineCacheIdentity::operator==(
    const Phase13PipelineCacheIdentity& other) const noexcept
{
    return VendorId == other.VendorId &&
        DeviceId == other.DeviceId &&
        DriverVersion == other.DriverVersion &&
        PipelineCacheUuid == other.PipelineCacheUuid &&
        ShaderManifestHash == other.ShaderManifestHash &&
        BackendVersion == other.BackendVersion;
}

std::uint64_t Phase13HashBytes(
    const void* data,
    std::size_t size,
    std::uint64_t seed) noexcept
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = seed;
    for (std::size_t index = 0; index < size; ++index)
    {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

std::vector<std::uint8_t> Phase13EncodePipelineCache(
    const Phase13PipelineCacheIdentity& identity,
    const void* payload,
    std::size_t payloadSize)
{
    if (payloadSize > std::numeric_limits<std::uint32_t>::max())
        return {};
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + payloadSize);
    out.insert(out.end(), kCacheMagic.begin(), kCacheMagic.end());
    AppendU32(out, kPhase13PipelineCacheFormatVersion);
    AppendU32(out, static_cast<std::uint32_t>(kHeaderSize));
    AppendU32(out, identity.VendorId);
    AppendU32(out, identity.DeviceId);
    AppendU32(out, identity.DriverVersion);
    AppendU32(out, identity.BackendVersion);
    out.insert(out.end(), identity.PipelineCacheUuid.begin(), identity.PipelineCacheUuid.end());
    AppendU64(out, identity.ShaderManifestHash);
    const std::uint64_t payloadHash = Phase13HashBytes(payload, payloadSize);
    AppendU64(out, payloadHash);
    AppendU32(out, static_cast<std::uint32_t>(payloadSize));
    if (payload && payloadSize)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(payload);
        out.insert(out.end(), bytes, bytes + payloadSize);
    }
    return out;
}

Phase13PipelineCacheDecodeResult Phase13DecodePipelineCache(
    const Phase13PipelineCacheIdentity& expected,
    const void* fileData,
    std::size_t fileSize)
{
    Phase13PipelineCacheDecodeResult result;
    if (!fileData || fileSize < kHeaderSize)
    {
        result.RejectionReason = "cache file is truncated";
        return result;
    }
    const auto* begin = static_cast<const std::uint8_t*>(fileData);
    const auto* cursor = begin;
    const auto* end = begin + fileSize;
    if (!std::equal(kCacheMagic.begin(), kCacheMagic.end(), cursor))
    {
        result.RejectionReason = "cache magic mismatch";
        return result;
    }
    cursor += kCacheMagic.size();

    std::uint32_t formatVersion = 0;
    std::uint32_t headerSize = 0;
    Phase13PipelineCacheIdentity found;
    std::uint64_t payloadHash = 0;
    std::uint32_t payloadSize = 0;
    if (!ReadU32(cursor, end, formatVersion) || !ReadU32(cursor, end, headerSize) ||
        !ReadU32(cursor, end, found.VendorId) || !ReadU32(cursor, end, found.DeviceId) ||
        !ReadU32(cursor, end, found.DriverVersion) || !ReadU32(cursor, end, found.BackendVersion))
    {
        result.RejectionReason = "cache header is truncated";
        return result;
    }
    if (formatVersion != kPhase13PipelineCacheFormatVersion || headerSize != kHeaderSize)
    {
        result.RejectionReason = "cache format version mismatch";
        return result;
    }
    if (static_cast<std::size_t>(end - cursor) < found.PipelineCacheUuid.size())
    {
        result.RejectionReason = "cache UUID is truncated";
        return result;
    }
    std::copy_n(cursor, found.PipelineCacheUuid.size(), found.PipelineCacheUuid.begin());
    cursor += found.PipelineCacheUuid.size();
    if (!ReadU64(cursor, end, found.ShaderManifestHash) ||
        !ReadU64(cursor, end, payloadHash) || !ReadU32(cursor, end, payloadSize))
    {
        result.RejectionReason = "cache payload header is truncated";
        return result;
    }
    if (found != expected)
    {
        result.RejectionReason = "cache device, driver, UUID, shader manifest or backend identity mismatch";
        return result;
    }
    if (static_cast<std::size_t>(end - cursor) != payloadSize)
    {
        result.RejectionReason = "cache payload size mismatch";
        return result;
    }
    if (Phase13HashBytes(cursor, payloadSize) != payloadHash)
    {
        result.RejectionReason = "cache payload hash mismatch";
        return result;
    }
    result.Payload.assign(cursor, end);
    result.Valid = true;
    return result;
}

std::string Phase13CacheDeviceDirectory(const Phase13PipelineCacheIdentity& identity)
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
        << std::setw(4) << identity.VendorId << '-'
        << std::setw(4) << identity.DeviceId;
    return stream.str();
}

std::string Phase13CacheFileStem(const Phase13PipelineCacheIdentity& identity)
{
    std::uint64_t hash = Phase13HashBytes(&identity.VendorId, sizeof(identity.VendorId));
    hash = Phase13HashBytes(&identity.DeviceId, sizeof(identity.DeviceId), hash);
    hash = Phase13HashBytes(&identity.DriverVersion, sizeof(identity.DriverVersion), hash);
    hash = Phase13HashBytes(identity.PipelineCacheUuid.data(), identity.PipelineCacheUuid.size(), hash);
    hash = Phase13HashBytes(&identity.ShaderManifestHash, sizeof(identity.ShaderManifestHash), hash);
    hash = Phase13HashBytes(&identity.BackendVersion, sizeof(identity.BackendVersion), hash);
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

Phase13PipelinePrewarm::Phase13PipelinePrewarm(std::uint32_t requiredPipelines)
    : Ready(requiredPipelines, false)
{
}

void Phase13PipelinePrewarm::SetWarmCacheLoaded(bool warm) noexcept
{
    Warm = warm;
}

bool Phase13PipelinePrewarm::MarkPipelineReady(std::uint32_t variant) noexcept
{
    if (variant >= Ready.size())
        return false;
    if (!Ready[variant])
    {
        Ready[variant] = true;
        ++Completed;
    }
    return true;
}

bool Phase13PipelinePrewarm::RecordGameplayVariantUse(std::uint32_t variant) noexcept
{
    if (variant >= Ready.size() || !Ready[variant])
    {
        ++Misses;
        return false;
    }
    return true;
}

Phase13PrewarmStats Phase13PipelinePrewarm::Stats() const noexcept
{
    Phase13PrewarmStats stats;
    stats.RequiredPipelines = static_cast<std::uint32_t>(Ready.size());
    stats.CompletedPipelines = Completed;
    stats.GameplayMisses = Misses;
    stats.WarmCacheLoaded = Warm;
    stats.Complete = Completed == Ready.size();
    return stats;
}

float Phase13PipelinePrewarm::Progress() const noexcept
{
    return Ready.empty() ? 1.0f : static_cast<float>(Completed) / static_cast<float>(Ready.size());
}

bool Phase13ExitAudit::Passed() const noexcept
{
    return VsyncOnOff && FrameLimitOnOff && FastForwardAndSlowMotion &&
        AudioSyncPreserved && PresentModeSelection && LatestFrameDrop &&
        LeaseReleaseOnDrop && AcquireBlockingAvoided && PipelineCacheColdWarm &&
        PipelineCacheIdentityValidation && PipelineCacheAtomicWrite && PipelinePrewarm &&
        DeviceLossInjection && DeviceLossFallback && ConfigPreserved &&
        NoDeadlock && NoStaleOutput;
}

} // namespace melonDS::Vulkan
