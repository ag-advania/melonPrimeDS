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

// Why QueryTimings() returned nothing. "Empty" has three very different
// meanings and collapsing them makes the diagnostic useless: a missing entry
// point is a driver capability gap, a rejected query is an ABI or usage
// problem, and a successful-but-empty ring is the driver declining to
// correlate frames -- which is the one that says Reflex is inert.
enum class DX12NvidiaReflexLatencyReportStatus : int
{
    NotQueried = 0,
    Unsupported,      // the driver did not expose NvAPI_D3D_GetLatency
    QueryFailed,      // the call was rejected; see the logged NVAPI status
    NoCompleteFrames, // the call succeeded and the ring holds nothing complete
    Available,        // at least one complete report was returned
};

[[nodiscard]] const char* DX12NvidiaReflexLatencyReportStatusName(
    DX12NvidiaReflexLatencyReportStatus status) noexcept;

// One driver-side latency report: the subset of NVAPI's
// NV_LATENCY_RESULT_PARAMS::FrameReport that MelonPrime consumes. Timestamps
// are the driver's microsecond clock, and FrameId is the logical frame id the
// markers carried, which is what makes correlation checkable.
//
// This mirrors VkLatencyTimingsFrameReportNV field for field so the DX12 and
// Vulkan diagnostics can be read side by side.
struct DX12NvidiaReflexFrameReport
{
    u64 FrameId = 0;
    u64 InputSampleTimeUs = 0;
    u64 SimStartTimeUs = 0;
    u64 SimEndTimeUs = 0;
    u64 RenderSubmitStartTimeUs = 0;
    u64 RenderSubmitEndTimeUs = 0;
    u64 PresentStartTimeUs = 0;
    u64 PresentEndTimeUs = 0;
    u64 GpuRenderStartTimeUs = 0;
    u64 GpuRenderEndTimeUs = 0;
    u32 GpuActiveRenderTimeUs = 0;
    u32 GpuFrameTimeUs = 0;
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
    void BeginFrame(u64 logicalFrameId);
    void MarkInputSample();
    void MarkSimulationStart();
    void MarkRenderSubmitStart();
    void MarkRenderSubmitEnd();
    void EndRenderPhase();
    void MarkPresentStart();
    void MarkPresentEnd();
    void FinishFrame();

    // Driver-side latency reports, newest last, for the frames the driver has
    // finished correlating. Fills at most `maxCount` entries and returns how
    // many were written; 0 means Reflex is not running, the driver does not
    // expose latency reporting, or it has nothing complete yet.
    //
    // This is the only observable proof that the markers and the Sleep call
    // actually reached the driver and were matched to one frame id --
    // NvAPI_D3D_SetLatencyMarker's result says nothing about correlation. It is
    // the DX12 counterpart of VulkanNvidiaReflex::QueryTimings(), whose absence
    // meant DX12 Reflex could be inert without any signal. Cold path: intended
    // for a periodic diagnostic, not for every frame.
    //
    // Latency reporting is optional. A driver that lacks it, or rejects the
    // query, loses the diagnostic only; Reflex itself keeps running.
    [[nodiscard]] u32 QueryTimings(DX12NvidiaReflexFrameReport* out, u32 maxCount);

    // Why the last QueryTimings() returned what it did. Meaningful only right
    // after that call.
    [[nodiscard]] DX12NvidiaReflexLatencyReportStatus GetLatencyReportStatus() const noexcept
    {
        return LatencyReportStatus;
    }

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
    bool LatencyReportFailureLogged = false;
    DX12NvidiaReflexLatencyReportStatus LatencyReportStatus =
        DX12NvidiaReflexLatencyReportStatus::NotQueried;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_NVIDIA_REFLEX_H
