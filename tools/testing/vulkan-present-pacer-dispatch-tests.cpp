/*
    API-level fake-dispatch tests for the production Vulkan present pacer.

    The fake owns no Vulkan objects.  It records every entry point call and
    returns scripted results, while the real VulkanPresentPacer.cpp performs
    all lifecycle classification and state transitions.
*/

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <type_traits>
#include <vector>

#include "VulkanPresentPacer.h"
#include "Platform.h"

namespace melonDS::Platform
{
// The standalone vector does not link the Qt frontend's logger.
void Log(LogLevel, const char*, ...)
{
}
} // namespace melonDS::Platform

namespace
{
using namespace melonDS;

constexpr u64 FrameIntervalNs = 16'666'667;
template <typename Handle>
Handle FakeHandle(uintptr_t value)
{
    if constexpr (std::is_pointer_v<Handle>)
        return reinterpret_cast<Handle>(value);
    else
        return static_cast<Handle>(value);
}

const VkSurfaceKHR Surface = FakeHandle<VkSurfaceKHR>(0x101);
const VkSwapchainKHR Swapchain = FakeHandle<VkSwapchainKHR>(0x202);
const VkSwapchainKHR RecreatedSwapchain = FakeHandle<VkSwapchainKHR>(0x303);

struct FakeVulkan
{
    static FakeVulkan* Current;

    struct TimingReportScript
    {
        u64 PresentId = 0;
        VkBool32 Complete = VK_FALSE;
        u64 StageTimeNs = 0;
    };

    bool SurfaceId2 = true;
    bool SurfaceWait2 = true;
    bool SurfaceTiming = true;
    bool SurfaceAbsolute = true;
    bool SurfaceRelative = true;
    bool PresentStageDomain = true;

    std::deque<VkResult> WaitResults;
    std::deque<VkResult> TimingPropertiesResults;
    std::deque<VkResult> TimeDomainCountResults;
    std::deque<VkResult> TimeDomainArrayResults;
    std::deque<VkResult> ExtPastResults;
    std::deque<VkResult> GoogleRefreshResults;
    std::deque<VkResult> GooglePastResults;
    std::deque<TimingReportScript> PastReports;
    VkResult SurfaceCapabilities2Result = VK_SUCCESS;
    VkResult LegacySurfaceCapabilitiesResult = VK_SUCCESS;
    VkResult QueueSizeResult = VK_SUCCESS;
    uint64_t ReportTimingPropertiesCounter = 1;
    uint64_t ReportTimeDomainsCounter = 1;

    int Caps2Calls = 0;
    int LegacyCapsCalls = 0;
    int QueueSizeCalls = 0;
    int WaitCalls = 0;
    int TimingPropertiesCalls = 0;
    int TimeDomainCountCalls = 0;
    int TimeDomainArrayCalls = 0;
    int ExtPastCalls = 0;
    int GoogleRefreshCalls = 0;
    int GooglePastCalls = 0;

    FakeVulkan()
    {
        Current = this;
    }

    ~FakeVulkan()
    {
        if (Current == this)
            Current = nullptr;
    }

    static VkResult Pop(std::deque<VkResult>& results, VkResult fallback = VK_SUCCESS)
    {
        if (results.empty())
            return fallback;
        const VkResult result = results.front();
        results.pop_front();
        return result;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL GetSurfaceCapabilities2(
        VkPhysicalDevice,
        const VkPhysicalDeviceSurfaceInfo2KHR*,
        VkSurfaceCapabilities2KHR* capabilities)
    {
        ++Current->Caps2Calls;
        if (Current->SurfaceCapabilities2Result != VK_SUCCESS)
            return Current->SurfaceCapabilities2Result;
        if (!capabilities)
            return VK_ERROR_UNKNOWN;
        capabilities->surfaceCapabilities.minImageCount = 2;
        capabilities->surfaceCapabilities.maxImageCount = 3;
        capabilities->surfaceCapabilities.currentExtent = {256, 192};
        for (VkBaseOutStructure* node =
                 reinterpret_cast<VkBaseOutStructure*>(capabilities->pNext);
             node != nullptr; node = node->pNext)
        {
            switch (node->sType)
            {
            case VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR:
                reinterpret_cast<VkSurfaceCapabilitiesPresentId2KHR*>(node)
                    ->presentId2Supported = Current->SurfaceId2 ? VK_TRUE : VK_FALSE;
                break;
            case VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_WAIT_2_KHR:
                reinterpret_cast<VkSurfaceCapabilitiesPresentWait2KHR*>(node)
                    ->presentWait2Supported = Current->SurfaceWait2 ? VK_TRUE : VK_FALSE;
                break;
            case VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT:
            {
                auto* timing = reinterpret_cast<VkPresentTimingSurfaceCapabilitiesEXT*>(node);
                timing->presentTimingSupported = Current->SurfaceTiming ? VK_TRUE : VK_FALSE;
                timing->presentAtAbsoluteTimeSupported =
                    Current->SurfaceAbsolute ? VK_TRUE : VK_FALSE;
                timing->presentAtRelativeTimeSupported =
                    Current->SurfaceRelative ? VK_TRUE : VK_FALSE;
                timing->presentStageQueries =
                    Current->SurfaceTiming
                        ? VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT
                        : 0;
                break;
            }
            default:
                break;
            }
        }
        return VK_SUCCESS;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL GetSurfaceCapabilities(
        VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR* capabilities)
    {
        ++Current->LegacyCapsCalls;
        if (Current->LegacySurfaceCapabilitiesResult != VK_SUCCESS)
            return Current->LegacySurfaceCapabilitiesResult;
        if (!capabilities)
            return VK_ERROR_UNKNOWN;
        capabilities->minImageCount = 2;
        return VK_SUCCESS;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL SetQueueSize(
        VkDevice, VkSwapchainKHR, uint32_t)
    {
        ++Current->QueueSizeCalls;
        return Current->QueueSizeResult;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL WaitForPresent2(
        VkDevice, VkSwapchainKHR, const VkPresentWait2InfoKHR*)
    {
        ++Current->WaitCalls;
        return Pop(Current->WaitResults);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL GetTimingProperties(
        VkDevice, VkSwapchainKHR, VkSwapchainTimingPropertiesEXT* properties,
        uint64_t* counter)
    {
        ++Current->TimingPropertiesCalls;
        const VkResult result = Pop(Current->TimingPropertiesResults);
        if (result == VK_SUCCESS)
        {
            if (properties)
            {
                properties->refreshDuration = FrameIntervalNs;
                properties->refreshInterval = FrameIntervalNs;
            }
            if (counter)
                *counter = 1;
        }
        return result;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL GetTimeDomains(
        VkDevice, VkSwapchainKHR, VkSwapchainTimeDomainPropertiesEXT* properties,
        uint64_t* counter)
    {
        const bool countQuery = properties && properties->pTimeDomains == nullptr;
        if (countQuery)
            ++Current->TimeDomainCountCalls;
        else
            ++Current->TimeDomainArrayCalls;
        VkResult result = countQuery
            ? Pop(Current->TimeDomainCountResults)
            : Pop(Current->TimeDomainArrayResults);
        if (result == VK_SUCCESS && properties)
        {
            if (countQuery)
            {
                properties->timeDomainCount = Current->PresentStageDomain ? 2 : 1;
            }
            else
            {
                properties->timeDomainCount = Current->PresentStageDomain ? 2 : 1;
                properties->pTimeDomains[0] = VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT;
                properties->pTimeDomainIds[0] = 7;
                if (Current->PresentStageDomain)
                {
                    properties->pTimeDomains[1] = VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT;
                    properties->pTimeDomainIds[1] = 8;
                }
            }
            if (counter)
                *counter = 1;
        }
        return result;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL GetExtPastTiming(
        VkDevice, const VkPastPresentationTimingInfoEXT*,
        VkPastPresentationTimingPropertiesEXT* properties)
    {
        ++Current->ExtPastCalls;
        const VkResult result = Pop(Current->ExtPastResults);
        if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && properties)
        {
            properties->timingPropertiesCounter = Current->ReportTimingPropertiesCounter;
            properties->timeDomainsCounter = Current->ReportTimeDomainsCounter;
            const uint32_t capacity = properties->presentationTimingCount;
            properties->presentationTimingCount = 0;
            if (capacity != 0 && properties->pPresentationTimings != nullptr
                && !Current->PastReports.empty())
            {
                const TimingReportScript reportScript = Current->PastReports.front();
                Current->PastReports.pop_front();
                VkPastPresentationTimingEXT& report = properties->pPresentationTimings[0];
                report.presentId = reportScript.PresentId;
                report.reportComplete = reportScript.Complete;
                report.presentStageCount = reportScript.StageTimeNs != 0 ? 1 : 0;
                report.timeDomain = VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT;
                report.timeDomainId = 8;
                if (report.presentStageCount != 0 && report.pPresentStages != nullptr)
                {
                    report.pPresentStages[0].stage =
                        VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT;
                    report.pPresentStages[0].time = reportScript.StageTimeNs;
                }
                properties->presentationTimingCount = 1;
            }
        }
        return result;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL GetGoogleRefresh(
        VkDevice, VkSwapchainKHR, VkRefreshCycleDurationGOOGLE* duration)
    {
        ++Current->GoogleRefreshCalls;
        const VkResult result = Pop(Current->GoogleRefreshResults);
        if (result == VK_SUCCESS && duration)
            duration->refreshDuration = FrameIntervalNs;
        return result;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL GetGooglePast(
        VkDevice, VkSwapchainKHR, uint32_t* count,
        VkPastPresentationTimingGOOGLE*)
    {
        ++Current->GooglePastCalls;
        if (count)
            *count = 0;
        return Pop(Current->GooglePastResults);
    }

    VulkanPresentPacerDispatch Dispatch() const
    {
        VulkanPresentPacerDispatch dispatch;
        dispatch.GetPhysicalDeviceSurfaceCapabilities2KHR = &GetSurfaceCapabilities2;
        dispatch.GetPhysicalDeviceSurfaceCapabilitiesKHR = &GetSurfaceCapabilities;
        dispatch.SetSwapchainPresentTimingQueueSizeEXT = &SetQueueSize;
        dispatch.WaitForPresent2KHR = &WaitForPresent2;
        dispatch.GetSwapchainTimingPropertiesEXT = &GetTimingProperties;
        dispatch.GetSwapchainTimeDomainPropertiesEXT = &GetTimeDomains;
        dispatch.GetPastPresentationTimingEXT = &GetExtPastTiming;
        dispatch.GetRefreshCycleDurationGOOGLE = &GetGoogleRefresh;
        dispatch.GetPastPresentationTimingGOOGLE = &GetGooglePast;
        return dispatch;
    }
};

FakeVulkan* FakeVulkan::Current = nullptr;

int Failures = 0;

[[noreturn]] void Fail(const std::string& message)
{
    std::fprintf(stderr, "Vulkan pacer dispatch test FAILED: %s\n", message.c_str());
    std::exit(1);
}

void Require(bool condition, const std::string& message)
{
    if (!condition)
        Fail(message);
}

VkDevice FakeDevice()
{
    return reinterpret_cast<VkDevice>(static_cast<uintptr_t>(0x11));
}

VkPhysicalDevice FakePhysicalDevice()
{
    return reinterpret_cast<VkPhysicalDevice>(static_cast<uintptr_t>(0x22));
}

VulkanPresentPacerInitInfo BaseInfo(bool ext, bool wait, bool google)
{
    VulkanPresentPacerInitInfo info;
    info.Device = FakeDevice();
    info.PhysicalDevice = FakePhysicalDevice();
    info.PresentId2ExtensionEnabled = true;
    info.PresentWait2ExtensionEnabled = wait;
    info.PresentTimingExtensionEnabled = ext;
    info.GoogleDisplayTimingExtensionEnabled = google;
    info.GoogleDisplayTimingFeatureEnabled = google;
    info.PresentAtAbsoluteTimeFeatureEnabled = ext;
    info.PresentAtRelativeTimeFeatureEnabled = ext;
    info.LatestReadyExtensionEnabled = true;
    return info;
}

void ConfigureCapabilities(VulkanPresentPacer& pacer, FakeVulkan& fake)
{
    VkSurfaceCapabilitiesKHR capabilities{};
    Require(pacer.QuerySurfaceCapabilities(capabilities),
        "fake surface capabilities query must succeed");
    Require(fake.Caps2Calls == 1, "modern surface capabilities must use the fake call");
}

void TestSurfaceCapabilitiesFallback()
{
    FakeVulkan fake;
    fake.SurfaceCapabilities2Result = VK_ERROR_EXTENSION_NOT_PRESENT;
    VulkanPresentPacer pacer;
    const VulkanPresentPacerInitInfo info = BaseInfo(true, false, false);
    Require(pacer.InitializeForTesting(fake.Dispatch(), info, Surface),
        "fallback pacer initialization must succeed");

    VkSurfaceCapabilitiesKHR capabilities{};
    Require(pacer.QuerySurfaceCapabilities(capabilities),
        "legacy surface capabilities fallback must succeed");
    Require(fake.Caps2Calls == 1 && fake.LegacyCapsCalls == 1,
        "failed modern surface query must invoke the legacy fallback exactly once");
    Require(capabilities.minImageCount == 2,
        "legacy surface capabilities must populate the returned capabilities");
    Require(pacer.GetSwapchainCreateFlags() == 0,
        "legacy surface capability fallback must disable modern swapchain flags");
}

void MakePacer(
    VulkanPresentPacer& pacer, FakeVulkan& fake,
    bool ext = true, bool wait = true, bool google = false)
{
    const VulkanPresentPacerInitInfo info = BaseInfo(ext, wait, google);
    Require(pacer.InitializeForTesting(fake.Dispatch(), info, Surface),
        "fake pacer initialization must succeed");
    ConfigureCapabilities(pacer, fake);
}

void StartSwapchain(VulkanPresentPacer& pacer, VkSwapchainKHR swapchain = Swapchain)
{
    pacer.OnSwapchainCreated(swapchain, VK_PRESENT_MODE_FIFO_KHR, 2);
}

void TestWaitResult(VkResult scripted, VulkanPacerBeginResult expected)
{
    FakeVulkan fake;
    VulkanPresentPacer pacer;
    MakePacer(pacer, fake, false, true, false);
    pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::PresentWait));
    StartSwapchain(pacer);

    VkPresentInfoKHR present{};
    VulkanPresentPacer::PresentMetadata metadata{};
    (void)pacer.BeginFrame(false, false, true, FrameIntervalNs);
    Require(pacer.PreparePresent(present, 1, metadata) != 0,
        "present-id2 metadata must be prepared for wait coverage");
    pacer.NotifyPresentResult(VK_SUCCESS, metadata);
    fake.WaitResults.push_back(scripted);
    const VulkanPacerBeginResult result =
        pacer.BeginFrame(false, false, true, FrameIntervalNs);
    Require(result == expected, "WaitForPresent2KHR result must route through production pacer");
    const VulkanPacerBeginAction action = VulkanPacerActionFor(result);
    if (expected == VulkanPacerBeginResult::SwapchainSuboptimal
        || expected == VulkanPacerBeginResult::SwapchainOutOfDate)
        Require(action.RebuildSwapchain && !action.FailRenderer,
            "swapchain wait result must request a rebuild");
    if (expected == VulkanPacerBeginResult::DeviceLost
        || expected == VulkanPacerBeginResult::SurfaceLost)
        Require(!action.RebuildSwapchain && action.FailRenderer,
            "device/surface loss from wait must fail the renderer");
    if (expected == VulkanPacerBeginResult::Continue)
        Require(!action.RebuildSwapchain && !action.FailRenderer,
            "successful/timeout/unknown wait must continue");
    if (scripted == VK_TIMEOUT)
    {
        const auto snapshot = pacer.CaptureState(metadata);
        Require(snapshot.WaitTimeouts == 1 && snapshot.BoundedWaitAttempted,
            "wait timeout must be counted and attributed to the current frame");
    }
    if (scripted == VK_ERROR_UNKNOWN)
    {
        const int calls = fake.WaitCalls;
        (void)pacer.BeginFrame(false, false, true, FrameIntervalNs);
        Require(fake.WaitCalls == calls, "unknown wait result must disable repeated calls");
    }
}

void TestWaitResults()
{
    TestWaitResult(VK_SUCCESS, VulkanPacerBeginResult::Continue);
    TestWaitResult(VK_TIMEOUT, VulkanPacerBeginResult::Continue);
    TestWaitResult(VK_SUBOPTIMAL_KHR, VulkanPacerBeginResult::SwapchainSuboptimal);
    TestWaitResult(VK_ERROR_OUT_OF_DATE_KHR, VulkanPacerBeginResult::SwapchainOutOfDate);
    TestWaitResult(VK_ERROR_DEVICE_LOST, VulkanPacerBeginResult::DeviceLost);
    TestWaitResult(VK_ERROR_SURFACE_LOST_KHR, VulkanPacerBeginResult::SurfaceLost);
    TestWaitResult(VK_ERROR_UNKNOWN, VulkanPacerBeginResult::Continue);
}

void TestPresenterOneFrameBudgetWait()
{
    FakeVulkan fake;
    VulkanPresentPacer pacer;
    MakePacer(pacer, fake, false, true, false);
    pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::PresenterOneFrameBudget));
    StartSwapchain(pacer);

    VkPresentInfoKHR present{};
    VulkanPresentPacer::PresentMetadata metadata{};
    Require(pacer.BeginFrame(false, false, true, FrameIntervalNs)
                == VulkanPacerBeginResult::Continue,
        "strict presenter pacing must bootstrap without a previous present");
    Require(pacer.PreparePresent(present, 1, metadata) != 0,
        "strict presenter pacing must retain present-id2 correlation");
    pacer.NotifyPresentResult(VK_SUCCESS, metadata);

    const int callsBefore = fake.WaitCalls;
    Require(pacer.BeginFrame(false, false, true, FrameIntervalNs)
                == VulkanPacerBeginResult::Continue,
        "strict presenter pacing wait must be non-fatal on success");
    Require(fake.WaitCalls == callsBefore + 1,
        "strict presenter pacing must wait for the previous accepted present");
    const auto snapshot = pacer.CaptureState(metadata);
    Require(snapshot.Policy == static_cast<int>(
                VulkanPresentPacingPolicy::PresenterOneFrameBudget)
            && snapshot.BoundedPresentWait && snapshot.BoundedWaitAttempted
            && snapshot.Authority == static_cast<int>(VulkanPacingAuthority::GenericPresentTiming),
        "strict presenter pacing capture must expose its bounded-wait authority");

    FakeVulkan noWaitFake;
    VulkanPresentPacer noWaitPacer;
    MakePacer(noWaitPacer, noWaitFake, false, false, false);
    noWaitPacer.SetPolicy(static_cast<int>(
        VulkanPresentPacingPolicy::PresenterOneFrameBudget));
    StartSwapchain(noWaitPacer);
    VulkanPresentPacer::PresentMetadata noWaitMetadata{};
    VkPresentInfoKHR noWaitPresent{};
    (void)noWaitPacer.BeginFrame(false, false, true, FrameIntervalNs);
    Require(noWaitPacer.PreparePresent(noWaitPresent, 1, noWaitMetadata) != 0,
        "strict presenter no-wait path must still prepare present-id2 metadata");
    noWaitPacer.NotifyPresentResult(VK_SUCCESS, noWaitMetadata);
    (void)noWaitPacer.BeginFrame(false, false, true, FrameIntervalNs);
    Require(noWaitFake.WaitCalls == 0,
        "strict presenter pacing must not call unavailable present_wait2");
}

void TestExtPastResult(VkResult scripted, VulkanPacerBeginResult expected)
{
    FakeVulkan fake;
    VulkanPresentPacer pacer;
    MakePacer(pacer, fake, true, false, false);
    pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::JustInTimeFifoLatestReady));
    StartSwapchain(pacer);
    fake.ExtPastResults.push_back(scripted);
    const VulkanPacerBeginResult result =
        pacer.BeginFrame(false, false, true, FrameIntervalNs);
    Require(result == expected, "EXT past-timing result must route through production pacer");
    const VulkanPacerBeginAction action = VulkanPacerActionFor(result);
    if (expected == VulkanPacerBeginResult::SwapchainOutOfDate)
        Require(action.RebuildSwapchain, "EXT OUT_OF_DATE must rebuild the swapchain");
    if (expected == VulkanPacerBeginResult::DeviceLost
        || expected == VulkanPacerBeginResult::SurfaceLost)
        Require(action.FailRenderer && !action.RebuildSwapchain,
            "EXT device/surface loss must fail the renderer");
    if (scripted == VK_ERROR_UNKNOWN)
    {
        const int calls = fake.ExtPastCalls;
        (void)pacer.BeginFrame(false, false, true, FrameIntervalNs);
        Require(fake.ExtPastCalls == calls,
            "unknown EXT past-timing result must disable repeated calls");
    }
}

void TestExtPastResults()
{
    TestExtPastResult(VK_SUCCESS, VulkanPacerBeginResult::Continue);
    TestExtPastResult(VK_INCOMPLETE, VulkanPacerBeginResult::Continue);
    TestExtPastResult(VK_ERROR_OUT_OF_DATE_KHR, VulkanPacerBeginResult::SwapchainOutOfDate);
    TestExtPastResult(VK_ERROR_DEVICE_LOST, VulkanPacerBeginResult::DeviceLost);
    TestExtPastResult(VK_ERROR_SURFACE_LOST_KHR, VulkanPacerBeginResult::SurfaceLost);
    TestExtPastResult(VK_ERROR_UNKNOWN, VulkanPacerBeginResult::Continue);
}

void TestTimingPropertiesResult(VkResult scripted, VulkanPacerBeginResult expected)
{
    FakeVulkan fake;
    VulkanPresentPacer pacer;
    MakePacer(pacer, fake, true, false, false);
    pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::JustInTimeFifoLatestReady));
    fake.TimingPropertiesResults.push_back(scripted);
    if (scripted == VK_NOT_READY)
        fake.TimingPropertiesResults.push_back(VK_SUCCESS);
    StartSwapchain(pacer);
    if (scripted == VK_NOT_READY)
    {
        VkPresentInfoKHR present{};
        VulkanPresentPacer::PresentMetadata metadata{};
        (void)pacer.BeginFrame(false, false, true, FrameIntervalNs);
        pacer.PreparePresent(present, 1, metadata);
        pacer.NotifyPresentResult(VK_SUCCESS, metadata);
        Require(pacer.BeginFrame(false, false, true, FrameIntervalNs)
                    == VulkanPacerBeginResult::Continue,
            "timing properties VK_NOT_READY retry must remain non-fatal");
        Require(fake.TimingPropertiesCalls >= 2,
            "timing properties VK_NOT_READY must retry after a present");
        return;
    }
    const VulkanPacerBeginResult result =
        pacer.BeginFrame(false, false, true, FrameIntervalNs);
    Require(result == expected,
        "timing-properties result must reach the typed production lifecycle result");
    const VulkanPacerBeginAction action = VulkanPacerActionFor(result);
    if (scripted == VK_ERROR_SURFACE_LOST_KHR)
        Require(action.FailRenderer && !action.RebuildSwapchain,
            "timing-properties surface loss must fail the renderer");
    if (scripted == VK_ERROR_UNKNOWN)
        Require(!pacer.ShouldUseFifoLatestReady(),
            "unknown timing-properties result must retire target scheduling");
}

void TestTimingProperties()
{
    TestTimingPropertiesResult(VK_SUCCESS, VulkanPacerBeginResult::Continue);
    TestTimingPropertiesResult(VK_NOT_READY, VulkanPacerBeginResult::Continue);
    TestTimingPropertiesResult(VK_ERROR_SURFACE_LOST_KHR, VulkanPacerBeginResult::SurfaceLost);
    TestTimingPropertiesResult(VK_ERROR_UNKNOWN, VulkanPacerBeginResult::Continue);
}

void TestTimeDomainsSuccessAndRetry()
{
    {
        FakeVulkan fake;
        VulkanPresentPacer pacer;
        MakePacer(pacer, fake, true, false, false);
        pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::JustInTimeFifoLatestReady));
        StartSwapchain(pacer);
        Require(fake.TimeDomainCountCalls == 1 && fake.TimeDomainArrayCalls == 1,
            "time-domain success must issue count and array calls");
        Require(pacer.ShouldUseFifoLatestReady(),
            "valid time-domain contract must keep target scheduling available");
    }
    {
        FakeVulkan fake;
        fake.TimeDomainCountResults.push_back(VK_INCOMPLETE);
        fake.TimeDomainCountResults.push_back(VK_SUCCESS);
        VulkanPresentPacer pacer;
        MakePacer(pacer, fake, true, false, false);
        pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::JustInTimeFifoLatestReady));
        StartSwapchain(pacer);
        Require(fake.TimeDomainCountCalls == 2 && fake.TimeDomainArrayCalls == 1,
            "time-domain count VK_INCOMPLETE must retry with a bounded count query");
    }
    {
        FakeVulkan fake;
        fake.TimeDomainArrayResults.push_back(VK_INCOMPLETE);
        fake.TimeDomainArrayResults.push_back(VK_INCOMPLETE);
        fake.TimeDomainArrayResults.push_back(VK_SUCCESS);
        VulkanPresentPacer pacer;
        MakePacer(pacer, fake, true, false, false);
        pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::JustInTimeFifoLatestReady));
        StartSwapchain(pacer);
        Require(fake.TimeDomainCountCalls == 3 && fake.TimeDomainArrayCalls == 3,
            "time-domain array VK_INCOMPLETE must retry within the bounded limit");
    }
}

void TestTimeDomainIncompleteExhaustion()
{
    FakeVulkan fake;
    fake.TimeDomainCountResults.push_back(VK_INCOMPLETE);
    fake.TimeDomainCountResults.push_back(VK_INCOMPLETE);
    fake.TimeDomainCountResults.push_back(VK_INCOMPLETE);
    // The fourth call is the production retry after an accepted present.
    fake.TimeDomainCountResults.push_back(VK_SUCCESS);
    // Keep the report counters stable so ReportPastTiming cannot trigger an
    // unrelated counter-change refresh before the explicit pending retry.
    fake.ReportTimeDomainsCounter = 0;

    VulkanPresentPacer pacer;
    MakePacer(pacer, fake, true, false, false);
    pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::JustInTimeFifoLatestReady));
    StartSwapchain(pacer);
    Require(fake.TimeDomainCountCalls == 3 && fake.TimeDomainArrayCalls == 0,
        "three VK_INCOMPLETE count results must exhaust the bounded enumeration");

    // No accepted present exists yet, so the pending retry must remain armed
    // without issuing another count query.
    Require(pacer.BeginFrame(false, false, true, FrameIntervalNs)
                == VulkanPacerBeginResult::Continue,
        "bounded time-domain exhaustion must remain non-fatal before a present");
    Require(fake.TimeDomainCountCalls == 3 && fake.TimeDomainArrayCalls == 0,
        "time-domain retry must wait for an accepted present");

    VkPresentInfoKHR present{};
    VulkanPresentPacer::PresentMetadata metadata{};
    pacer.PreparePresent(present, 1, metadata);
    pacer.NotifyPresentResult(VK_SUCCESS, metadata);
    Require(pacer.BeginFrame(false, false, true, FrameIntervalNs)
                == VulkanPacerBeginResult::Continue,
        "accepted present must trigger the pending time-domain retry");
    Require(fake.TimeDomainCountCalls == 4 && fake.TimeDomainArrayCalls == 1,
        "pending time-domain retry must use exactly one count/array success pass");
    Require(pacer.ShouldUseFifoLatestReady(),
        "successful post-present retry must restore target scheduling availability");
}

void TestTimeDomainFailures()
{
    {
        FakeVulkan fake;
        fake.TimeDomainCountResults.push_back(VK_ERROR_SURFACE_LOST_KHR);
        VulkanPresentPacer pacer;
        MakePacer(pacer, fake, true, false, false);
        StartSwapchain(pacer);
        const VulkanPacerBeginResult result =
            pacer.BeginFrame(false, false, true, FrameIntervalNs);
        Require(result == VulkanPacerBeginResult::SurfaceLost,
            "time-domain count surface loss must be latched for BeginFrame");
        Require(VulkanPacerActionFor(result).FailRenderer,
            "time-domain count surface loss must fail the renderer");
    }
    {
        FakeVulkan fake;
        fake.TimeDomainArrayResults.push_back(VK_ERROR_SURFACE_LOST_KHR);
        VulkanPresentPacer pacer;
        MakePacer(pacer, fake, true, false, false);
        StartSwapchain(pacer);
        const VulkanPacerBeginResult result =
            pacer.BeginFrame(false, false, true, FrameIntervalNs);
        Require(result == VulkanPacerBeginResult::SurfaceLost,
            "time-domain array surface loss must be latched for BeginFrame");
    }
    {
        FakeVulkan fake;
        fake.PresentStageDomain = false;
        VulkanPresentPacer pacer;
        MakePacer(pacer, fake, true, false, false);
        pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::JustInTimeFifoLatestReady));
        StartSwapchain(pacer);
        Require(!pacer.ShouldUseFifoLatestReady(),
            "missing PRESENT_STAGE_LOCAL must disable target scheduling");
    }
}

void TestGoogleRefreshResult(VkResult scripted, VulkanPacerBeginResult expected)
{
    FakeVulkan fake;
    VulkanPresentPacer pacer;
    MakePacer(pacer, fake, false, false, true);
    pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::JustInTime));
    fake.GoogleRefreshResults.push_back(scripted);
    StartSwapchain(pacer);
    const VulkanPacerBeginResult result =
        pacer.BeginFrame(false, false, true, FrameIntervalNs);
    Require(result == expected,
        "GOOGLE refresh-cycle result must route through production pacer");
    if (scripted == VK_ERROR_DEVICE_LOST || scripted == VK_ERROR_SURFACE_LOST_KHR)
        Require(VulkanPacerActionFor(result).FailRenderer,
            "GOOGLE refresh device/surface loss must fail the renderer");
    if (scripted == VK_ERROR_UNKNOWN)
    {
        const int calls = fake.GoogleRefreshCalls;
        (void)pacer.BeginFrame(false, false, true, FrameIntervalNs);
        Require(fake.GoogleRefreshCalls == calls,
            "unknown GOOGLE refresh result must disable repeat calls");
    }
}

void TestGoogleRefresh()
{
    TestGoogleRefreshResult(VK_SUCCESS, VulkanPacerBeginResult::Continue);
    TestGoogleRefreshResult(VK_ERROR_DEVICE_LOST, VulkanPacerBeginResult::DeviceLost);
    TestGoogleRefreshResult(VK_ERROR_SURFACE_LOST_KHR, VulkanPacerBeginResult::SurfaceLost);
    TestGoogleRefreshResult(VK_ERROR_UNKNOWN, VulkanPacerBeginResult::Continue);
}

void TestGooglePastResult(VkResult scripted, VulkanPacerBeginResult expected)
{
    FakeVulkan fake;
    VulkanPresentPacer pacer;
    MakePacer(pacer, fake, false, false, true);
    pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::JustInTime));
    fake.GoogleRefreshResults.push_back(VK_SUCCESS);
    fake.GooglePastResults.push_back(scripted);
    StartSwapchain(pacer);
    const VulkanPacerBeginResult result =
        pacer.BeginFrame(false, false, true, FrameIntervalNs);
    Require(result == expected,
        "GOOGLE past-timing result must route through production pacer");
    if (expected == VulkanPacerBeginResult::SwapchainOutOfDate)
        Require(VulkanPacerActionFor(result).RebuildSwapchain,
            "GOOGLE past OUT_OF_DATE must rebuild the swapchain");
    if (expected == VulkanPacerBeginResult::DeviceLost
        || expected == VulkanPacerBeginResult::SurfaceLost)
        Require(VulkanPacerActionFor(result).FailRenderer,
            "GOOGLE past device/surface loss must fail the renderer");
    if (scripted == VK_ERROR_UNKNOWN)
    {
        const int calls = fake.GooglePastCalls;
        (void)pacer.BeginFrame(false, false, true, FrameIntervalNs);
        Require(fake.GooglePastCalls == calls,
            "unknown GOOGLE past result must disable repeat calls");
    }
}

void TestGooglePast()
{
    TestGooglePastResult(VK_SUCCESS, VulkanPacerBeginResult::Continue);
    TestGooglePastResult(VK_INCOMPLETE, VulkanPacerBeginResult::Continue);
    TestGooglePastResult(VK_ERROR_OUT_OF_DATE_KHR, VulkanPacerBeginResult::SwapchainOutOfDate);
    TestGooglePastResult(VK_ERROR_DEVICE_LOST, VulkanPacerBeginResult::DeviceLost);
    TestGooglePastResult(VK_ERROR_SURFACE_LOST_KHR, VulkanPacerBeginResult::SurfaceLost);
    TestGooglePastResult(VK_ERROR_UNKNOWN, VulkanPacerBeginResult::Continue);
}

void TestQueuePressureAndRetry()
{
    {
        FakeVulkan fake;
        VulkanPresentPacer pacer;
        MakePacer(pacer, fake, true, false, false);
        StartSwapchain(pacer);

        // A real queue-full retry strips only timing metadata and keeps the
        // present ID/sequence owned by the production pacer.
        pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::TelemetryOnly));
        (void)pacer.BeginFrame(false, false, true, FrameIntervalNs);
        VkPresentInfoKHR present{};
        VulkanPresentPacer::PresentMetadata metadata{};
        pacer.PreparePresent(present, 1, metadata);
        Require(metadata.TimingAttached, "EXT telemetry present must carry timing metadata");
        Require(pacer.PrepareRetryWithoutTiming(
                    VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT, present, metadata),
            "queue-full result must prepare a retry without timing metadata");
        Require(!metadata.TimingAttached && present.pNext == &metadata.Id2
                    && metadata.Id2.pNext == nullptr,
            "queue-full retry must splice timing metadata from the present chain");
        pacer.NotifyPresentResult(VK_SUCCESS, metadata);
        const auto snapshot = pacer.CaptureState(metadata);
        Require(snapshot.TimingQueueFullCount == 1,
            "queue-full retry must be visible in capture state");
    }

    FakeVulkan fake;
    VulkanPresentPacer pacer;
    MakePacer(pacer, fake, true, false, false);
    StartSwapchain(pacer);

    // Fill the finite queue with accepted timed presents.  The next prepare
    // pauses metadata before vkQueuePresentKHR is called, which is the
    // production queue-pressure behavior.
    for (u64 id = 1; id <= 17; ++id)
    {
        (void)pacer.BeginFrame(false, false, true, FrameIntervalNs);
        VkPresentInfoKHR nextPresent{};
        VulkanPresentPacer::PresentMetadata nextMetadata{};
        pacer.PreparePresent(nextPresent, id, nextMetadata);
        if (id <= 16)
        {
            Require(nextMetadata.TimingAttached,
                "timing queue slots must remain attached before capacity");
            pacer.NotifyPresentResult(VK_SUCCESS, nextMetadata);
        }
        else
        {
            Require(!nextMetadata.TimingAttached,
                "full timing queue must pause metadata before present");
            const auto snapshot = pacer.CaptureState(nextMetadata);
            Require(snapshot.TimingQueueSize == 16,
                "queue pressure must preserve the allocated queue size");
        }
    }

    // A completed report drains one slot after pressure has paused metadata.
    // The production pacer must then grow the queue and re-enable metadata
    // before preparing the next present.
    fake.PastReports.push_back({1, VK_TRUE, FrameIntervalNs});
    (void)pacer.BeginFrame(false, false, true, FrameIntervalNs);
    VkPresentInfoKHR recoveredPresent{};
    VulkanPresentPacer::PresentMetadata recoveredMetadata{};
    pacer.PreparePresent(recoveredPresent, 18, recoveredMetadata);
    Require(recoveredMetadata.TimingAttached,
        "a completed timing report must re-enable metadata after queue pressure");
    const auto recoverySnapshot = pacer.CaptureState(recoveredMetadata);
    Require(recoverySnapshot.TimingQueueSize == 32,
        "queue-pressure recovery must grow the timing queue before resuming metadata");
    Require(recoverySnapshot.TimingQueueRecoveries == 1,
        "queue-pressure recovery must increment the recovery counter");
}

void TestTimingQueueAllocationFailure()
{
    FakeVulkan fake;
    fake.QueueSizeResult = VK_ERROR_OUT_OF_HOST_MEMORY;
    VulkanPresentPacer pacer;
    MakePacer(pacer, fake, true, false, false);
    pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::JustInTimeFifoLatestReady));
    StartSwapchain(pacer);

    Require(fake.QueueSizeCalls == 1,
        "failed initial timing queue allocation must call the setter once");
    Require(pacer.BeginFrame(false, false, true, FrameIntervalNs)
                == VulkanPacerBeginResult::Continue,
        "timing queue allocation failure must remain non-fatal to presentation");
    Require(!pacer.ShouldUseFifoLatestReady(),
        "failed timing queue allocation must disable FIFO latest-ready scheduling");
    Require(VulkanPacerActionFor(VulkanPacerBeginResult::Continue).RebuildSwapchain == false
                && VulkanPacerActionFor(VulkanPacerBeginResult::Continue).FailRenderer == false,
        "timing queue allocation failure must keep presenter action at Continue");

    VkPresentInfoKHR present{};
    VulkanPresentPacer::PresentMetadata metadata{};
    pacer.PreparePresent(present, 1, metadata);
    Require(!metadata.TimingAttached && metadata.TimingBackend == VulkanPresentTimingBackend::None,
        "failed timing queue allocation must not attach EXT timing metadata");
}

void TestSameFrameRecreationCapture()
{
    FakeVulkan fake;
    VulkanPresentPacer pacer;
    MakePacer(pacer, fake, true, false, false);
    StartSwapchain(pacer);
    pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::JustInTime));
    (void)pacer.BeginFrame(false, false, true, FrameIntervalNs);
    pacer.OnSwapchainCreated(RecreatedSwapchain, VK_PRESENT_MODE_FIFO_KHR, 2);

    VkPresentInfoKHR present{};
    VulkanPresentPacer::PresentMetadata metadata{};
    pacer.PreparePresent(present, 2, metadata);
    const auto snapshot = pacer.CaptureState(metadata);
    Require(snapshot.FallbackReason == static_cast<int>(
                VulkanJitFallbackReason::FrameDecisionInvalidatedBySwapchainRecreation),
        "same-frame recreation must attribute capture to lifecycle invalidation");
    Require(!snapshot.TargetTimeScheduling,
        "a recreated swapchain must not inherit target permission from the old frame");
}

void TestSameFrameRecreationWithLifecycleFailure()
{
    FakeVulkan fake;
    VulkanPresentPacer pacer;
    MakePacer(pacer, fake, true, false, false);
    pacer.SetPolicy(static_cast<int>(VulkanPresentPacingPolicy::JustInTime));
    StartSwapchain(pacer);
    Require(pacer.BeginFrame(false, false, true, FrameIntervalNs)
                == VulkanPacerBeginResult::Continue,
        "the retired generation must have a live decision before recreation");

    // The recreation's eager timing-properties query reports surface loss.
    // It must be latched for the next BeginFrame while the old decision is
    // independently invalidated for the first present on the new generation.
    fake.TimingPropertiesResults.push_back(VK_ERROR_SURFACE_LOST_KHR);
    pacer.OnSwapchainCreated(RecreatedSwapchain, VK_PRESENT_MODE_FIFO_KHR, 2);
    VkPresentInfoKHR present{};
    VulkanPresentPacer::PresentMetadata metadata{};
    pacer.PreparePresent(present, 2, metadata);
    const auto snapshot = pacer.CaptureState(metadata);
    Require(!metadata.TimingAttached && !metadata.GoogleTimingAttached
                && metadata.TimingBackend == VulkanPresentTimingBackend::None,
        "recreated present must not inherit timing/backend permission");
    Require(snapshot.FallbackReason == static_cast<int>(
                VulkanJitFallbackReason::FrameDecisionInvalidatedBySwapchainRecreation),
        "same-frame recreation must attribute capture to generation invalidation");

    const VulkanPacerBeginResult result =
        pacer.BeginFrame(false, false, true, FrameIntervalNs);
    const VulkanPacerBeginAction action = VulkanPacerActionFor(result);
    Require(result == VulkanPacerBeginResult::SurfaceLost,
        "recreated eager surface loss must reach the next BeginFrame");
    Require(action.FailRenderer && !action.RebuildSwapchain,
        "recreated surface loss must fail the renderer rather than rebuild");
}

} // namespace

int main()
{
    TestSurfaceCapabilitiesFallback();
    TestWaitResults();
    TestPresenterOneFrameBudgetWait();
    TestExtPastResults();
    TestTimingProperties();
    TestTimeDomainsSuccessAndRetry();
    TestTimeDomainIncompleteExhaustion();
    TestTimeDomainFailures();
    TestGoogleRefresh();
    TestGooglePast();
    TestQueuePressureAndRetry();
    TestTimingQueueAllocationFailure();
    TestSameFrameRecreationCapture();
    TestSameFrameRecreationWithLifecycleFailure();
    std::puts("Vulkan present pacer fake-dispatch tests passed");
    return Failures == 0 ? 0 : 1;
}
