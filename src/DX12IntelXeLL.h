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

#include <string>

#include "DX12Common.h"

namespace melonDS
{

struct DX12IntelXeLLSupport
{
    bool Available = false;
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
    void Shutdown() noexcept;

    // xellSetSleepMode must only be called while the shared D3D12 queue is
    // idle. DX12Renderer enforces that precondition before entering here.
    bool SetEnabled(bool enabled);
    [[nodiscard]] bool IsAvailable() const noexcept { return Available; }
    [[nodiscard]] bool IsEnabled() const noexcept { return Enabled; }
    [[nodiscard]] const std::string& GetUnavailableReason() const noexcept
    {
        return UnavailableReason;
    }

    // Required XeLL order:
    // Sleep -> SimulationStart -> InputSample -> RenderSubmitStart/End ->
    // PresentStart/End. Simulation and RenderSubmit are closed defensively on
    // every transition/early-exit path.
    void BeginFrame();
    void MarkInputSample();
    void MarkRenderSubmitStart();
    void MarkRenderSubmitEnd();
    void EndRenderPhase();
    void MarkPresentStart();
    void MarkPresentEnd();
    void FinishFrame();

private:
    enum class Marker : int
    {
        SimulationStart = 0,
        SimulationEnd = 1,
        RenderSubmitStart = 2,
        RenderSubmitEnd = 3,
        PresentStart = 4,
        PresentEnd = 5,
        InputSample = 6,
    };

    struct Api;

    bool SendMarker(Marker marker);
    void DisableForRuntimeFailure(const char* operation, int status);

    Api* Functions = nullptr;
    void* Context = nullptr;
    std::string UnavailableReason;
    u32 FrameId = 0;
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
