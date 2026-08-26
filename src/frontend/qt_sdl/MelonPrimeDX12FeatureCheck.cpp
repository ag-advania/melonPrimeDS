#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "MelonPrimeDX12FeatureCheck.h"

#include <mutex>
#include <utility>

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
#include <cstdlib>
#endif

#include "DX12AmdAntiLag2.h"
#include "DX12Common.h"
#include "DX12Context.h"
#include "DX12IntelXeLL.h"
#include "DX12NvidiaReflex.h"
#include "Platform.h"

namespace MelonPrime::DX12FeatureCheck
{
namespace
{
// What the last heavyweight admission attempt established, as opposed to what
// it happened to observe. Only Unsupported and Admitted are answers about this
// machine; Unknown means nothing durable has been learned yet.
enum class Admission
{
    Unknown,
    Admitted,
    Unsupported,
};

std::mutex gProbeMutex;
Result gResult{};
Admission gAdmission = Admission::Unknown;
// True only when gResult is a durable answer. A transient admission failure
// deliberately leaves this false so the next attempt re-probes.
bool gProbed = false;

// Stage A, with the lock already held. The loader resolves modules and entry
// points with LoadLibrary/GetProcAddress and nothing else, so this creates no
// device and cannot conflict with a live backend.
bool PlatformEligibleLocked()
{
    const auto& entry = melonDS::DX12::LoadEntryPoints();
    return entry.IsCoreReady() && entry.IsShaderCompilerReady();
}

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
// Developer-only seam for the "a transient admission failure must not be cached
// as unsupported" regression test.
// MELONPRIME_TEST_DX12_TRANSIENT_PROBE_FAILURES=N makes the first N admission
// attempts fail the way a device-creation failure does, without needing a
// machine where that actually happens. The test then checks that attempt N+1
// succeeds, which it can only do if the failures were left uncached.
bool ConsumeInjectedTransientFailureLocked()
{
    static int remaining = [] {
        const char* value =
            std::getenv("MELONPRIME_TEST_DX12_TRANSIENT_PROBE_FAILURES");
        return (value && value[0] != 0) ? std::atoi(value) : 0;
    }();
    if (remaining <= 0)
        return false;
    remaining--;
    return true;
}
#endif

void FillFailureLocked(std::string reason)
{
    gResult.Available = false;
    gResult.Reason = std::move(reason);
    gResult.NvidiaReflexAvailable = false;
    gResult.NvidiaReflexReason = gResult.Reason;
    gResult.AmdAntiLag2Available = false;
    gResult.AmdAntiLag2Reason = gResult.Reason;
    gResult.IntelXeLLAvailable = false;
    gResult.IntelXeLLReason = gResult.Reason;
    gResult.AdapterName.clear();
}
} // namespace

bool IsPlatformEligible()
{
    std::scoped_lock lock(gProbeMutex);
    return PlatformEligibleLocked();
}

const Result& ProbeRuntimeAdmission()
{
    std::scoped_lock lock(gProbeMutex);
    if (gProbed)
        return gResult;

    auto& context = melonDS::DX12Context::Get();
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    const bool injectedTransientFailure = ConsumeInjectedTransientFailureLocked();
#else
    constexpr bool injectedTransientFailure = false;
#endif
    if (injectedTransientFailure || !context.Acquire())
    {
        if (injectedTransientFailure)
        {
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Error,
                "[probe-test] injected transient DX12 admission failure\n");
        }

        FillFailureLocked(
            injectedTransientFailure
                ? std::string("injected transient admission failure")
                : (context.GetFailureReason().empty()
                    ? std::string("DirectX 12 is not available on this system")
                    : context.GetFailureReason()));

        // Only a missing runtime is a permanent answer about this machine.
        // Adapter enumeration and device creation can fail for reasons that are
        // not about capability at all -- most importantly another graphics
        // backend still holding a device -- so those stay uncached and the next
        // admission attempt re-probes. Caching them was what turned one
        // mistimed probe into "this PC does not support DX12" for the rest of
        // the process.
        const bool hardUnsupported =
            !injectedTransientFailure && !PlatformEligibleLocked();
        gProbed = hardUnsupported;
        gAdmission = hardUnsupported ? Admission::Unsupported : Admission::Unknown;

        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "MelonPrime DX12 probe: available=0 durable=%d reason=%s\n",
            hardUnsupported ? 1 : 0,
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

    gProbed = true;
    gAdmission = Admission::Admitted;

    context.Release();
    return gResult;
}

const Result& Probe()
{
    return ProbeRuntimeAdmission();
}

bool IsRuntimeAvailable()
{
    std::scoped_lock lock(gProbeMutex);
    switch (gAdmission)
    {
    case Admission::Unsupported:
        return false;
    case Admission::Admitted:
        return true;
    case Admission::Unknown:
        break;
    }
    return PlatformEligibleLocked();
}

const std::string& UnavailableReason()
{
    std::scoped_lock lock(gProbeMutex);
    return gResult.Reason;
}

void ReportRuntimeFailure(std::string reason)
{
    std::scoped_lock lock(gProbeMutex);

    const std::string diagnostic = reason.empty() ? std::string("unspecified runtime failure") : std::move(reason);
    FillFailureLocked("DirectX 12 initialization failed");
    // Sticky and user-visible: the renderer really did fail to come up, so the
    // settings dialog stops offering DX12 until the user asks again. That is a
    // different thing from a probe that could not run, which is why this is the
    // one failure that does latch.
    gProbed = true;
    gAdmission = Admission::Unsupported;

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
    gAdmission = Admission::Unknown;
}

} // namespace MelonPrime::DX12FeatureCheck

#endif
