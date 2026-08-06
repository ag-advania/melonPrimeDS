#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "MelonPrimeVulkanFeatureCheck.h"

#include <mutex>
#include <utility>

#include "Platform.h"
#include "VulkanContext.h"
#include "VulkanDispatch.h"

namespace MelonPrime::VulkanFeatureCheck
{
namespace
{
std::mutex gProbeMutex;
Result gResult{};
bool gProbed = false;
}

const Result& Probe()
{
    std::scoped_lock lock(gProbeMutex);
    if (gProbed)
        return gResult;

    gProbed = true;
    auto& context = melonDS::VulkanContext::Get();
    if (!context.Acquire())
    {
        gResult.Available = false;
        gResult.Reason = melonDS::VulkanDispatch::GetLoaderPath().empty()
            ? "Vulkan loader was not found"
            : "Vulkan initialization failed";
        gResult.NvidiaReflexAvailable = false;
        gResult.NvidiaReflexReason = gResult.Reason;
        gResult.AmdAntiLag2Available = false;
        gResult.AmdAntiLag2Reason = gResult.Reason;
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrime Vulkan probe: available=0 loader=%s reason=%s",
            melonDS::VulkanDispatch::GetLoaderPath().c_str(),
            gResult.Reason.c_str());
        return gResult;
    }

    gResult.Available = context.IsReady();
    gResult.Reason = gResult.Available ? std::string{} : "No compatible Vulkan device was found";
    gResult.NvidiaReflexAvailable = gResult.Available && context.SupportsNvidiaReflex();
    gResult.NvidiaReflexReason = gResult.NvidiaReflexAvailable
        ? std::string{}
        : context.GetNvidiaReflexUnavailableReason();
    gResult.AmdAntiLag2Available = gResult.Available && context.SupportsAmdAntiLag2();
    gResult.AmdAntiLag2Reason = gResult.AmdAntiLag2Available
        ? std::string{}
        : context.GetAmdAntiLag2UnavailableReason();
    melonDS::Platform::Log(
        gResult.Available ? melonDS::Platform::LogLevel::Info : melonDS::Platform::LogLevel::Error,
        "MelonPrime Vulkan probe: available=%d loader=%s device=%s queueFamily=%u reflex=%d reflexReason=\"%s\" antiLag2=%d antiLag2Reason=\"%s\"",
        gResult.Available ? 1 : 0,
        melonDS::VulkanDispatch::GetLoaderPath().c_str(),
        context.GetDeviceProfile().DeviceName.c_str(),
        context.GetQueueFamilyIndex(),
        gResult.NvidiaReflexAvailable ? 1 : 0,
        gResult.NvidiaReflexReason.c_str(),
        gResult.AmdAntiLag2Available ? 1 : 0,
        gResult.AmdAntiLag2Reason.c_str());
    context.Release();
    return gResult;
}

bool IsRuntimeAvailable()
{
    return Probe().Available;
}

const std::string& UnavailableReason()
{
    return Probe().Reason;
}

void ReportRuntimeFailure(std::string reason)
{
    std::scoped_lock lock(gProbeMutex);
    gResult.Available = false;
    gResult.NvidiaReflexAvailable = false;
    gResult.AmdAntiLag2Available = false;
    const std::string diagnostic = reason.empty() ? "unspecified runtime failure" : std::move(reason);
    gResult.Reason = "Vulkan initialization failed";
    gResult.NvidiaReflexReason = gResult.Reason;
    gResult.AmdAntiLag2Reason = gResult.Reason;
    gProbed = true;
    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Error,
        "MelonPrime Vulkan runtime disabled requested=Vulkan actual=Software reason=%s",
        diagnostic.c_str());
}

void ResetProbeForRetry()
{
    std::scoped_lock lock(gProbeMutex);
    gResult = {};
    gProbed = false;
}

void ResetProbeForTesting()
{
    ResetProbeForRetry();
}

} // namespace MelonPrime::VulkanFeatureCheck

#endif
