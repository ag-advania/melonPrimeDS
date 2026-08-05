#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "MelonPrimeDX12FeatureCheck.h"

#include <mutex>
#include <utility>

#include "DX12Context.h"
#include "Platform.h"

namespace MelonPrime::DX12FeatureCheck
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

    auto& context = melonDS::DX12Context::Get();
    if (!context.Acquire())
    {
        gResult.Available = false;
        gResult.Reason = context.GetFailureReason().empty()
            ? std::string("DirectX 12 is not available on this system")
            : context.GetFailureReason();
        gResult.AdapterName.clear();

        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrime DX12 probe: available=0 reason=%s\n",
            gResult.Reason.c_str());
        return gResult;
    }

    const auto& profile = context.GetDeviceProfile();
    gResult.Available = true;
    gResult.Reason.clear();
    gResult.AdapterName = profile.AdapterName;

    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Info,
        "MelonPrime DX12 probe: available=1 adapter=\"%s\" featureLevel=%X.%X shaderModel=%u.%u\n",
        profile.AdapterName.c_str(),
        (static_cast<unsigned>(profile.FeatureLevel) >> 12) & 0xF,
        (static_cast<unsigned>(profile.FeatureLevel) >> 8) & 0xF,
        (profile.HighestShaderModel >> 4) & 0xF,
        profile.HighestShaderModel & 0xF);

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

    const std::string diagnostic = reason.empty() ? std::string("unspecified runtime failure") : std::move(reason);
    gResult.Available = false;
    gResult.Reason = "DirectX 12 initialization failed";
    gProbed = true;

    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Error,
        "MelonPrime DX12 runtime disabled requested=DX12 actual=Software reason=%s\n",
        diagnostic.c_str());
}

void ResetProbeForRetry()
{
    std::scoped_lock lock(gProbeMutex);
    gResult = {};
    gProbed = false;
}

} // namespace MelonPrime::DX12FeatureCheck

#endif
