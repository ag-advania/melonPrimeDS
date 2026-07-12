#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace melonDS::Vulkan
{

// MELONPRIME_VULKAN_TEXCACHE_RESIDENCY_CONTRACT_V1
inline constexpr std::uint32_t kTextureCacheResidencyContractVersion = 1;
inline constexpr std::uint32_t kTextureCacheSamplerCount = 9;

enum class VulkanTextureSamplerAxisMode : std::uint32_t
{
    Clamp = 0,
    Repeat = 1,
    Mirror = 2,
};

struct VulkanTextureCacheKey
{
    std::uint32_t TexParam = 0;
    std::uint32_t TexPalette = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t Format = 0;
    std::uint32_t TextureAddress = 0;
    std::uint32_t PaletteAddress = 0;
    std::uint32_t Color0Transparent = 0;
};

static_assert(sizeof(VulkanTextureCacheKey) == 32);
static_assert(offsetof(VulkanTextureCacheKey, TextureAddress) == 20);

inline bool operator==(const VulkanTextureCacheKey& left,
                       const VulkanTextureCacheKey& right) noexcept
{
    return left.TexParam == right.TexParam &&
        left.TexPalette == right.TexPalette &&
        left.Width == right.Width &&
        left.Height == right.Height &&
        left.Format == right.Format &&
        left.TextureAddress == right.TextureAddress &&
        left.PaletteAddress == right.PaletteAddress &&
        left.Color0Transparent == right.Color0Transparent;
}

struct VulkanTextureCacheRequest
{
    std::uint32_t TexParam = 0;
    std::uint32_t TexPalette = 0;
    std::uint64_t ContentGeneration = 0;
    std::uint32_t SourceOrder = 0;
};

struct VulkanTextureCacheEntry
{
    VulkanTextureCacheKey Key{};
    std::uint64_t ContentGeneration = 0;
    std::uint64_t LastUseSerial = 0;
    std::uint32_t UploadCount = 0;
    std::uint32_t ImageSlot = 0;
    bool Resident = false;
};

struct VulkanTextureDescriptorSlot
{
    std::uint32_t EntryIndex = 0;
    std::uint32_t SamplerIndex = 0;
};

struct VulkanTextureCacheDecision
{
    std::uint32_t SourceOrder = 0;
    std::uint32_t EntryIndex = 0;
    std::uint32_t DescriptorSlot = 0;
    std::uint32_t SamplerIndex = 0;
    bool CacheHit = false;
    bool UploadRequired = false;
    bool Invalidated = false;
};

struct VulkanTextureCachePlan
{
    std::vector<VulkanTextureCacheEntry> Entries;
    std::vector<VulkanTextureDescriptorSlot> DescriptorSlots;
    std::vector<VulkanTextureCacheDecision> Decisions;
    std::uint32_t CacheHitCount = 0;
    std::uint32_t CacheMissCount = 0;
    std::uint32_t UploadCount = 0;
    std::uint32_t InvalidationCount = 0;
    bool SourceOrderPreserved = false;
    bool NonAdjacentReuseObserved = false;
    bool DescriptorReuseObserved = false;
    bool UnchangedTextureNotReuploaded = false;

    void Clear() noexcept
    {
        Entries.clear();
        DescriptorSlots.clear();
        Decisions.clear();
        CacheHitCount = 0;
        CacheMissCount = 0;
        UploadCount = 0;
        InvalidationCount = 0;
        SourceOrderPreserved = false;
        NonAdjacentReuseObserved = false;
        DescriptorReuseObserved = false;
        UnchangedTextureNotReuploaded = false;
    }
};

inline std::uint32_t TextureCacheWidth(std::uint32_t texParam) noexcept
{
    return 8u << ((texParam >> 20) & 0x7u);
}

inline std::uint32_t TextureCacheHeight(std::uint32_t texParam) noexcept
{
    return 8u << ((texParam >> 23) & 0x7u);
}

inline VulkanTextureSamplerAxisMode DecodeVulkanTextureSamplerAxis(
    std::uint32_t texParam, bool vertical) noexcept
{
    const std::uint32_t repeatBit = vertical ? 17u : 16u;
    const std::uint32_t mirrorBit = vertical ? 19u : 18u;
    if ((texParam & (1u << repeatBit)) == 0)
        return VulkanTextureSamplerAxisMode::Clamp;
    return (texParam & (1u << mirrorBit)) != 0
        ? VulkanTextureSamplerAxisMode::Mirror
        : VulkanTextureSamplerAxisMode::Repeat;
}

inline std::uint32_t VulkanTextureSamplerTableIndex(
    VulkanTextureSamplerAxisMode s,
    VulkanTextureSamplerAxisMode t) noexcept
{
    return static_cast<std::uint32_t>(s) * 3u +
        static_cast<std::uint32_t>(t);
}

inline VulkanTextureCacheKey BuildVulkanTextureCacheKey(
    std::uint32_t texParam, std::uint32_t texPalette) noexcept
{
    VulkanTextureCacheKey key;
    key.TexParam = texParam & ~0xC00F0000u;
    key.Format = (key.TexParam >> 26) & 0x7u;
    if (key.Format == 5u)
        key.TexParam &= ~(1u << 29);
    key.TexPalette = key.Format == 7u ? 0u : texPalette;
    key.Width = TextureCacheWidth(key.TexParam);
    key.Height = TextureCacheHeight(key.TexParam);
    key.TextureAddress = (key.TexParam & 0xFFFFu) * 8u;
    if (key.Format == 7u)
    {
        key.PaletteAddress = 0;
    }
    else
    {
        std::uint32_t paletteAddress = texPalette * 16u;
        if (key.Format == 2u)
            paletteAddress >>= 1;
        key.PaletteAddress = paletteAddress & 0x1FFFFu;
    }
    key.Color0Transparent =
        key.Format >= 2u && key.Format <= 4u &&
        (key.TexParam & (1u << 29)) != 0 ? 1u : 0u;
    return key;
}

inline bool BuildVulkanTextureCachePlan(
    const std::vector<VulkanTextureCacheRequest>& requests,
    VulkanTextureCachePlan& plan,
    std::string* failureReason = nullptr)
{
    plan.Clear();
    std::vector<std::uint32_t> previousDecisionForEntry;
    std::vector<std::uint32_t> previousDecisionForDescriptor;
    std::uint64_t serial = 1;

    for (std::size_t requestIndex = 0; requestIndex < requests.size(); ++requestIndex)
    {
        const auto& request = requests[requestIndex];
        const VulkanTextureCacheKey key = BuildVulkanTextureCacheKey(
            request.TexParam, request.TexPalette);
        if (key.Format == 0u)
        {
            if (failureReason)
                *failureReason = "texture format zero is not cacheable";
            plan.Clear();
            return false;
        }

        std::uint32_t entryIndex = static_cast<std::uint32_t>(plan.Entries.size());
        for (std::uint32_t i = 0; i < plan.Entries.size(); ++i)
        {
            if (plan.Entries[i].Key == key)
            {
                entryIndex = i;
                break;
            }
        }

        bool cacheHit = false;
        bool uploadRequired = false;
        bool invalidated = false;
        if (entryIndex == plan.Entries.size())
        {
            VulkanTextureCacheEntry entry;
            entry.Key = key;
            entry.ContentGeneration = request.ContentGeneration;
            entry.LastUseSerial = serial;
            entry.UploadCount = 1;
            entry.ImageSlot = entryIndex;
            entry.Resident = true;
            plan.Entries.push_back(entry);
            previousDecisionForEntry.push_back(
                static_cast<std::uint32_t>(requestIndex));
            uploadRequired = true;
            ++plan.CacheMissCount;
            ++plan.UploadCount;
        }
        else
        {
            auto& entry = plan.Entries[entryIndex];
            const std::uint32_t previous = previousDecisionForEntry[entryIndex];
            if (entry.ContentGeneration != request.ContentGeneration)
            {
                entry.ContentGeneration = request.ContentGeneration;
                ++entry.UploadCount;
                uploadRequired = true;
                invalidated = true;
                ++plan.CacheMissCount;
                ++plan.UploadCount;
                ++plan.InvalidationCount;
            }
            else
            {
                cacheHit = true;
                ++plan.CacheHitCount;
                plan.UnchangedTextureNotReuploaded = true;
                if (requestIndex > static_cast<std::size_t>(previous + 1u))
                    plan.NonAdjacentReuseObserved = true;
            }
            entry.LastUseSerial = serial;
            previousDecisionForEntry[entryIndex] =
                static_cast<std::uint32_t>(requestIndex);
        }

        const auto s = DecodeVulkanTextureSamplerAxis(request.TexParam, false);
        const auto t = DecodeVulkanTextureSamplerAxis(request.TexParam, true);
        const std::uint32_t samplerIndex = VulkanTextureSamplerTableIndex(s, t);
        std::uint32_t descriptorSlot =
            static_cast<std::uint32_t>(plan.DescriptorSlots.size());
        for (std::uint32_t i = 0; i < plan.DescriptorSlots.size(); ++i)
        {
            const auto& slot = plan.DescriptorSlots[i];
            if (slot.EntryIndex == entryIndex && slot.SamplerIndex == samplerIndex)
            {
                descriptorSlot = i;
                if (previousDecisionForDescriptor[i] + 1u != requestIndex)
                    plan.DescriptorReuseObserved = true;
                previousDecisionForDescriptor[i] =
                    static_cast<std::uint32_t>(requestIndex);
                break;
            }
        }
        if (descriptorSlot == plan.DescriptorSlots.size())
        {
            plan.DescriptorSlots.push_back({entryIndex, samplerIndex});
            previousDecisionForDescriptor.push_back(
                static_cast<std::uint32_t>(requestIndex));
        }

        plan.Decisions.push_back({
            request.SourceOrder,
            entryIndex,
            descriptorSlot,
            samplerIndex,
            cacheHit,
            uploadRequired,
            invalidated,
        });
        ++serial;
    }

    plan.SourceOrderPreserved = plan.Decisions.size() == requests.size();
    for (std::size_t i = 0; i < requests.size() && plan.SourceOrderPreserved; ++i)
        plan.SourceOrderPreserved =
            plan.Decisions[i].SourceOrder == requests[i].SourceOrder;
    return true;
}


// MELONPRIME_VULKAN_TEXCACHE_DECODE_CONTRACT_V1
inline constexpr std::uint32_t kTextureDecodeContractVersion = 1;
inline constexpr std::uint32_t kTextureDecodeFormatCount = 7;
inline constexpr std::uint32_t kTextureDirtyPageSize = 512;

struct VulkanTextureMemorySpan
{
    std::uint32_t Address = 0;
    std::uint32_t Size = 0;
};

struct VulkanTextureDecodeFootprint
{
    VulkanTextureCacheKey Key{};
    std::array<VulkanTextureMemorySpan, 2> TextureSpans{};
    VulkanTextureMemorySpan PaletteSpan{};
    std::uint32_t TextureSpanCount = 0;
    std::uint32_t OutputTexelCount = 0;
    bool UsesPalette = false;
    bool UsesAuxiliaryTexture = false;
};

struct VulkanTextureMemoryView
{
    const std::uint8_t* Texture = nullptr;
    std::size_t TextureSize = 0;
    const std::uint8_t* Palette = nullptr;
    std::size_t PaletteSize = 0;
};

struct VulkanTextureDecodeHashes
{
    std::array<std::uint64_t, 2> Texture{{0, 0}};
    std::uint64_t Palette = 0;
    std::uint64_t Combined = 0;
};

struct VulkanTextureDirtyPageSet
{
    std::vector<std::uint64_t> TextureWords;
    std::vector<std::uint64_t> PaletteWords;
};

bool BuildVulkanTextureDecodeFootprint(
    const VulkanTextureCacheKey& key,
    VulkanTextureDecodeFootprint& footprint,
    std::string* failureReason = nullptr) noexcept;

bool DecodeVulkanTextureRgb6a5(
    const VulkanTextureDecodeFootprint& footprint,
    const VulkanTextureMemoryView& memory,
    std::vector<std::uint32_t>& output,
    std::string* failureReason = nullptr);

VulkanTextureDecodeHashes HashVulkanTextureDecodeInput(
    const VulkanTextureDecodeFootprint& footprint,
    const VulkanTextureMemoryView& memory) noexcept;

bool VulkanTextureDecodeTouchesDirtyPages(
    const VulkanTextureDecodeFootprint& footprint,
    const VulkanTextureMemoryView& memory,
    const VulkanTextureDirtyPageSet& dirty) noexcept;

void MarkVulkanTextureDirtyPage(
    std::vector<std::uint64_t>& words,
    std::uint32_t byteAddress,
    std::size_t memorySize) noexcept;


// MELONPRIME_VULKAN_TEXCACHE_UPLOAD_RING_CONTRACT_V1
inline constexpr std::uint32_t kTextureUploadRingContractVersion = 1;
inline constexpr std::uint32_t kTextureUploadRingDefaultCopyAlignment = 16;

struct VulkanTextureUploadRingConfig
{
    std::uint64_t Capacity = 0;
    std::uint64_t CopyAlignment = kTextureUploadRingDefaultCopyAlignment;
    std::uint64_t NonCoherentAtomSize = 1;
};

struct VulkanTextureUploadReservation
{
    std::uint64_t Offset = 0;
    std::uint64_t Size = 0;
    std::uint64_t PaddedSize = 0;
    std::uint64_t FlushOffset = 0;
    std::uint64_t FlushSize = 0;
    std::uint64_t SubmissionSerial = 0;
    bool Wrapped = false;
    bool ReusedRetiredSpace = false;
};

struct VulkanTextureUploadRingState
{
    VulkanTextureUploadRingConfig Config{};
    std::vector<VulkanTextureUploadReservation> InFlight;
    std::uint64_t Head = 0;
    std::uint64_t LastRetiredSerial = 0;
    std::uint64_t ReservationCount = 0;
    std::uint64_t WrapCount = 0;
    std::uint64_t ReuseCount = 0;
    std::uint64_t RejectedOverlapCount = 0;
    bool PersistentMappingRequired = true;
    bool NonCoherentFlushRequired = true;

    void Reset(const VulkanTextureUploadRingConfig& config) noexcept;
};

std::uint64_t AlignVulkanTextureUploadOffset(
    std::uint64_t value,
    std::uint64_t alignment) noexcept;

bool ReserveVulkanTextureUpload(
    VulkanTextureUploadRingState& state,
    std::uint64_t size,
    std::uint64_t submissionSerial,
    VulkanTextureUploadReservation& reservation,
    std::string* failureReason = nullptr) noexcept;

void RetireVulkanTextureUploads(
    VulkanTextureUploadRingState& state,
    std::uint64_t completedSerial) noexcept;

bool ValidateVulkanTextureUploadFlushRange(
    const VulkanTextureUploadRingState& state,
    const VulkanTextureUploadReservation& reservation) noexcept;



// MELONPRIME_VULKAN_PHASE8_COMPLETION_CONTRACT_V1
inline constexpr std::uint32_t kPhase8CompletionContractVersion = 1;
inline constexpr std::uint32_t kPhase8MinimumScale = 1;
inline constexpr std::uint32_t kPhase8MaximumScale = 16;

// Upload completion is expressed as a monotonically increasing signal value.
// Timeline semaphores are preferred, while the same state machine can be fed
// by fence/frame serial completion on Vulkan 1.1 implementations.
enum class VulkanTextureRetirementBackend : std::uint32_t
{
    TimelineSemaphore = 0,
    FenceSerial = 1,
};

struct VulkanTextureAsyncSubmission
{
    std::uint64_t Serial = 0;
    std::uint64_t SignalValue = 0;
    bool Completed = false;
};

struct VulkanTextureAsyncRetirementState
{
    VulkanTextureRetirementBackend Backend =
        VulkanTextureRetirementBackend::FenceSerial;
    std::vector<VulkanTextureAsyncSubmission> InFlight;
    std::uint64_t NextSerial = 1;
    std::uint64_t LastQueuedSignalValue = 0;
    std::uint64_t ObservedSignalValue = 0;
    std::uint64_t LastRetiredSerial = 0;
    std::uint64_t SubmissionCount = 0;
    std::uint64_t RetirementCount = 0;

    void Reset(bool timelineSemaphoreAvailable) noexcept;
};

bool QueueVulkanTextureAsyncSubmission(
    VulkanTextureAsyncRetirementState& state,
    std::uint64_t signalValue,
    VulkanTextureAsyncSubmission& submission,
    std::string* failureReason = nullptr) noexcept;

std::uint64_t ObserveVulkanTextureCompletion(
    VulkanTextureAsyncRetirementState& state,
    std::uint64_t completedSignalValue) noexcept;

void RetireVulkanTextureUploadsFromAsync(
    VulkanTextureUploadRingState& ring,
    const VulkanTextureAsyncRetirementState& retirement) noexcept;

struct VulkanTextureRuntimeEntry
{
    VulkanTextureCacheKey Key{};
    VulkanTextureDecodeFootprint Footprint{};
    VulkanTextureDecodeHashes Hashes{};
    std::uint64_t ContentGeneration = 0;
    std::uint64_t ImageGeneration = 0;
    std::uint64_t LastUseSerial = 0;
    std::uint32_t UploadCount = 0;
    bool Resident = false;
    bool CaptureBacked = false;
};

struct VulkanTextureRuntimeState
{
    std::vector<VulkanTextureRuntimeEntry> Entries;
    std::vector<VulkanTextureDescriptorSlot> DescriptorSlots;
    VulkanTextureUploadRingState UploadRing{};
    VulkanTextureAsyncRetirementState Retirement{};
    std::uint64_t NextImageGeneration = 1;
    std::uint64_t UseSerial = 0;
    std::uint64_t CacheHitCount = 0;
    std::uint64_t CacheMissCount = 0;
    std::uint64_t UploadCount = 0;
    std::uint64_t UploadBytes = 0;
    std::uint64_t InvalidationCount = 0;
    std::uint64_t DescriptorRewriteCount = 0;
    bool FirstFrameFullUpload = true;

    void Reset(
        const VulkanTextureUploadRingConfig& ringConfig,
        bool timelineSemaphoreAvailable) noexcept;
};

struct VulkanTextureRuntimeAcquireResult
{
    std::uint32_t EntryIndex = 0;
    std::uint32_t DescriptorSlot = 0;
    std::uint32_t SamplerIndex = 0;
    bool CacheHit = false;
    bool UploadRequired = false;
    bool DirtyPagesTouched = false;
    bool HashChanged = false;
    bool DescriptorRewriteRequired = false;
    VulkanTextureUploadReservation Reservation{};
    VulkanTextureAsyncSubmission Submission{};
    std::vector<std::uint32_t> DecodedRgb6a5;
};

bool AcquireVulkanTextureRuntime(
    VulkanTextureRuntimeState& state,
    const VulkanTextureCacheRequest& request,
    const VulkanTextureMemoryView& memory,
    const VulkanTextureDirtyPageSet& dirty,
    std::uint64_t completionSignalValue,
    VulkanTextureRuntimeAcquireResult& result,
    std::string* failureReason = nullptr);

// DS display-capture reference. Pixels use the native RGB5551 layout so the
// CPU synchronization path can write directly to LCDC VRAM.
enum class VulkanDisplayCaptureMode : std::uint32_t
{
    SourceA = 0,
    SourceB = 1,
    Blend = 2,
};

enum class VulkanDisplayCaptureSourceA : std::uint32_t
{
    Engine2D = 0,
    Engine3D = 1,
};

enum class VulkanDisplayCaptureSourceB : std::uint32_t
{
    Vram = 0,
    Fifo = 1,
};

struct VulkanDisplayCaptureConfig
{
    std::uint32_t Width = 256;
    std::uint32_t Height = 192;
    std::uint32_t YStart = 0;
    std::uint32_t YEnd = 192;
    std::uint32_t DestinationBank = 0;
    std::uint32_t DestinationOffset = 0;
    std::uint32_t DestinationBufferHeight = 256;
    std::uint32_t SourceBOffset = 0;
    std::uint32_t Eva = 16;
    std::uint32_t Evb = 0;
    VulkanDisplayCaptureMode Mode = VulkanDisplayCaptureMode::SourceA;
    VulkanDisplayCaptureSourceA SourceA = VulkanDisplayCaptureSourceA::Engine2D;
    VulkanDisplayCaptureSourceB SourceB = VulkanDisplayCaptureSourceB::Vram;
    std::array<std::int16_t, 192> SourceAXOffset{};
};

bool ValidateVulkanDisplayCaptureConfig(
    const VulkanDisplayCaptureConfig& config,
    std::string* failureReason = nullptr) noexcept;

std::uint16_t BlendVulkanDisplayCapturePixel(
    std::uint16_t sourceA,
    std::uint16_t sourceB,
    std::uint32_t eva,
    std::uint32_t evb) noexcept;

bool ExecuteVulkanDisplayCaptureReference(
    const VulkanDisplayCaptureConfig& config,
    const std::vector<std::uint16_t>& sourceA,
    const std::vector<std::uint16_t>& sourceB,
    std::vector<std::uint16_t>& destination,
    std::string* failureReason = nullptr);

struct VulkanCaptureSlot
{
    std::uint32_t Bank = 0;
    std::uint32_t Offset = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t BufferHeight = 0;
    std::uint64_t Generation = 0;
    bool Complete = false;
    bool GpuResident = false;
    bool CpuSynchronized = false;
    std::vector<std::uint16_t> Pixels;
};

struct VulkanCaptureRegistry
{
    std::vector<VulkanCaptureSlot> Slots;
    std::uint64_t NextGeneration = 1;
    std::uint64_t CaptureCount = 0;
    std::uint64_t GpuReuseCount = 0;
    std::uint64_t CpuSyncCount = 0;

    void Reset() noexcept;
};

VulkanCaptureSlot& StoreVulkanCapture(
    VulkanCaptureRegistry& registry,
    const VulkanDisplayCaptureConfig& config,
    std::vector<std::uint16_t> pixels,
    bool complete = true);

const VulkanCaptureSlot* ResolveVulkanCaptureTexture(
    VulkanCaptureRegistry& registry,
    std::uint32_t bank,
    std::uint32_t offset,
    std::uint32_t width) noexcept;

bool SyncVulkanCaptureToCpuVram(
    VulkanCaptureRegistry& registry,
    std::uint32_t bank,
    std::uint32_t offset,
    std::uint32_t width,
    std::vector<std::uint8_t>& vram,
    std::vector<std::uint64_t>& dirtyWords,
    std::string* failureReason = nullptr);

struct VulkanScaleResourcePlan
{
    std::uint32_t OldScale = 1;
    std::uint32_t NewScale = 1;
    std::uint64_t OldGeneration = 0;
    std::uint64_t NewGeneration = 0;
    std::uint64_t RequiredBytes = 0;
    std::uint64_t MemoryBudgetBytes = 0;
    bool PresenterLeaseOutstanding = false;
    bool DeferOldResourceDestruction = false;
    bool ReusePipelines = true;
    bool Valid = false;
    std::string FailureReason;
};

std::uint64_t EstimateVulkanPhase8ScaleBytes(std::uint32_t scale) noexcept;

VulkanScaleResourcePlan BuildVulkanScaleResourcePlan(
    std::uint32_t oldScale,
    std::uint32_t newScale,
    std::uint64_t oldGeneration,
    std::uint64_t memoryBudgetBytes,
    bool presenterLeaseOutstanding) noexcept;

struct VulkanPhase8LifecycleState
{
    VulkanTextureRuntimeState TextureRuntime{};
    VulkanCaptureRegistry CaptureRegistry{};
    std::uint32_t Scale = 1;
    std::uint64_t OutputGeneration = 1;
    std::uint64_t DeferredResourceBytes = 0;
    std::uint64_t ResetCount = 0;
    std::uint64_t SavestateCount = 0;
    std::uint64_t RendererSwitchCount = 0;
    std::uint64_t ScaleChangeCount = 0;
    bool GpuWorkInFlight = false;
    bool PreSavestateSynchronized = false;
    bool FirstFrameFullUpload = true;

    void Initialize(
        const VulkanTextureUploadRingConfig& ringConfig,
        bool timelineSemaphoreAvailable) noexcept;
};

bool PreSavestateVulkanPhase8(
    VulkanPhase8LifecycleState& state,
    std::array<std::vector<std::uint8_t>, 4>& vramBanks,
    std::array<std::vector<std::uint64_t>, 4>& dirtyWords,
    std::string* failureReason = nullptr);

void PostSavestateVulkanPhase8(
    VulkanPhase8LifecycleState& state,
    const VulkanTextureUploadRingConfig& ringConfig,
    bool timelineSemaphoreAvailable) noexcept;

void ResetVulkanPhase8(
    VulkanPhase8LifecycleState& state,
    const VulkanTextureUploadRingConfig& ringConfig,
    bool timelineSemaphoreAvailable) noexcept;

bool FlushVulkanPhase8ForRendererSwitch(
    VulkanPhase8LifecycleState& state,
    std::array<std::vector<std::uint8_t>, 4>& vramBanks,
    std::array<std::vector<std::uint64_t>, 4>& dirtyWords,
    const VulkanTextureUploadRingConfig& ringConfig,
    bool timelineSemaphoreAvailable,
    std::string* failureReason = nullptr);

bool ApplyVulkanPhase8ScaleChange(
    VulkanPhase8LifecycleState& state,
    const VulkanScaleResourcePlan& plan) noexcept;

struct VulkanPhase8ExitAudit
{
    bool AllTextureFormats = false;
    bool RepeatMirrorClamp = false;
    bool ClearBitmap = false;
    bool DisplayCapture = false;
    bool CaptureTextureReuse = false;
    bool Savestate = false;
    bool Reset = false;
    bool RendererSwitch = false;
    bool ScaleLiveChange = false;
    bool AsyncRetirement = false;
    bool CpuReadbackSynchronization = false;
    bool MemoryBudget = false;
    bool ResourceLifetime = false;

    bool Passed() const noexcept;
};

} // namespace melonDS::Vulkan
