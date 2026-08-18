/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef DX12_INTEL_XELL_H
#define DX12_INTEL_XELL_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <cstdint>
#include <string>

#include "DX12Common.h"

namespace melonDS
{

struct DX12IntelXeLLSupport
{
    bool Available = false;
    std::string Reason;
};

using DX12IntelXeLLResult = std::int32_t;
using DX12IntelXeLLContext = void*;

struct DX12IntelXeLLSleepParams
{
    std::uint32_t MinimumIntervalUs = 0;
    // Bit 0: low latency; bit 1: boost (currently unsupported); rest reserved.
    std::uint32_t Flags = 0;
};

struct DX12IntelXeLLVersion
{
    std::uint16_t Major = 0;
    std::uint16_t Minor = 0;
    std::uint16_t Patch = 0;
    std::uint16_t Reserved = 0;
};

enum class DX12IntelXeLLMarker : std::int32_t
{
    SimulationStart = 0,
    SimulationEnd = 1,
    RenderSubmitStart = 2,
    RenderSubmitEnd = 3,
    PresentStart = 4,
    PresentEnd = 5,
    InputSample = 6,
};

enum class DX12IntelXeLLLoggingLevel : std::int32_t
{
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
};

using DX12IntelXeLLLogCallback = void (__cdecl*)(
    const char*, DX12IntelXeLLLoggingLevel);

// Exact XeLL 1.3.2 function table used by both the production DLL loader and
// the hardware-independent fake backend. Keeping the state machine behind
// this table lets CI execute the real MelonPrime integration without changing
// Intel's redistributable DLL or requiring an Intel Arc adapter.
struct DX12IntelXeLLApi
{
    using CreateContextFn = DX12IntelXeLLResult (__cdecl*)(
        ID3D12Device*, DX12IntelXeLLContext*);
    using DestroyContextFn = DX12IntelXeLLResult (__cdecl*)(DX12IntelXeLLContext);
    using SetSleepModeFn = DX12IntelXeLLResult (__cdecl*)(
        DX12IntelXeLLContext, const DX12IntelXeLLSleepParams*);
    using GetSleepModeFn = DX12IntelXeLLResult (__cdecl*)(
        DX12IntelXeLLContext, DX12IntelXeLLSleepParams*);
    using SleepFn = DX12IntelXeLLResult (__cdecl*)(
        DX12IntelXeLLContext, std::uint32_t);
    using AddMarkerFn = DX12IntelXeLLResult (__cdecl*)(
        DX12IntelXeLLContext, std::uint32_t, DX12IntelXeLLMarker);
    using GetVersionFn = DX12IntelXeLLResult (__cdecl*)(DX12IntelXeLLVersion*);
    using SetLoggingCallbackFn = DX12IntelXeLLResult (__cdecl*)(
        DX12IntelXeLLContext,
        DX12IntelXeLLLoggingLevel,
        DX12IntelXeLLLogCallback);

    CreateContextFn CreateContext = nullptr;
    DestroyContextFn DestroyContext = nullptr;
    SetSleepModeFn SetSleepMode = nullptr;
    GetSleepModeFn GetSleepMode = nullptr;
    SleepFn Sleep = nullptr;
    AddMarkerFn AddMarker = nullptr;
    GetVersionFn GetVersion = nullptr;
    SetLoggingCallbackFn SetLoggingCallback = nullptr;
};

struct DX12IntelXeLLStatus
{
    bool Requested = false;
    bool RuntimePresent = false;
    bool SupportedByProbe = false;
    bool ContextCreated = false;
    bool SleepModeApplied = false;
    bool ActualEnabled = false;
    std::uint32_t MinimumIntervalUs = 0;
    std::uint32_t FrameId = 0;
    DX12IntelXeLLVersion RuntimeVersion{};
    std::string Reason;
};

// Per-renderer Intel Xe Low Latency (XeLL) context. The official runtime is
// loaded from libxell.dll next to the executable so MinGW builds do not depend
// on Intel's MSVC import library. All frame calls remain on the emulation
// thread and use one monotonically increasing frame ID.
class DX12IntelXeLL
{
public:
    DX12IntelXeLL() = default;
    ~DX12IntelXeLL();

    DX12IntelXeLL(const DX12IntelXeLL&) = delete;
    DX12IntelXeLL& operator=(const DX12IntelXeLL&) = delete;

    static DX12IntelXeLLSupport Probe(ID3D12Device* device, u32 vendorId);

    bool Initialize(ID3D12Device* device, u32 vendorId);
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    // Test-only injection seam. The same lifecycle and marker implementation
    // used in production runs against a fake table on non-Intel hardware.
    bool InitializeForTesting(
        ID3D12Device* device,
        u32 vendorId,
        const DX12IntelXeLLApi& api);
#endif
    void Shutdown() noexcept;

    // xellSetSleepMode must only be called while the shared D3D12 queue is
    // idle. DX12Renderer enforces that precondition before entering here.
    bool SetEnabled(bool enabled) { return SetSleepMode(enabled, 0); }
    bool SetSleepMode(bool enabled, std::uint32_t minimumIntervalUs);
    [[nodiscard]] bool IsAvailable() const noexcept { return Available; }
    [[nodiscard]] bool IsEnabled() const noexcept { return Enabled; }
    [[nodiscard]] bool IsActive() const noexcept
    {
        return Available && Context && StateApplied && Enabled;
    }
    [[nodiscard]] DX12IntelXeLLStatus GetStatus() const;
    [[nodiscard]] const std::string& GetUnavailableReason() const noexcept
    {
        return UnavailableReason;
    }

    // Required XeLL order:
    // Sleep -> SimulationStart -> InputSample -> RenderSubmitStart/End ->
    // PresentStart/End. Simulation and RenderSubmit are closed defensively on
    // every transition/early-exit path. FinishFrame only closes phases that
    // actually started; it never synthesizes an input or Present marker.
    void BeginFrame();
    void MarkInputSample();
    void MarkRenderSubmitStart();
    void MarkRenderSubmitEnd();
    void EndRenderPhase();
    void MarkPresentStart();
    void MarkPresentEnd();
    void FinishFrame();

private:
    bool InitializeWithApi(
        ID3D12Device* device,
        u32 vendorId,
        const DX12IntelXeLLApi& api,
        bool runtimePresent);
    bool ValidateApi(const DX12IntelXeLLApi& api, std::string& missingSymbol) const;
    bool SendMarker(DX12IntelXeLLMarker marker);
    void DisableForRuntimeFailure(const char* operation, int status);
    void LogStatus(const char* event) const;

    DX12IntelXeLLApi Functions{};
    void* RuntimeModule = nullptr;
    void* Context = nullptr;
    std::string UnavailableReason;
    DX12IntelXeLLVersion RuntimeVersion{};
    u32 FrameId = 0;
    std::uint32_t MinimumIntervalUs = 0;
    bool Requested = false;
    bool RuntimePresent = false;
    bool SupportedByProbe = false;
    bool ContextCreated = false;
    bool Available = false;
    bool Enabled = false;
    bool StateApplied = false;
    bool FrameOpen = false;
    bool SimulationOpen = false;
    bool RenderSubmitStarted = false;
    bool RenderSubmitOpen = false;
    bool PresentStarted = false;
    bool PresentOpen = false;
    bool InputSampled = false;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_INTEL_XELL_H
