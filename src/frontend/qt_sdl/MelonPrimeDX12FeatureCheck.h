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
    bool IntelXeLLAvailable = false;
    std::string Reason;
    std::string NvidiaReflexReason;
    std::string AmdAntiLag2Reason;
    std::string IntelXeLLReason;
    std::string AdapterName;
};

// --- Stage A: passive eligibility -----------------------------------------
//
// Answers "could this build, on this machine, ever run DX12" from the module
// loader alone: d3d12.dll, dxgi.dll and the shader compiler resolve, or they
// do not. It creates no device, enumerates no adapter and touches no vendor
// runtime, so it is safe to call at any point in a backend transition --
// including while another graphics API still owns a device.
bool IsPlatformEligible();

// --- Stage B: heavyweight runtime admission -------------------------------
//
// Creates a D3D12 device, picks an adapter and probes the vendor low-latency
// runtimes.
//
// **Must not be called while another graphics backend still owns a device.**
// On Windows, creating a D3D12 device while a VkDevice is live can fail with
// an empty adapter enumeration and take the Vulkan device down with it; that
// was REAUDIT-P1-001. Call it only after the outgoing backend has been
// quiesced and released -- which, in the emulation thread, means after the old
// renderer has been destroyed.
//
// A *hard* unsupported answer (the runtime is not installed) is cached
// permanently. A device-creation or adapter-enumeration failure is not: those
// can fail for reasons that are not about this machine's capability, so they
// are left uncached and the next admission attempt re-probes.
const Result& ProbeRuntimeAdmission();

// Alias kept for callers that already hold the selected backend's device --
// the settings dialog only reaches this while DX12 is the active renderer, so
// the underlying Acquire() is a refcount bump rather than a device creation.
const Result& Probe();

// The passive answer, for renderer normalization.
//
// Normalization runs before the outgoing backend has been released, so it must
// never create a device. This returns the cached admission result when there is
// one, and otherwise falls back to Stage A eligibility -- optimistically, on
// purpose. A machine that turns out to have no usable D3D12 device fails at the
// admission step instead, which happens after teardown and degrades cleanly to
// Software.
bool IsRuntimeAvailable();

const std::string& UnavailableReason();

// Marks DX12 unavailable after a runtime failure so the settings dialog stops
// offering it until the user retries. This is the sticky, user-visible latch;
// it is distinct from a probe failure.
void ReportRuntimeFailure(std::string reason);
void ResetProbeForRetry();

} // namespace MelonPrime::DX12FeatureCheck

#endif
