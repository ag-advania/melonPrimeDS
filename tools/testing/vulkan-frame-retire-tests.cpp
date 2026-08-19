/*
    Model and API-level fake-dispatch tests for Vulkan resource retirement.

    The frame policy is pure, while DeferredDestroyQueue is the production
    queue. The forced OOM sequence verifies that a previous-frame texcache
    allocation is destroyed before the one bounded materialization retry.
*/

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <type_traits>
#include <vector>

#include "GPU3D_Texcache.h"
#include "VulkanSync.h"

namespace melonDS::Platform
{
// The standalone model links the production synchronization object from core
// but not the Qt frontend's logger implementation.
void Log(LogLevel, const char*, ...)
{
}
} // namespace melonDS::Platform

namespace melonDS::Vk
{
struct VulkanFrameRingTestAccess
{
    static void SetState(
        FrameRing& ring,
        const VulkanResourceRetireFrameState& state,
        u64 slotFrame) noexcept
    {
        ring.Device = nullptr;
        ring.CoreResourcesReady = false;
        ring.CompletedFrame = state.CompletedFrame;
        ring.HasSubmittedFrame = state.HasSubmittedFrame;
        ring.CurrentIndex = 0;
        ring.LastSubmittedIndex = 0;
        ring.LastSubmittedFrameNumber = state.HasSubmittedFrame
            ? state.LastSubmittedFrame : 0;
        ring.AbsoluteFrame = state.Recording
            ? state.CurrentRecordingFrame
            : state.HasSubmittedFrame ? state.LastSubmittedFrame + 1 : 1;
        ring.Frames.clear();
        ring.Frames.resize(1);
        ring.Frames[0].SubmittedFrame = slotFrame;
        ring.Frames[0].Recording = state.Recording;
    }
};
} // namespace melonDS::Vk

namespace
{

using namespace melonDS;
using namespace melonDS::Vk;

template <typename Handle>
Handle FakeHandle(uintptr_t value)
{
    if constexpr (std::is_pointer_v<Handle>)
        return reinterpret_cast<Handle>(value);
    else
        return static_cast<Handle>(value);
}

struct FakeVulkan
{
    static FakeVulkan* Current;

    VkDevice Device = FakeHandle<VkDevice>(0x101);
    DeviceDispatch Dispatch{};
    std::deque<VkResult> AllocateResults;
    std::vector<std::string> Events;

    FakeVulkan()
    {
        Current = this;
        Dispatch.AllocateMemory = &AllocateMemory;
        Dispatch.DestroyImageView = &DestroyImageView;
        Dispatch.DestroyImage = &DestroyImage;
        Dispatch.FreeMemory = &FreeMemory;
    }

    ~FakeVulkan()
    {
        if (Current == this)
            Current = nullptr;
    }

    static VkResult Pop(std::deque<VkResult>& results)
    {
        if (results.empty())
            return VK_SUCCESS;
        const VkResult result = results.front();
        results.pop_front();
        return result;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL AllocateMemory(
        VkDevice,
        const VkMemoryAllocateInfo*,
        const VkAllocationCallbacks*,
        VkDeviceMemory* memory)
    {
        const VkResult result = Pop(Current->AllocateResults);
        Current->Events.emplace_back(
            result == VK_ERROR_OUT_OF_DEVICE_MEMORY
                ? "allocate:oom"
                : result == VK_SUCCESS ? "allocate:success" : "allocate:fatal");
        if (result == VK_SUCCESS && memory)
            *memory = FakeHandle<VkDeviceMemory>(0x404);
        return result;
    }

    static VKAPI_ATTR void VKAPI_CALL DestroyImageView(
        VkDevice, VkImageView, const VkAllocationCallbacks*)
    {
        Current->Events.emplace_back("destroy:image_view");
    }

    static VKAPI_ATTR void VKAPI_CALL DestroyImage(
        VkDevice, VkImage, const VkAllocationCallbacks*)
    {
        Current->Events.emplace_back("destroy:image");
    }

    static VKAPI_ATTR void VKAPI_CALL FreeMemory(
        VkDevice, VkDeviceMemory, const VkAllocationCallbacks*)
    {
        Current->Events.emplace_back("destroy:memory");
    }
};

FakeVulkan* FakeVulkan::Current = nullptr;

bool Require(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "vulkan-frame-retire-tests: FAIL: %s\n", message);
    return condition;
}

bool TestRetireFramePolicy()
{
    bool ok = true;

    // Case 1: no submission.
    VulkanResourceRetireFrameState state{};
    ok &= Require(VulkanResourceRetireFrame(state) == 0,
        "no submission must retire at completed frame zero");

    // Case 2: frame 10 submitted, CPU is between submissions.
    state.CompletedFrame = 9;
    state.LastSubmittedFrame = 10;
    state.HasSubmittedFrame = true;
    ok &= Require(VulkanResourceRetireFrame(state) == 10,
        "non-recording invalidation must use the last submitted frame");

    // Case 3: frame 11 recording.
    state.CurrentRecordingFrame = 11;
    state.Recording = true;
    ok &= Require(VulkanResourceRetireFrame(state) == 11,
        "recording invalidation must use the current recording frame");

    // Case 4: previous frame completion.
    state.Recording = false;
    state.CompletedFrame = 10;
    ok &= Require(VulkanResourceRetireFrame(state) == 10,
        "completed previous frame must be collectible at frame ten");

    return ok;
}

bool TestProductionFrameRingMapping()
{
    bool ok = true;
    FrameRing ring;

    // Case A: the first frame is recording and no successful submit exists.
    // The test access seeds only one-slot lifecycle state; the assertions call
    // production FrameRing getters and therefore exercise its extraction path.
    VulkanFrameRingTestAccess::SetState(ring, {
        0, 0, 1, false, true}, 1);
    ok &= Require(ring.GetResourceRetireFrame() == 1
            && ring.GetLastSubmittedFrameNumber() == 0
            && ring.GetCurrentRecordingFrameNumber() == 1,
        "production FrameRing first recording mapping must use frame one");

    // Case B: a one-slot ring is reused while frame 11 is recording, so the
    // slot's SubmittedFrame has already overwritten the successful frame 10.
    VulkanFrameRingTestAccess::SetState(ring, {
        9, 10, 11, true, true}, 11);
    ok &= Require(ring.GetResourceRetireFrame() == 11
            && ring.GetLastSubmittedFrameNumber() == 10
            && ring.GetCurrentRecordingFrameNumber() == 11,
        "production FrameRing one-slot recording mapping must preserve last submit");

    // Case C: QueueSubmit for frame 11 failed after the same-slot recording;
    // the slot is not pending and the last successful submit is still 10.
    VulkanFrameRingTestAccess::SetState(ring, {
        10, 10, 0, true, false}, 11);
    ok &= Require(ring.GetResourceRetireFrame() == 10
            && ring.GetLastSubmittedFrameNumber() == 10
            && ring.GetCurrentRecordingFrameNumber() == 0,
        "production FrameRing submit failure mapping must preserve last submit");

    return ok;
}

bool TestForcedOomRetryReclaimsBeforeAllocation()
{
    FakeVulkan fake;
    DeferredDestroyQueue queue;
    queue.Init(fake.Dispatch, fake.Device);

    // Frame 10 is the last submission that could reference these old objects.
    const u64 retireFrame = VulkanResourceRetireFrame({
        9, 10, 0, true, false});
    queue.Enqueue(
        DeferredObject::ImageView,
        FakeHandle<VkImageView>(0x201), retireFrame);
    queue.Enqueue(
        DeferredObject::Image,
        FakeHandle<VkImage>(0x202), retireFrame);
    queue.Enqueue(
        DeferredObject::DeviceMemory,
        FakeHandle<VkDeviceMemory>(0x203), retireFrame);

    fake.AllocateResults.push_back(VK_ERROR_OUT_OF_DEVICE_MEMORY);
    fake.AllocateResults.push_back(VK_SUCCESS);

    VkMemoryAllocateInfo allocationInfo{};
    int materializeCalls = 0;
    bool RuntimeFailed = false;
    const auto materialize = [&]() {
        materializeCalls++;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        const VkResult result = fake.Dispatch.AllocateMemory(
            fake.Device, &allocationInfo, nullptr, &memory);
        if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY)
            return TextureMaterializeResult::RetryAfterRetire;
        if (result == VK_SUCCESS)
            return TextureMaterializeResult::Ready;
        RuntimeFailed = true;
        return TextureMaterializeResult::Fatal;
    };

    const TextureMaterializeResult first = materialize();
    bool ok = true;
    ok &= Require(first == TextureMaterializeResult::RetryAfterRetire,
        "the first allocation must force the retryable OOM result");
    ok &= Require(materializeCalls == 1 && queue.GetPendingCount() == 3,
        "the OOM result must leave one bounded retry and old objects pending");

    // BeginFrame() has waited for frame 10 and collects before retrying.
    queue.Collect(10);
    const TextureMaterializeResult second = materialize();
    ok &= Require(second == TextureMaterializeResult::Ready,
        "the one retry must succeed after retirement");
    ok &= Require(materializeCalls == 2,
        "materialization must be attempted exactly twice");
    ok &= Require(!RuntimeFailed,
        "a successful retry must not latch RuntimeFailed");
    ok &= Require(queue.GetPendingCount() == 0,
        "all old texcache objects must be collected before the retry");

    const auto successAllocation = std::find(
        fake.Events.begin(), fake.Events.end(), "allocate:success");
    const auto imageViewDestroy = std::find(
        fake.Events.begin(), fake.Events.end(), "destroy:image_view");
    const auto imageDestroy = std::find(
        fake.Events.begin(), fake.Events.end(), "destroy:image");
    const auto memoryDestroy = std::find(
        fake.Events.begin(), fake.Events.end(), "destroy:memory");
    ok &= Require(successAllocation != fake.Events.end()
            && imageViewDestroy != fake.Events.end()
            && imageDestroy != fake.Events.end()
            && memoryDestroy != fake.Events.end()
            && imageViewDestroy < successAllocation
            && imageDestroy < successAllocation
            && memoryDestroy < successAllocation,
        "old image, view, and memory must be destroyed before new allocation");

    return ok;
}

} // namespace

int main()
{
    const bool ok = TestRetireFramePolicy()
        && TestProductionFrameRingMapping()
        && TestForcedOomRetryReclaimsBeforeAllocation();
    if (!ok)
        return 1;
    std::puts("vulkan-frame-retire-tests: PASS");
    return 0;
}
