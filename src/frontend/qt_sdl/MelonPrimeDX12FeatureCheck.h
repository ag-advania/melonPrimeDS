#pragma once

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <string>

namespace MelonPrime::DX12FeatureCheck
{

struct Result
{
    bool Available = false;
    bool NvidiaReflexAvailable = false;
    bool AmdAntiLag2Available = false;
    std::string Reason;
    std::string NvidiaReflexReason;
    std::string AmdAntiLag2Reason;
    std::string AdapterName;
};

// Probes the DirectX 12 runtime, adapter and device once, then caches the
// answer. Safe to call from the GUI thread (the settings dialog) and the
// emulation thread.
const Result& Probe();
bool IsRuntimeAvailable();
const std::string& UnavailableReason();

// Marks DX12 unavailable after a runtime failure so the settings dialog stops
// offering it until the user retries.
void ReportRuntimeFailure(std::string reason);
void ResetProbeForRetry();

} // namespace MelonPrime::DX12FeatureCheck

#endif
