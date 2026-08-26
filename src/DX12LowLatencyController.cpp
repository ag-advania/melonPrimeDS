/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "DX12LowLatencyController.h"

#include "DX12Context.h"
#include "DX12Perf.h"
#include "Platform.h"

namespace melonDS
{

DX12LowLatencyController& DX12LowLatencyController::Get() noexcept
{
    // process-service: one instance per process, matching the D3D12 device
    // scope of the vendor sessions it owns.
    static DX12LowLatencyController instance;
    return instance;
}

DX12LowLatencyController* DX12LowLatencyController::GetIfActive() noexcept
{
    DX12LowLatencyController& instance = Get();
    return instance.Initialized ? &instance : nullptr;
}

void DX12LowLatencyController::Initialize(ID3D12Device* device, u32 vendorId)
{
    AmdAntiLag2.Initialize(device, vendorId);
    IntelXeLL.Initialize(device, vendorId);
    NvidiaReflex.Initialize(device, vendorId);
    IntelXeLLRequestedIntervalUs = 0;
    Initialized = true;
}

void DX12LowLatencyController::Shutdown() noexcept
{
    // Close the frame first. A vendor session torn down between a Begin and
    // its Finish leaves the driver correlating an interval that never ended.
    IntelXeLL.FinishFrame();
    IntelXeLL.Shutdown();
    AmdAntiLag2.Shutdown();
    NvidiaReflex.Shutdown();
    IntelXeLLRequestedIntervalUs = 0;
    PacingDecisionLogged = false;
    LastLoggedPacingDecision = {};
    QueueIdleHook = nullptr;
    QueueIdleUserData = nullptr;
    Initialized = false;
}

void DX12LowLatencyController::ApplySettings(const Settings& settings)
{
    AmdAntiLag2.SetEnabled(settings.AmdAntiLag2Enabled);
    IntelXeLLPacingPolicy =
        DX12IntelXeLLPacingPolicyFromConfig(settings.IntelXeLLPacingPolicy);
    IntelXeLLRequestedIntervalUs = 0;
    if (QueueIdleHook)
    {
        if (!QueueIdleHook(QueueIdleUserData))
        {
            Platform::Log(
                Platform::LogLevel::Error,
                "Intel XeLL state change skipped because the DX12 queue did not become idle\n");
        }
        else
        {
            IntelXeLL.SetSleepMode(settings.IntelXeLLEnabled, 0);
        }
    }
    NvidiaReflex.SetMode(settings.NvidiaReflexMode);
    DX12Perf::SetCounter(
        DX12Perf::Counter::DX12ReflexMode,
        static_cast<u64>(NvidiaReflex.GetMode()));
    LogPacingStateIfChanged();
}

void DX12LowLatencyController::UpdateFrameCap(std::uint32_t minimumIntervalUs)
{
    const DX12LowLatencyPacingDecision decision = GetPacingDecision();
    const std::uint32_t requestedInterval = decision.XeLLOwnsFrameCap
        ? minimumIntervalUs
        : 0;
    if (IntelXeLLRequestedIntervalUs == requestedInterval)
        return;

    const DX12IntelXeLLStatus status = IntelXeLL.GetStatus();
    if (!status.ContextCreated || !status.SleepModeApplied)
        return;

    if (!QueueIdleHook || !QueueIdleHook(QueueIdleUserData))
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "Intel XeLL frame-cap transition skipped because the DX12 queue did not become idle\n");
        return;
    }

    if (IntelXeLL.SetSleepMode(status.Requested, requestedInterval))
    {
        IntelXeLLRequestedIntervalUs = requestedInterval;
        LogPacingStateIfChanged();
    }
}

void DX12LowLatencyController::BeginFrame(u64 logicalFrameId)
{
    AmdAntiLag2.BeginFrame();
    LogPacingStateIfChanged();
    NvidiaReflex.BeginFrame(logicalFrameId);
    IntelXeLL.BeginFrame();
}

void DX12LowLatencyController::MarkInputSample()
{
    NvidiaReflex.MarkInputSample();
    IntelXeLL.MarkInputSample();
}

void DX12LowLatencyController::MarkSimulationStart()
{
    NvidiaReflex.MarkSimulationStart();
}

void DX12LowLatencyController::BeginRenderSubmit()
{
    IntelXeLL.MarkRenderSubmitStart();
    NvidiaReflex.MarkRenderSubmitStart();
}

void DX12LowLatencyController::EndRenderSubmit()
{
    IntelXeLL.MarkRenderSubmitEnd();
    NvidiaReflex.MarkRenderSubmitEnd();
}

void DX12LowLatencyController::EndRenderPhase()
{
    NvidiaReflex.EndRenderPhase();
    IntelXeLL.EndRenderPhase();
}

void DX12LowLatencyController::BeginPresent()
{
    NvidiaReflex.MarkPresentStart();
    IntelXeLL.MarkPresentStart();
}

void DX12LowLatencyController::EndPresent()
{
    NvidiaReflex.MarkPresentEnd();
    IntelXeLL.MarkPresentEnd();
}

void DX12LowLatencyController::FinishFrame()
{
    NvidiaReflex.FinishFrame();
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    ReportReflexLatencyTimings();
#endif
    IntelXeLL.FinishFrame();
}

DX12LowLatencyPacingDecision
DX12LowLatencyController::GetPacingDecision() const noexcept
{
    return ResolveDX12LowLatencyPacing(
        NvidiaReflex.IsActive(),
        AmdAntiLag2.IsActive(),
        IntelXeLL.IsActive(),
        IntelXeLLPacingPolicy);
}

void DX12LowLatencyController::LogPacingStateIfChanged()
{
    const DX12LowLatencyPacingDecision decision = GetPacingDecision();
    DX12Perf::SetCounter(
        DX12Perf::Counter::DX12VendorPacingAuthority,
        static_cast<u64>(decision.Authority));
    DX12Perf::SetCounter(
        DX12Perf::Counter::DX12ReflexMode,
        static_cast<u64>(NvidiaReflex.GetMode()));
    if (PacingDecisionLogged
        && decision.Authority == LastLoggedPacingDecision.Authority
        && decision.BypassHostLimiter == LastLoggedPacingDecision.BypassHostLimiter
        && decision.BypassPresentWait == LastLoggedPacingDecision.BypassPresentWait
        && decision.XeLLOwnsFrameCap == LastLoggedPacingDecision.XeLLOwnsFrameCap)
    {
        return;
    }

    LastLoggedPacingDecision = decision;
    PacingDecisionLogged = true;
    const DX12IntelXeLLStatus xell = IntelXeLL.GetStatus();
    const auto& profile = DX12Context::Get().GetDeviceProfile();
    Platform::Log(
        Platform::LogLevel::Info,
        "DX12 low-latency pacing adapter=\"%s\" vendor=%04X device=%04X driver=%016llX "
        "authority=%s xellPolicy=%s xellRequested=%d xellActual=%d "
        "minimumIntervalUs=%u hostLimiterBypass=%d frameLatencyWaitBypass=%d "
        "hardwareValidation=pending\n",
        profile.AdapterName.c_str(),
        profile.VendorId,
        profile.DeviceId,
        static_cast<unsigned long long>(profile.DriverVersion),
        DX12LowLatencyPacingAuthorityName(decision.Authority),
        DX12IntelXeLLPacingPolicyName(IntelXeLLPacingPolicy),
        xell.Requested ? 1 : 0,
        xell.ActualEnabled ? 1 : 0,
        xell.MinimumIntervalUs,
        decision.BypassHostLimiter ? 1 : 0,
        decision.BypassPresentWait ? 1 : 0);
}

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
void DX12LowLatencyController::ReportReflexLatencyTimings()
{
    if (!NvidiaReflex.IsAvailable())
        return;

    // Every 600th frame, matching VulkanPresenter::ReportLatencyTimings(), so a
    // long session leaves a readable handful of lines instead of a wall of
    // them. Developer builds only: this exists to prove the markers and the
    // Sleep call are landing, which is not something a shipping user needs.
    if (++ReflexLatencyTimingCountdown < 600)
        return;
    ReflexLatencyTimingCountdown = 0;

    DX12NvidiaReflexFrameReport reports[8]{};
    const melonDS::u32 count = NvidiaReflex.QueryTimings(reports, 8);
    if (count == 0)
    {
        // Not cosmetic. Reflex On costing nothing on this backend is only
        // believable if the driver is correlating frames; an empty report under
        // an active mode says the opposite.
        Platform::Log(Platform::LogLevel::Info,
            "NVIDIA Reflex timings: none (reason=%s mode=%d active=%d)\n",
            DX12NvidiaReflexLatencyReportStatusName(
                NvidiaReflex.GetLatencyReportStatus()),
            static_cast<int>(NvidiaReflex.GetMode()),
            NvidiaReflex.IsActive() ? 1 : 0);
        return;
    }

    // A non-zero frameID matching the ids the markers carried, together with
    // non-zero sim/submit/present stamps, is the end-to-end evidence that
    // NvAPI_D3D_SetLatencyMarker and NvAPI_D3D_Sleep were correlated by the
    // driver into one frame.
    const DX12NvidiaReflexFrameReport& r = reports[count - 1];
    Platform::Log(Platform::LogLevel::Info,
        "NVIDIA Reflex timings: mode=%d active=%d reports=%u frameID=%llu sim=%llu..%llu "
        "renderSubmit=%llu..%llu present=%llu..%llu gpuRender=%llu..%llu inputSample=%llu "
        "gpuActiveRenderUs=%u gpuFrameUs=%u\n",
        static_cast<int>(NvidiaReflex.GetMode()),
        NvidiaReflex.IsActive() ? 1 : 0,
        static_cast<unsigned>(count),
        static_cast<unsigned long long>(r.FrameId),
        static_cast<unsigned long long>(r.SimStartTimeUs),
        static_cast<unsigned long long>(r.SimEndTimeUs),
        static_cast<unsigned long long>(r.RenderSubmitStartTimeUs),
        static_cast<unsigned long long>(r.RenderSubmitEndTimeUs),
        static_cast<unsigned long long>(r.PresentStartTimeUs),
        static_cast<unsigned long long>(r.PresentEndTimeUs),
        static_cast<unsigned long long>(r.GpuRenderStartTimeUs),
        static_cast<unsigned long long>(r.GpuRenderEndTimeUs),
        static_cast<unsigned long long>(r.InputSampleTimeUs),
        static_cast<unsigned>(r.GpuActiveRenderTimeUs),
        static_cast<unsigned>(r.GpuFrameTimeUs));
}
#endif

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
