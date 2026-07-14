#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "GPU_VulkanOutputRing.h is owned by the complete MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_PHASE10_OUTPUT_RING_CONTRACT_V1

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace melonDS::Vulkan
{

inline constexpr std::uint32_t kPhase10OutputRingContractVersion = 1;
inline constexpr std::uint32_t kPhase10OutputSlotCount = 3;
inline constexpr std::uint32_t kPhase10MaxPresenterLeasesPerSlot = 8;
inline constexpr std::uint32_t kPhase10DiagnosticTimeoutMilliseconds = 250;

struct VulkanPhase10OutputDescriptor
{
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    VkExtent2D Extent{};
    VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::uint32_t LayerCount = 0;
    std::uint32_t EngineALayer = 0;
    std::uint32_t QueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    std::uint64_t FrameSerial = 0;
    std::uint64_t Generation = 0;
    VkSemaphore ProducerTimeline = VK_NULL_HANDLE;
    std::uint64_t ProducerValue = 0;

    [[nodiscard]] bool ValidForPresenter() const noexcept;
};

struct VulkanPhase10OutputRingStats
{
    std::uint64_t BeginProduceCount = 0;
    std::uint64_t PublishCount = 0;
    std::uint64_t AcquireCount = 0;
    std::uint64_t ReleaseCount = 0;
    std::uint64_t ReuseCount = 0;
    std::uint64_t RejectedReuseCount = 0;
    std::uint64_t FrameDropCount = 0;
    std::uint64_t GenerationInvalidationCount = 0;
    std::uint64_t MultiWindowAcquireCount = 0;
    std::uint64_t LeaseBeforeProducerCompleteCount = 0;
};

struct VulkanPhase10OutputSlotSnapshot
{
    bool RendererInFlight = false;
    bool Published = false;
    bool ProducerComplete = false;
    bool StaleGeneration = false;
    std::uint32_t PresenterRefs = 0;
    std::uint64_t FrameSerial = 0;
    std::uint64_t Generation = 0;
    std::uint64_t ProducerValue = 0;
};

class VulkanPhase10OutputRing;

class VulkanPhase10OutputLease
{
public:
    VulkanPhase10OutputLease() noexcept = default;
    VulkanPhase10OutputLease(const VulkanPhase10OutputLease&) = delete;
    VulkanPhase10OutputLease& operator=(const VulkanPhase10OutputLease&) = delete;
    VulkanPhase10OutputLease(VulkanPhase10OutputLease&& other) noexcept;
    VulkanPhase10OutputLease& operator=(VulkanPhase10OutputLease&& other) noexcept;
    ~VulkanPhase10OutputLease();

    [[nodiscard]] bool Valid() const noexcept { return Owner != nullptr; }
    [[nodiscard]] const VulkanPhase10OutputDescriptor* Descriptor() const noexcept;
    [[nodiscard]] std::uint32_t SlotIndex() const noexcept { return Slot; }
    void ReleaseNow() noexcept;

private:
    friend class VulkanPhase10OutputRing;
    VulkanPhase10OutputLease(
        VulkanPhase10OutputRing* owner,
        std::uint32_t slot,
        std::uint32_t cookie,
        std::uint64_t generation) noexcept;

    VulkanPhase10OutputRing* Owner = nullptr;
    std::uint32_t Slot = 0;
    std::uint32_t Cookie = 0;
    std::uint64_t Generation = 0;
};

class VulkanPhase10OutputRing
{
public:
    VulkanPhase10OutputRing() noexcept;
    VulkanPhase10OutputRing(const VulkanPhase10OutputRing&) = delete;
    VulkanPhase10OutputRing& operator=(const VulkanPhase10OutputRing&) = delete;
    ~VulkanPhase10OutputRing();

    void Reset(std::uint64_t generation) noexcept;

    [[nodiscard]] std::optional<std::uint32_t> BeginProduce(
        std::uint64_t frameSerial,
        std::uint64_t generation,
        std::uint64_t producerValue) noexcept;

    bool Publish(
        std::uint32_t slot,
        const VulkanPhase10OutputDescriptor& descriptor,
        std::string* failureReason = nullptr) noexcept;

    void MarkProducerComplete(std::uint64_t completedValue) noexcept;

    [[nodiscard]] std::optional<VulkanPhase10OutputLease> AcquireLatest(
        std::uint64_t generation,
        std::string* failureReason = nullptr) noexcept;

    void InvalidateGeneration(std::uint64_t generation) noexcept;

    [[nodiscard]] std::optional<std::uint64_t> OldestProducerWaitValue() const noexcept;
    [[nodiscard]] VulkanPhase10OutputRingStats Stats() const noexcept;
    [[nodiscard]] VulkanPhase10OutputSlotSnapshot SlotSnapshot(
        std::uint32_t slot) const noexcept;
    [[nodiscard]] std::uint64_t CurrentGeneration() const noexcept;
    [[nodiscard]] bool CanDestroy() const noexcept;

private:
    friend class VulkanPhase10OutputLease;

    struct LeaseCookie
    {
        bool Active = false;
        std::uint64_t Generation = 0;
    };

    struct SlotState
    {
        VulkanPhase10OutputDescriptor Descriptor;
        bool RendererInFlight = false;
        bool Published = false;
        bool ProducerComplete = false;
        bool StaleGeneration = false;
        std::uint32_t PresenterRefs = 0;
        std::array<LeaseCookie, kPhase10MaxPresenterLeasesPerSlot> Cookies{};
    };

    void ReleaseLease(
        std::uint32_t slot,
        std::uint32_t cookie,
        std::uint64_t generation) noexcept;
    const VulkanPhase10OutputDescriptor* DescriptorForLease(
        std::uint32_t slot,
        std::uint32_t cookie,
        std::uint64_t generation) const noexcept;
    void ClearSlotIfRetired(SlotState& slot) noexcept;

    mutable std::mutex Mutex;
    std::array<SlotState, kPhase10OutputSlotCount> Slots{};
    std::uint64_t Generation = 1;
    std::uint64_t CompletedProducerValue = 0;
    VulkanPhase10OutputRingStats Counters;
};

struct VulkanPhase10ExitAudit
{
    bool FixedThreeSlotRing = false;
    bool ProducerTimelineWait = false;
    bool ShaderReadLayout = false;
    bool SameQueueFamilyFastPath = false;
    bool MultiWindowLease = false;
    bool ReuseRejectedWhileReferenced = false;
    bool ReleaseAfterCompletion = false;
    bool ReuseAfterAllReferencesReleased = false;
    bool GenerationInvalidation = false;
    bool BoundedFrameDrop = false;
    bool DiagnosticTimeoutPolicy = false;
    bool NoPresenterCpuCopy = false;
    bool ResourceDestroySafe = false;

    [[nodiscard]] bool Passed() const noexcept;
};

} // namespace melonDS::Vulkan
