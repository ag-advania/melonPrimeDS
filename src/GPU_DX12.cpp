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

#include "GPU_DX12.h"

#include "DX12Context.h"
#include "DX12Perf.h"
#include "GPU3D_DX12.h"
#include "NDS.h"
#include "Platform.h"

namespace melonDS
{

DX12Renderer::DX12Renderer(melonDS::NDS& nds)
    : SoftRenderer(nds)
{
    if (RasterDifferential::Enabled())
        DifferentialReference = std::move(Rend3D);

    // Replaces the SoftRenderer3D the base constructor installed. A null result
    // leaves Rend3D empty, which Init() reports as a failure so the frontend can
    // fall back to Software.
    if (auto renderer3D = DX12Renderer3D::New(GPU.GPU3D))
        Rend3D = std::move(renderer3D);
    else
        Rend3D.reset();
}

DX12Renderer::~DX12Renderer()
{
    if (auto* dx12 = GetDX12Renderer3D())
        dx12->WaitForQueueIdle();
    IntelXeLL.Shutdown();
}

bool DX12Renderer::Init()
{
    if (!Rend3D || !Rend3D->Init())
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "DX12 renderer init failed stage=3D-device actual=Software\n");
        return false;
    }
    if (DifferentialReference)
    {
        DifferentialReference->Reset();
        DifferentialState.Reset();
    }

    auto& context = DX12Context::Get();
    AmdAntiLag2.Initialize(context.GetDevice(), context.GetDeviceProfile().VendorId);
    IntelXeLL.Initialize(context.GetDevice(), context.GetDeviceProfile().VendorId);
    NvidiaReflex.Initialize(context.GetDevice(), context.GetDeviceProfile().VendorId);

    Platform::Log(
        Platform::LogLevel::Info,
        "DX12 renderer init succeeded requested=DX12 actual=DX12 presentation=high-resolution-composed\n");
    return true;
}

void DX12Renderer::Stop()
{
    FinishIntelXeLLFrame();
    if (auto* dx12 = GetDX12Renderer3D())
        dx12->WaitForQueueIdle();
    IntelXeLL.Shutdown();
    AmdAntiLag2.Shutdown();
    NvidiaReflex.Shutdown();
    if (auto* dx12 = GetDX12Renderer3D())
        dx12->Stop();
    SoftRenderer::Stop();
}

void DX12Renderer::PreSavestate()
{
    // The DX12 renderer owns its own GPU synchronization and has no software
    // render thread to suspend, so a savestate needs nothing here.
}

void DX12Renderer::PostSavestate()
{
    // OpenGL resets all renderer-private state after savestate I/O. Do the
    // same here: FinalFB, the high-resolution capture sidecar and structured
    // 2D capture references are derived caches, not serialized DS state.
    SoftRenderer::Reset();
    if (DifferentialReference)
    {
        DifferentialReference->Reset();
        DifferentialState.Reset();
    }
}

void DX12Renderer::SetRenderSettings(RendererSettings& settings)
{
    if (auto* dx12 = GetDX12Renderer3D())
    {
        // DX12 rasterizes the original DS polygons as scanline spans. Better
        // Polygons is a triangle-splitting workaround for raster backends and
        // is intentionally not part of the DX12 renderer contract.
        dx12->SetRenderSettings(settings.ScaleFactor, settings.HiresCoordinates);
    }
    AmdAntiLag2.SetEnabled(settings.AmdAntiLag2Enabled);
    IntelXeLLPacingPolicy = DX12IntelXeLLPacingPolicyFromConfig(
        settings.IntelXeLLPacingPolicy);
    IntelXeLLRequestedIntervalUs = 0;
    if (auto* dx12 = GetDX12Renderer3D())
    {
        if (!dx12->WaitForQueueIdle())
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
    LogLowLatencyPacingStateIfChanged();
}

void DX12Renderer::Start3DRendering()
{
    IntelXeLL.MarkRenderSubmitStart();
    NvidiaReflex.MarkRenderSubmitStart();
    Renderer::Start3DRendering();
    if (DifferentialReference)
    {
        // DX12's texture cache owns the destructive VRAM dirty snapshot. The
        // software oracle runs only after those flat mirrors are coherent.
        static_cast<SoftRenderer3D*>(DifferentialReference.get())->RenderReferenceFrame();
    }
}

void DX12Renderer::VBlank()
{
    auto* dx12 = GetDX12Renderer3D();
    StructuredVulkanFrameView view{};
    if (!dx12 || !GetStructuredVulkanFrame(view) || !view.Valid)
    {
        IntelXeLL.MarkRenderSubmitEnd();
        NvidiaReflex.MarkRenderSubmitEnd();
        return;
    }

    const std::array<const u32*, 14> planes = {
        view.Plane[0][0],
        view.Plane[0][1],
        view.Plane[0][2],
        view.Plane[0][3],
        view.Plane[1][0],
        view.Plane[1][1],
        view.Plane[1][2],
        view.Plane[1][3],
        view.CaptureSourcePlane[0],
        view.CaptureSourcePlane[1],
        view.CaptureSourcePlane[2],
        view.CaptureSourcePlane[3],
        view.CaptureSourceBNative,
        view.CaptureSourceBReference,
    };
    const std::array<const u32*, 2> lineMeta = {
        view.LineMeta[0],
        view.LineMeta[1],
    };
    const bool composed = dx12->ComposeStructuredOutput(
        planes, lineMeta, view.CaptureCommands, view.ScreenRouting, view.Generation,
        view.ContentGeneration);
    DX12Perf::AddCounter(
        DX12Perf::Counter::StructuredScreenRouteCopyBytes,
        view.ScreenRouteCopyBytes);
    DX12Perf::AddCounter(
        DX12Perf::Counter::StructuredScreenRouteCopyNanoseconds,
        view.ScreenRouteCopyNanoseconds);
    DX12Perf::AddCounter(
        DX12Perf::Counter::StructuredRegularLines,
        view.StructuredRegularLines);
    DX12Perf::AddCounter(
        DX12Perf::Counter::StructuredFallbackLines,
        view.StructuredFallbackLines);
    if (composed && DifferentialReference && dx12->GetScaleFactor() == 1)
    {
        const bool exact = DifferentialState.CompareFrame(
            *Rend3D, *DifferentialReference, "DX12");
        if (!exact)
        {
            Platform::Log(
                Platform::LogLevel::Error,
                "[RasterDiffState] backend=DX12 dispCnt=%08X polygons=%u "
                "clear1=%08X clear2=%08X\n",
                GPU.GPU3D.RenderDispCnt,
                GPU.GPU3D.RenderNumPolygons,
                GPU.GPU3D.RenderClearAttr1,
                GPU.GPU3D.RenderClearAttr2);
        }
    }
    IntelXeLL.MarkRenderSubmitEnd();
    NvidiaReflex.MarkRenderSubmitEnd();
}

RendererOutput DX12Renderer::GetOutput()
{
    auto* dx12 = GetDX12Renderer3D();
    if (!dx12)
        return {};

    RendererOutput output = dx12->GetComposedOutput();
    if (output.Kind == RendererOutputKind::None)
    {
        // The DX12 pipelines compile incrementally after a ROM starts. Until
        // the first VBlank can publish a composed frame, keep the native Qt
        // panel on the initialized software buffers instead of making it draw
        // its as-yet-uninitialized cached images. Metal uses the same fallback
        // during its output transition.
        return SoftRenderer::GetOutput();
    }

    return output;
}

RendererOutputLease DX12Renderer::AcquireOutputLease()
{
    auto* dx12 = GetDX12Renderer3D();
    if (!dx12)
        return {};

    RendererOutputLease lease = dx12->AcquireComposedOutputLease();
    if (lease.Output.Kind != RendererOutputKind::None)
        return lease;
    return RendererOutputLease(SoftRenderer::GetOutput(), nullptr, nullptr);
}

bool DX12Renderer::NeedsShaderCompile()
{
    return Rend3D && Rend3D->NeedsShaderCompile();
}

void DX12Renderer::ShaderCompileStep(int& current, int& count)
{
    if (Rend3D)
        Rend3D->ShaderCompileStep(current, count);
}

bool DX12Renderer::HasRuntimeFailure() const noexcept
{
    const auto* dx12 = GetDX12Renderer3D();
    return dx12 && dx12->HasRuntimeFailure();
}

const std::string& DX12Renderer::GetRuntimeFailureReason() const noexcept
{
    static const std::string empty;
    const auto* dx12 = GetDX12Renderer3D();
    return dx12 ? dx12->GetRuntimeFailureReason() : empty;
}

DX12Renderer3D* DX12Renderer::GetDX12Renderer3D() noexcept
{
    return dynamic_cast<DX12Renderer3D*>(Rend3D.get());
}

const DX12Renderer3D* DX12Renderer::GetDX12Renderer3D() const noexcept
{
    return dynamic_cast<const DX12Renderer3D*>(Rend3D.get());
}

void DX12Renderer::BeginReflexFrame()
{
    NvidiaReflex.BeginFrame();
}

void DX12Renderer::BeginAmdAntiLag2Frame()
{
    AmdAntiLag2.BeginFrame();
    LogLowLatencyPacingStateIfChanged();
}

void DX12Renderer::BeginIntelXeLLFrame()
{
    IntelXeLL.BeginFrame();
}

void DX12Renderer::MarkReflexInputSample()
{
    NvidiaReflex.MarkInputSample();
}

void DX12Renderer::MarkIntelXeLLInputSample()
{
    IntelXeLL.MarkInputSample();
}

void DX12Renderer::MarkReflexSimulationStart()
{
    NvidiaReflex.MarkSimulationStart();
}

void DX12Renderer::EndReflexRenderPhase()
{
    NvidiaReflex.EndRenderPhase();
}

void DX12Renderer::EndIntelXeLLRenderPhase()
{
    IntelXeLL.EndRenderPhase();
}

void DX12Renderer::BeginReflexPresent()
{
    NvidiaReflex.MarkPresentStart();
}

void DX12Renderer::EndReflexPresent()
{
    NvidiaReflex.MarkPresentEnd();
}

void DX12Renderer::BeginIntelXeLLPresent()
{
    IntelXeLL.MarkPresentStart();
}

void DX12Renderer::EndIntelXeLLPresent()
{
    IntelXeLL.MarkPresentEnd();
}

void DX12Renderer::FinishReflexFrame()
{
    NvidiaReflex.FinishFrame();
}

void DX12Renderer::FinishIntelXeLLFrame()
{
    IntelXeLL.FinishFrame();
}

void DX12Renderer::UpdateIntelXeLLFrameCap(std::uint32_t minimumIntervalUs)
{
    const DX12LowLatencyPacingDecision decision = GetLowLatencyPacingDecision();
    const std::uint32_t requestedInterval = decision.XeLLOwnsFrameCap
        ? minimumIntervalUs
        : 0;
    if (IntelXeLLRequestedIntervalUs == requestedInterval)
        return;

    const DX12IntelXeLLStatus status = IntelXeLL.GetStatus();
    if (!status.ContextCreated || !status.SleepModeApplied)
        return;

    auto* dx12 = GetDX12Renderer3D();
    if (!dx12 || !dx12->WaitForQueueIdle())
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "Intel XeLL frame-cap transition skipped because the DX12 queue did not become idle\n");
        return;
    }

    if (IntelXeLL.SetSleepMode(status.Requested, requestedInterval))
    {
        IntelXeLLRequestedIntervalUs = requestedInterval;
        LogLowLatencyPacingStateIfChanged();
    }
}

DX12LowLatencyPacingDecision DX12Renderer::GetLowLatencyPacingDecision() const noexcept
{
    return ResolveDX12LowLatencyPacing(
        NvidiaReflex.IsActive(),
        AmdAntiLag2.IsActive(),
        IntelXeLL.IsActive(),
        IntelXeLLPacingPolicy);
}

void DX12Renderer::LogLowLatencyPacingStateIfChanged()
{
    const DX12LowLatencyPacingDecision decision = GetLowLatencyPacingDecision();
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

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
