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

void DX12Renderer::AllocCapture(u32 bank, u32 start, u32 len)
{
    SoftRenderer::AllocCapture(bank, start, len);
}

CaptureSyncResult DX12Renderer::SyncVRAMCapture(
    u32 bank, u32 start, u32 len, bool complete)
{
    (void)complete;
    CaptureBlockProvenance provenance{};
    if (!GetCaptureProvenanceForRange(bank, start, len, provenance))
    {
        if (auto* dx12 = GetDX12Renderer3D())
            dx12->FailNativeGPU2DExact(
                "native DX12 GPU2D capture provenance range is inconsistent");
        return CaptureSyncResult::Failed;
    }

    if (IsNativeCaptureOwner(provenance.Owner))
    {
        // Display Capture ownership outlives the FrameRecorder that produced
        // it. This source decision intentionally does not inspect whether the
        // current emulated frame has finalized its native recorder.
        if (provenance.Owner != CaptureOwner::NativeDX12)
        {
            if (auto* dx12 = GetDX12Renderer3D())
                dx12->FailNativeGPU2DExact(
                    "native DX12 GPU2D capture owner belongs to another backend");
            return CaptureSyncResult::Failed;
        }

        auto* dx12 = GetDX12Renderer3D();
        if (!dx12
            || !dx12->ReadNativeCapture(
                bank, start, len, provenance, GPU.VRAM[bank]))
        {
            if (dx12)
                dx12->FailNativeGPU2DExact(
                    "native DX12 GPU2D capture readback failed");
            return CaptureSyncResult::Failed;
        }

        const u32 blockCount = len == 0u ? 1u : std::min<u32>(len, 3u);
        for (u32 i = 0; i < blockCount; ++i)
        {
            const u32 block = (start + i) & 3u;
            for (u32 subblock = 0; subblock < 64u; ++subblock)
                GPU.VRAMDirty[bank][block * 64u + subblock] = true;
        }
        MarkCaptureCpuCoherent(bank, start, len);
        GPU.RecordGPU2DCaptureSync(bank, start, len);
        return CaptureSyncResult::Synchronized;
    }

    // None/CpuCoherent means the CPU mirror is authoritative. The software
    // hook is a no-op by design; it is not a correctness fallback for a
    // native-owned block.
    return SoftRenderer::SyncVRAMCapture(bank, start, len, complete);
}

void DX12Renderer::InvalidateVRAMCapture(u32 bank, u32 start, u32 len)
{
    SoftRenderer::InvalidateVRAMCapture(bank, start, len);
}

NativeCaptureStateIdentity DX12Renderer::GetNativeCaptureStateIdentity() const noexcept
{
    const auto* dx12 = GetDX12Renderer3D();
    return dx12
        ? dx12->GetNativeCaptureStateIdentity(CaptureOwner::NativeDX12)
        : NativeCaptureStateIdentity{};
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
    NativeGPU2DAnnounced = false;
    NativeGPU2DFallbackAnnounced = false;
    NativeGPU2DStartupFallbackAnnounced = false;

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
    const auto& fallback = GetGPU2DFallbackCounters();
    Platform::Log(Platform::LogLevel::Info,
        "[GPU2DFallbackCounters] backend=DX12 startup_pipeline_fallback=%llu "
        "runtime_native_unavailable_fallback=%llu capture_software_fallback=%llu "
        "stale_generation_reject=%llu structured_fallback=%llu\n",
        static_cast<unsigned long long>(fallback.startup_pipeline_fallback),
        static_cast<unsigned long long>(fallback.runtime_native_unavailable_fallback),
        static_cast<unsigned long long>(fallback.capture_software_fallback),
        static_cast<unsigned long long>(fallback.stale_generation_reject),
        static_cast<unsigned long long>(fallback.structured_fallback));
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
    DX12Perf::SetCounter(
        DX12Perf::Counter::DX12ReflexMode,
        static_cast<u64>(NvidiaReflex.GetMode()));
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
    bool nativeComposed = false;
    GPU2DComposeResult nativeComposeResult = GPU2DComposeResult::Unavailable;
    const bool nativeProducer = UsesNativeGPU2DProducerForFrame();
    const bool exactValidation = GPU2DNative::ExactValidationEnabled();
    if (dx12 && HasNativeGPU2DFrameForCurrentEmulatedFrame())
    {
        const GPU2DNative::FrameInput& nativeFrame = GetNativeGPU2DFrame();
        nativeComposed = dx12->ComposeNativeGPU2D(
            nativeFrame,
            nativeFrame.Generation.Frame,
            !GPU.GPU3D.AbortFrame && dx12->HasFinalFBContent(),
            exactValidation ? GetSoftwareScreenFrame(0u) : nullptr,
            exactValidation ? GetSoftwareScreenFrame(1u) : nullptr);
        nativeComposeResult = dx12->GetLastComposeResult();
        if (nativeComposeResult == GPU2DComposeResult::Success
            || nativeComposeResult == GPU2DComposeResult::SemanticOnly)
        {
            // Publish physical capture ownership after semantic submission,
            // even when presentation backpressure retained the old visible
            // frame. The next frame may request a read before its recorder is
            // finalized, and must still select this native mirror.
            PublishNativeCaptureProvenance(
                CaptureOwner::NativeDX12,
                nativeFrame,
                GetNativeCaptureStateIdentity());
        }
        if (nativeComposed)
        {
            if (!NativeGPU2DAnnounced)
            {
                Platform::Log(Platform::LogLevel::Info,
                    "DX12 renderer gpu2d=DX12 gpu3d=DX12 fallback=0\n");
                NativeGPU2DAnnounced = true;
            }
        }
        else if (!nativeProducer
            || nativeComposeResult == GPU2DComposeResult::Fatal)
        {
            RecordGPU2DRuntimeNativeUnavailableFallback();
            DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DFallbackFrames);
            if (!NativeGPU2DFallbackAnnounced)
            {
                Platform::Log(Platform::LogLevel::Warn,
                    "DX12 renderer gpu2d=Software fallback=1 reason=native dispatch unavailable\n");
                NativeGPU2DFallbackAnnounced = true;
            }
        }
    }
    else if (dx12 && HasNativeGPU2DFrame())
    {
        RecordGPU2DStaleGenerationReject();
        Platform::Log(Platform::LogLevel::Warn,
            "DX12 renderer gpu2d=Software fallback=1 reason=stale_generation_reject "
            "stale_generation_reject=1\n");
    }
    if (nativeProducer && !nativeComposed)
    {
        if (nativeComposeResult == GPU2DComposeResult::Backpressure
            || nativeComposeResult == GPU2DComposeResult::SemanticOnly
            || nativeComposeResult == GPU2DComposeResult::Unavailable)
        {
            IntelXeLL.MarkRenderSubmitEnd();
            NvidiaReflex.MarkRenderSubmitEnd();
            return;
        }
        // Native ownership means no CPU structured frame was produced for this
        // generation. Do not present a previous frame or silently switch to
        // Software after a native submission/drop failure.
        if (dx12 && !dx12->HasRuntimeFailure())
            dx12->FailNativeGPU2DExact(
                "native GPU2D producer could not publish its owned frame");
        IntelXeLL.MarkRenderSubmitEnd();
        NvidiaReflex.MarkRenderSubmitEnd();
        return;
    }
    if (dx12 && !nativeComposed && dx12->HasRuntimeFailure())
    {
        Platform::Log(Platform::LogLevel::Error,
            "DX12 renderer gpu2d=Software fallback=1 disabled=1 reason=%s\n",
            dx12->GetRuntimeFailureReason().c_str());
        IntelXeLL.MarkRenderSubmitEnd();
        NvidiaReflex.MarkRenderSubmitEnd();
        return;
    }
    if (exactValidation && dx12 && !nativeComposed
        && nativeComposeResult != GPU2DComposeResult::Backpressure
        && nativeComposeResult != GPU2DComposeResult::SemanticOnly
        && nativeComposeResult != GPU2DComposeResult::Unavailable)
    {
        dx12->FailNativeGPU2DExact(
            "native GPU2D exact gate rejected a fallback or unavailable frame");
        IntelXeLL.MarkRenderSubmitEnd();
        NvidiaReflex.MarkRenderSubmitEnd();
        return;
    }
    if (dx12 && !nativeComposed && !NativeGPU2DFallbackAnnounced)
    {
        RecordGPU2DRuntimeNativeUnavailableFallback();
        if (GPU.CaptureEnable && !nativeProducer)
            RecordGPU2DCaptureSoftwareFallback();
        DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DFallbackFrames);
        Platform::Log(Platform::LogLevel::Warn,
            "DX12 renderer gpu2d=Software fallback=1 reason=native frame unavailable\n");
        NativeGPU2DFallbackAnnounced = true;
    }
    if (nativeComposeResult == GPU2DComposeResult::Backpressure
        || nativeComposeResult == GPU2DComposeResult::SemanticOnly)
    {
        IntelXeLL.MarkRenderSubmitEnd();
        NvidiaReflex.MarkRenderSubmitEnd();
        return;
    }
    StructuredVulkanFrameView view{};
    if (!nativeComposed && (!dx12 || !GetStructuredVulkanFrame(view) || !view.Valid))
    {
        IntelXeLL.MarkRenderSubmitEnd();
        NvidiaReflex.MarkRenderSubmitEnd();
        return;
    }

    if (nativeComposed)
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
    RecordGPU2DStructuredFallback();
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
        if (!NativeGPU2DStartupFallbackAnnounced)
        {
            RecordGPU2DStartupPipelineFallback();
            Platform::Log(Platform::LogLevel::Warn,
                "requested=DX12 actual=DX12 gpu2d=Software gpu3d=DX12 "
                "fallback=1 startupFallback=1 reason=pipeline compilation\n");
            NativeGPU2DStartupFallbackAnnounced = true;
        }
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
    if (!NativeGPU2DStartupFallbackAnnounced)
    {
        RecordGPU2DStartupPipelineFallback();
        Platform::Log(Platform::LogLevel::Warn,
            "requested=DX12 actual=DX12 gpu2d=Software gpu3d=DX12 "
            "fallback=1 startupFallback=1 reason=pipeline compilation\n");
        NativeGPU2DStartupFallbackAnnounced = true;
    }
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

bool DX12Renderer::CanUseNativeGPU2DForFrame() const noexcept
{
    const auto* dx12 = GetDX12Renderer3D();
    return dx12 && dx12->CanComposeNativeGPU2D();
}

void DX12Renderer::BeginReflexFrame(melonDS::u64 logicalFrameId)
{
    NvidiaReflex.BeginFrame(logicalFrameId);
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

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
