#include "GPU_VulkanOutputRing.h"

#include <algorithm>
#include <limits>

namespace melonDS::Vulkan
{

bool VulkanPhase10OutputDescriptor::ValidForPresenter() const noexcept
{
    return Image != VK_NULL_HANDLE &&
        View != VK_NULL_HANDLE &&
        Format != VK_FORMAT_UNDEFINED &&
        Extent.width != 0 && Extent.height != 0 &&
        LayerCount >= 1 &&
        Layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
        FrameSerial != 0 && Generation != 0;
}

VulkanPhase10OutputLease::VulkanPhase10OutputLease(
    VulkanPhase10OutputRing* owner,
    std::uint32_t slot,
    std::uint32_t cookie,
    std::uint64_t generation) noexcept
    : Owner(owner), Slot(slot), Cookie(cookie), Generation(generation)
{
}

VulkanPhase10OutputLease::VulkanPhase10OutputLease(
    VulkanPhase10OutputLease&& other) noexcept
    : Owner(other.Owner),
      Slot(other.Slot),
      Cookie(other.Cookie),
      Generation(other.Generation)
{
    other.Owner = nullptr;
}

VulkanPhase10OutputLease& VulkanPhase10OutputLease::operator=(
    VulkanPhase10OutputLease&& other) noexcept
{
    if (this != &other)
    {
        ReleaseNow();
        Owner = other.Owner;
        Slot = other.Slot;
        Cookie = other.Cookie;
        Generation = other.Generation;
        other.Owner = nullptr;
    }
    return *this;
}

VulkanPhase10OutputLease::~VulkanPhase10OutputLease()
{
    ReleaseNow();
}

const VulkanPhase10OutputDescriptor* VulkanPhase10OutputLease::Descriptor() const noexcept
{
    return Owner ? Owner->DescriptorForLease(Slot, Cookie, Generation) : nullptr;
}

void VulkanPhase10OutputLease::ReleaseNow() noexcept
{
    VulkanPhase10OutputRing* owner = Owner;
    Owner = nullptr;
    if (owner)
        owner->ReleaseLease(Slot, Cookie, Generation);
}

VulkanPhase10OutputRing::VulkanPhase10OutputRing() noexcept = default;

VulkanPhase10OutputRing::~VulkanPhase10OutputRing() = default;

void VulkanPhase10OutputRing::Reset(std::uint64_t generation) noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    Generation = generation ? generation : 1;
    CompletedProducerValue = 0;
    Counters = {};
    for (auto& slot : Slots)
        slot = {};
}

std::optional<std::uint32_t> VulkanPhase10OutputRing::BeginProduce(
    std::uint64_t frameSerial,
    std::uint64_t generation,
    std::uint64_t producerValue) noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    ++Counters.BeginProduceCount;
    if (!frameSerial || !generation || generation != Generation)
    {
        ++Counters.RejectedReuseCount;
        return std::nullopt;
    }

    std::optional<std::uint32_t> selected;
    std::uint64_t oldestFrame = std::numeric_limits<std::uint64_t>::max();
    for (std::uint32_t index = 0; index < Slots.size(); ++index)
    {
        auto& slot = Slots[index];
        const bool unused = !slot.RendererInFlight && !slot.Published &&
            slot.PresenterRefs == 0;
        const bool reusable = !slot.RendererInFlight && slot.Published &&
            slot.ProducerComplete && slot.PresenterRefs == 0;
        if (!unused && !reusable)
            continue;
        const std::uint64_t candidateFrame = unused ? 0 : slot.Descriptor.FrameSerial;
        if (!selected || candidateFrame < oldestFrame)
        {
            selected = index;
            oldestFrame = candidateFrame;
            if (unused)
                break;
        }
    }

    if (!selected)
    {
        ++Counters.RejectedReuseCount;
        ++Counters.FrameDropCount;
        return std::nullopt;
    }

    auto& slot = Slots[*selected];
    if (slot.Published)
        ++Counters.ReuseCount;
    slot.Descriptor = {};
    slot.Descriptor.FrameSerial = frameSerial;
    slot.Descriptor.Generation = generation;
    slot.Descriptor.ProducerValue = producerValue;
    slot.RendererInFlight = true;
    slot.Published = false;
    slot.ProducerComplete = producerValue <= CompletedProducerValue;
    slot.StaleGeneration = false;
    return selected;
}

bool VulkanPhase10OutputRing::Publish(
    std::uint32_t slotIndex,
    const VulkanPhase10OutputDescriptor& descriptor,
    std::string* failureReason) noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (slotIndex >= Slots.size())
    {
        if (failureReason) *failureReason = "slot index out of range";
        return false;
    }
    auto& slot = Slots[slotIndex];
    if (!slot.RendererInFlight)
    {
        if (failureReason) *failureReason = "slot is not renderer-in-flight";
        return false;
    }
    if (!descriptor.ValidForPresenter())
    {
        if (failureReason) *failureReason = "descriptor is not presenter-sampleable";
        return false;
    }
    if (descriptor.FrameSerial != slot.Descriptor.FrameSerial ||
        descriptor.Generation != slot.Descriptor.Generation ||
        descriptor.Generation != Generation)
    {
        if (failureReason) *failureReason = "frame or generation mismatch";
        return false;
    }
    slot.Descriptor = descriptor;
    slot.RendererInFlight = false;
    slot.Published = true;
    slot.ProducerComplete = descriptor.ProducerValue <= CompletedProducerValue;
    ++Counters.PublishCount;
    return true;
}

void VulkanPhase10OutputRing::MarkProducerComplete(std::uint64_t completedValue) noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    CompletedProducerValue = std::max(CompletedProducerValue, completedValue);
    for (auto& slot : Slots)
    {
        if (slot.Published && slot.Descriptor.ProducerValue <= CompletedProducerValue)
            slot.ProducerComplete = true;
        ClearSlotIfRetired(slot);
    }
}

std::optional<VulkanPhase10OutputLease> VulkanPhase10OutputRing::AcquireLatest(
    std::uint64_t generation,
    std::string* failureReason) noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    std::optional<std::uint32_t> selected;
    std::uint64_t newestFrame = 0;
    for (std::uint32_t index = 0; index < Slots.size(); ++index)
    {
        const auto& slot = Slots[index];
        if (!slot.Published || slot.StaleGeneration ||
            slot.Descriptor.Generation != generation)
        {
            continue;
        }
        if (!selected || slot.Descriptor.FrameSerial > newestFrame)
        {
            selected = index;
            newestFrame = slot.Descriptor.FrameSerial;
        }
    }
    if (!selected)
    {
        if (failureReason) *failureReason = "no published output for generation";
        return std::nullopt;
    }

    auto& slot = Slots[*selected];
    std::optional<std::uint32_t> cookie;
    for (std::uint32_t index = 0; index < slot.Cookies.size(); ++index)
    {
        if (!slot.Cookies[index].Active)
        {
            cookie = index;
            break;
        }
    }
    if (!cookie)
    {
        if (failureReason) *failureReason = "presenter lease capacity exhausted";
        return std::nullopt;
    }

    slot.Cookies[*cookie].Active = true;
    slot.Cookies[*cookie].Generation = generation;
    ++slot.PresenterRefs;
    ++Counters.AcquireCount;
    if (slot.PresenterRefs > 1)
        ++Counters.MultiWindowAcquireCount;
    if (!slot.ProducerComplete)
        ++Counters.LeaseBeforeProducerCompleteCount;
    return VulkanPhase10OutputLease(this, *selected, *cookie, generation);
}

void VulkanPhase10OutputRing::InvalidateGeneration(std::uint64_t generation) noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    Generation = generation ? generation : Generation + 1;
    if (Generation == 0)
        Generation = 1;
    ++Counters.GenerationInvalidationCount;
    for (auto& slot : Slots)
    {
        if (slot.Published || slot.RendererInFlight || slot.PresenterRefs)
            slot.StaleGeneration = true;
        ClearSlotIfRetired(slot);
    }
}

std::optional<std::uint64_t> VulkanPhase10OutputRing::OldestProducerWaitValue() const noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    std::optional<std::uint64_t> value;
    for (const auto& slot : Slots)
    {
        if ((!slot.Published && !slot.RendererInFlight) || slot.ProducerComplete)
            continue;
        if (!value || slot.Descriptor.ProducerValue < *value)
            value = slot.Descriptor.ProducerValue;
    }
    return value;
}

VulkanPhase10OutputRingStats VulkanPhase10OutputRing::Stats() const noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    return Counters;
}

VulkanPhase10OutputSlotSnapshot VulkanPhase10OutputRing::SlotSnapshot(
    std::uint32_t slotIndex) const noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    VulkanPhase10OutputSlotSnapshot snapshot;
    if (slotIndex >= Slots.size())
        return snapshot;
    const auto& slot = Slots[slotIndex];
    snapshot.RendererInFlight = slot.RendererInFlight;
    snapshot.Published = slot.Published;
    snapshot.ProducerComplete = slot.ProducerComplete;
    snapshot.StaleGeneration = slot.StaleGeneration;
    snapshot.PresenterRefs = slot.PresenterRefs;
    snapshot.FrameSerial = slot.Descriptor.FrameSerial;
    snapshot.Generation = slot.Descriptor.Generation;
    snapshot.ProducerValue = slot.Descriptor.ProducerValue;
    return snapshot;
}

std::uint64_t VulkanPhase10OutputRing::CurrentGeneration() const noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    return Generation;
}

bool VulkanPhase10OutputRing::CanDestroy() const noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    return std::all_of(Slots.begin(), Slots.end(), [](const auto& slot) {
        return !slot.RendererInFlight && slot.PresenterRefs == 0;
    });
}

void VulkanPhase10OutputRing::ReleaseLease(
    std::uint32_t slotIndex,
    std::uint32_t cookieIndex,
    std::uint64_t generation) noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (slotIndex >= Slots.size())
        return;
    auto& slot = Slots[slotIndex];
    if (cookieIndex >= slot.Cookies.size())
        return;
    auto& cookie = slot.Cookies[cookieIndex];
    if (!cookie.Active || cookie.Generation != generation)
        return;
    cookie = {};
    if (slot.PresenterRefs)
        --slot.PresenterRefs;
    ++Counters.ReleaseCount;
    ClearSlotIfRetired(slot);
}

const VulkanPhase10OutputDescriptor* VulkanPhase10OutputRing::DescriptorForLease(
    std::uint32_t slotIndex,
    std::uint32_t cookieIndex,
    std::uint64_t generation) const noexcept
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (slotIndex >= Slots.size())
        return nullptr;
    const auto& slot = Slots[slotIndex];
    if (cookieIndex >= slot.Cookies.size())
        return nullptr;
    const auto& cookie = slot.Cookies[cookieIndex];
    if (!cookie.Active || cookie.Generation != generation || !slot.Published)
        return nullptr;
    return &slot.Descriptor;
}

void VulkanPhase10OutputRing::ClearSlotIfRetired(SlotState& slot) noexcept
{
    if (slot.StaleGeneration && !slot.RendererInFlight && slot.PresenterRefs == 0)
        slot = {};
}

bool VulkanPhase10ExitAudit::Passed() const noexcept
{
    return FixedThreeSlotRing && ProducerTimelineWait && ShaderReadLayout &&
        SameQueueFamilyFastPath && MultiWindowLease &&
        ReuseRejectedWhileReferenced && ReleaseAfterCompletion &&
        ReuseAfterAllReferencesReleased && GenerationInvalidation &&
        BoundedFrameDrop && DiagnosticTimeoutPolicy && NoPresenterCpuCopy &&
        ResourceDestroySafe;
}

} // namespace melonDS::Vulkan
