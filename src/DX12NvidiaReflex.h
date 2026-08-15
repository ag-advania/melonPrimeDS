/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef DX12_NVIDIA_REFLEX_H
#define DX12_NVIDIA_REFLEX_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <string>

#include "DX12Common.h"

namespace melonDS
{

enum class DX12NvidiaReflexMode : int
{
    Off = 0,
    On = 1,
    OnBoost = 2,
};

struct DX12NvidiaReflexSupport
{
    bool Available = false;
    std::string Reason;
};

// Per-renderer Reflex frame state. NVAPI itself is loaded once per process,
// while all marker calls stay on the emulation/render thread.
class DX12NvidiaReflex
{
public:
    static DX12NvidiaReflexSupport Probe(ID3D12Device* device, u32 vendorId);

    bool Initialize(ID3D12Device* device, u32 vendorId);
    void Shutdown() noexcept;

    bool SetMode(int mode);
    [[nodiscard]] bool IsAvailable() const noexcept { return Available; }
    [[nodiscard]] bool IsActive() const noexcept
    {
        return Available && ModeApplied && Mode != DX12NvidiaReflexMode::Off;
    }
    [[nodiscard]] DX12NvidiaReflexMode GetMode() const noexcept { return Mode; }
    [[nodiscard]] const std::string& GetUnavailableReason() const noexcept { return UnavailableReason; }

    // Call order for an emulated frame:
    // BeginFrame (sleep only) -> MarkInputSample -> input polling ->
    // MarkSimulationStart -> MarkRenderSubmitStart/End ->
    // MarkPresentStart/End -> FinishFrame.
    void BeginFrame();
    void MarkInputSample();
    void MarkSimulationStart();
    void MarkRenderSubmitStart();
    void MarkRenderSubmitEnd();
    void EndRenderPhase();
    void MarkPresentStart();
    void MarkPresentEnd();
    void FinishFrame();

private:
    enum class Marker : u32
    {
        SimulationStart = 0,
        SimulationEnd = 1,
        RenderSubmitStart = 2,
        RenderSubmitEnd = 3,
        PresentStart = 4,
        PresentEnd = 5,
        InputSample = 6,
    };

    bool SendMarker(Marker marker);
    void DisableForRuntimeFailure(const char* operation, int status);

    ID3D12Device* Device = nullptr;
    DX12NvidiaReflexMode Mode = DX12NvidiaReflexMode::Off;
    std::string UnavailableReason;
    u64 FrameId = 0;
    bool Available = false;
    bool ModeApplied = false;
    bool FrameOpen = false;
    bool InputSampled = false;
    bool SimulationOpen = false;
    bool RenderSubmitOpen = false;
    bool PresentOpen = false;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_NVIDIA_REFLEX_H
