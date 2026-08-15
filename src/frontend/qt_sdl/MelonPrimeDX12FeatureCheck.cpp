#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "MelonPrimeDX12FeatureCheck.h"

#include <mutex>
#include <utility>

#include "DX12AmdAntiLag2.h"
#include "DX12Context.h"
#include "DX12IntelXeLL.h"
#include "DX12NvidiaReflex.h"
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
        gResult.NvidiaReflexAvailable = false;
        gResult.NvidiaReflexReason = gResult.Reason;
        gResult.AmdAntiLag2Available = false;
        gResult.AmdAntiLag2Reason = gResult.Reason;
        gResult.IntelXeLLAvailable = false;
        gResult.IntelXeLLReason = gResult.Reason;
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
    const auto reflex = melonDS::DX12NvidiaReflex::Probe(
        context.GetDevice(), profile.VendorId);
    gResult.NvidiaReflexAvailable = reflex.Available;
    gResult.NvidiaReflexReason = reflex.Reason;
    const auto antiLag2 = melonDS::DX12AmdAntiLag2::Probe(
        context.GetDevice(), profile.VendorId);
    gResult.AmdAntiLag2Available = antiLag2.Available;
    gResult.AmdAntiLag2Reason = antiLag2.Reason;
    const auto xell = melonDS::DX12IntelXeLL::Probe(
        context.GetDevice(), profile.VendorId);
    gResult.IntelXeLLAvailable = xell.Available;
    gResult.IntelXeLLReason = xell.Reason;

    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Info,
        "MelonPrime DX12 probe: available=1 adapter=\"%s\" featureLevel=%X.%X shaderModel=%u.%u reflex=%d reflexReason=\"%s\" antiLag2=%d antiLag2Reason=\"%s\" xell=%d xellReason=\"%s\"\n",
        profile.AdapterName.c_str(),
        (static_cast<unsigned>(profile.FeatureLevel) >> 12) & 0xF,
        (static_cast<unsigned>(profile.FeatureLevel) >> 8) & 0xF,
        (profile.HighestShaderModel >> 4) & 0xF,
        profile.HighestShaderModel & 0xF,
        gResult.NvidiaReflexAvailable,
        gResult.NvidiaReflexReason.c_str(),
        gResult.AmdAntiLag2Available,
        gResult.AmdAntiLag2Reason.c_str(),
        gResult.IntelXeLLAvailable,
        gResult.IntelXeLLReason.c_str());

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
    gResult.NvidiaReflexAvailable = false;
    gResult.AmdAntiLag2Available = false;
    gResult.IntelXeLLAvailable = false;
    gResult.Reason = "DirectX 12 initialization failed";
    gResult.NvidiaReflexReason = gResult.Reason;
    gResult.AmdAntiLag2Reason = gResult.Reason;
    gResult.IntelXeLLReason = gResult.Reason;
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
