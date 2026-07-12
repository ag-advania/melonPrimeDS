#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "Vulkan_Phase13Stability.h is owned by the complete MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_PHASE13_STABILITY_CONTRACT_V1

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace melonDS::Vulkan
{

inline constexpr std::uint32_t kPhase13StabilityContractVersion = 1;
inline constexpr std::uint32_t kPhase13BackendVersion = 24;
inline constexpr std::uint32_t kPhase13PipelineCacheFormatVersion = 1;
inline constexpr std::uint32_t kPhase13MaxVsyncInterval = 4;
inline constexpr std::uint32_t kPhase13ExpectedPrewarmPipelineCount = 36;

using Phase13CacheUuid = std::array<std::uint8_t, 16>;

enum class Phase13SpeedMode : std::uint8_t
{
    Normal,
    FastForward,
    SlowMotion,
};

enum class Phase13PresentMode : std::uint8_t
{
    Fifo,
    Mailbox,
    Immediate,
    FifoRelaxed,
};

enum class Phase13PresentStrategy : std::uint8_t
{
    ConfiguredVsync,
    LowLatencyMailbox,
    LowLatencyImmediate,
    FifoLatestFrameDrop,
};

struct Phase13PresentCapabilities
{
    bool Fifo = true;
    bool Mailbox = false;
    bool Immediate = false;
    bool FifoRelaxed = false;
    bool QtFifoOnlyPresenter = false;
};

struct Phase13PacingConfig
{
    bool VSync = false;
    std::uint32_t VSyncInterval = 1;
    bool FrameLimit = true;
    bool AudioSync = false;
    Phase13SpeedMode SpeedMode = Phase13SpeedMode::Normal;
};

struct Phase13PacingInput
{
    Phase13PacingConfig Config;
    Phase13PresentCapabilities Capabilities;
    std::uint64_t CompletedOutputSerial = 0;
    std::uint64_t LastPresentedSerial = 0;
    bool PresentRequestPending = false;
};

struct Phase13PacingDecision
{
    Phase13PresentMode RequestedMode = Phase13PresentMode::Fifo;
    Phase13PresentMode EffectiveMode = Phase13PresentMode::Fifo;
    Phase13PresentStrategy Strategy = Phase13PresentStrategy::ConfiguredVsync;
    std::uint32_t EffectiveInterval = 1;
    bool RequestPresent = false;
    bool DropOlderUnpresentedOutput = false;
    bool AvoidAcquireBlocking = false;
    bool RestoreConfiguredVsync = false;
    bool ReleaseSkippedLease = false;
};

struct Phase13PacingStats
{
    std::uint64_t Evaluations = 0;
    std::uint64_t PresentRequests = 0;
    std::uint64_t DroppedOutputs = 0;
    std::uint64_t PendingAcquireAvoided = 0;
    std::uint64_t VsyncRestoreTransitions = 0;
    std::uint64_t FastForwardFrames = 0;
    std::uint64_t SlowMotionFrames = 0;
};

class Phase13FramePacer
{
public:
    Phase13PacingDecision Evaluate(const Phase13PacingInput& input) noexcept;
    void OnPresented(std::uint64_t serial) noexcept;
    void Reset() noexcept;

    [[nodiscard]] std::uint64_t LastPresentedSerial() const noexcept;
    [[nodiscard]] Phase13PacingStats Stats() const noexcept;

private:
    std::uint64_t PresentedSerial = 0;
    Phase13SpeedMode PreviousSpeedMode = Phase13SpeedMode::Normal;
    Phase13PacingStats Counters;
};

enum class Phase13DeviceLossStage : std::uint8_t
{
    Unknown,
    Acquire,
    QueueSubmit,
    Present,
    FenceWait,
    PipelineCreation,
    LogicalDevice,
    PhysicalDevice,
};

struct Phase13DeviceLossDecision
{
    bool FirstNotification = false;
    bool StopVulkanRenderer = false;
    bool ForbidResourceReuse = false;
    bool InvalidateOutputGeneration = false;
    bool RequestSoftwareRenderer = false;
    bool RequestNativeQtPresenter = false;
    bool PreserveConfiguredRenderer = true;
    bool EmitOsdOnce = false;
    std::uint64_t LossGeneration = 0;
};

class Phase13DeviceLossGuard
{
public:
    Phase13DeviceLossDecision Record(
        Phase13DeviceLossStage stage,
        std::int32_t result,
        std::string detail);

    [[nodiscard]] bool Lost() const noexcept;
    [[nodiscard]] bool ResourcesReusable() const noexcept;
    [[nodiscard]] Phase13DeviceLossStage Stage() const noexcept;
    [[nodiscard]] std::int32_t Result() const noexcept;
    [[nodiscard]] const std::string& Detail() const noexcept;
    [[nodiscard]] std::uint64_t Generation() const noexcept;

private:
    bool DeviceLost = false;
    bool ReuseAllowed = true;
    Phase13DeviceLossStage FailureStage = Phase13DeviceLossStage::Unknown;
    std::int32_t FailureResult = 0;
    std::string FailureDetail;
    std::uint64_t LossGeneration = 0;
};

struct Phase13PipelineCacheIdentity
{
    std::uint32_t VendorId = 0;
    std::uint32_t DeviceId = 0;
    std::uint32_t DriverVersion = 0;
    Phase13CacheUuid PipelineCacheUuid{};
    std::uint64_t ShaderManifestHash = 0;
    std::uint32_t BackendVersion = kPhase13BackendVersion;

    [[nodiscard]] bool operator==(const Phase13PipelineCacheIdentity& other) const noexcept;
    [[nodiscard]] bool operator!=(const Phase13PipelineCacheIdentity& other) const noexcept
    {
        return !(*this == other);
    }
};

struct Phase13PipelineCacheDecodeResult
{
    bool Valid = false;
    std::string RejectionReason;
    std::vector<std::uint8_t> Payload;
};

[[nodiscard]] std::uint64_t Phase13HashBytes(
    const void* data,
    std::size_t size,
    std::uint64_t seed = 1469598103934665603ull) noexcept;

[[nodiscard]] std::vector<std::uint8_t> Phase13EncodePipelineCache(
    const Phase13PipelineCacheIdentity& identity,
    const void* payload,
    std::size_t payloadSize);

[[nodiscard]] Phase13PipelineCacheDecodeResult Phase13DecodePipelineCache(
    const Phase13PipelineCacheIdentity& expected,
    const void* fileData,
    std::size_t fileSize);

[[nodiscard]] std::string Phase13CacheDeviceDirectory(
    const Phase13PipelineCacheIdentity& identity);

[[nodiscard]] std::string Phase13CacheFileStem(
    const Phase13PipelineCacheIdentity& identity);

struct Phase13PrewarmStats
{
    std::uint32_t RequiredPipelines = 0;
    std::uint32_t CompletedPipelines = 0;
    std::uint32_t GameplayMisses = 0;
    bool WarmCacheLoaded = false;
    bool Complete = false;
};

class Phase13PipelinePrewarm
{
public:
    explicit Phase13PipelinePrewarm(
        std::uint32_t requiredPipelines = kPhase13ExpectedPrewarmPipelineCount);

    void SetWarmCacheLoaded(bool warm) noexcept;
    bool MarkPipelineReady(std::uint32_t variant) noexcept;
    bool RecordGameplayVariantUse(std::uint32_t variant) noexcept;

    [[nodiscard]] Phase13PrewarmStats Stats() const noexcept;
    [[nodiscard]] float Progress() const noexcept;

private:
    std::vector<bool> Ready;
    bool Warm = false;
    std::uint32_t Completed = 0;
    std::uint32_t Misses = 0;
};

struct Phase13ExitAudit
{
    bool VsyncOnOff = false;
    bool FrameLimitOnOff = false;
    bool FastForwardAndSlowMotion = false;
    bool AudioSyncPreserved = false;
    bool PresentModeSelection = false;
    bool LatestFrameDrop = false;
    bool LeaseReleaseOnDrop = false;
    bool AcquireBlockingAvoided = false;
    bool PipelineCacheColdWarm = false;
    bool PipelineCacheIdentityValidation = false;
    bool PipelineCacheAtomicWrite = false;
    bool PipelinePrewarm = false;
    bool DeviceLossInjection = false;
    bool DeviceLossFallback = false;
    bool ConfigPreserved = false;
    bool NoDeadlock = false;
    bool NoStaleOutput = false;

    [[nodiscard]] bool Passed() const noexcept;
};

} // namespace melonDS::Vulkan
