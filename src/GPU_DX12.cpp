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
#include "DX12LowLatencyController.h"
#include "DX12Perf.h"
#include "GPU2DFramePolicy.h"
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
    DX12LowLatencyController::Get().Shutdown();
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
        MarkCaptureCpuCoherent(
            bank, start, len,
            CaptureAuthorityTransitionReason::NativeReadbackMaterialized);
        GPU.RecordGPU2DCaptureSync(bank, start, len);
        return CaptureSyncResult::Synchronized;
    }

    // None/CpuCoherent means the CPU mirror is authoritative. The software
    // hook is a no-op by design; it is not a correctness fallback for a
    // native-owned block.
    return SoftRenderer::SyncVRAMCapture(bank, start, len, complete);
}

void DX12Renderer::InvalidateVRAMCapture(
    u32 bank,
    u32 start,
    u32 len,
    CaptureAuthorityTransitionReason reason)
{
    if (reason == CaptureAuthorityTransitionReason::CpuWrite
        || reason == CaptureAuthorityTransitionReason::CaptureRetired)
    {
        if (auto* dx12 = GetDX12Renderer3D())
        {
            dx12->InvalidateHighResCaptureRange(
                bank,
                start,
                len,
                reason == CaptureAuthorityTransitionReason::CpuWrite
                    ? GPU2DNative::HighResCaptureFallbackReason::CpuWriteInvalidated
                    : GPU2DNative::HighResCaptureFallbackReason::CaptureRetired);
        }
    }
    SoftRenderer::InvalidateVRAMCapture(bank, start, len, reason);
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
    auto& latency = DX12LowLatencyController::Get();
    latency.SetQueueIdleHook(&DX12Renderer::WaitForQueueIdleHook, this);
    latency.Initialize(context.GetDevice(), context.GetDeviceProfile().VendorId);

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
    if (auto* dx12 = GetDX12Renderer3D())
        dx12->WaitForQueueIdle();
    DX12LowLatencyController::Get().Shutdown();
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
    // 2D capture references are derived caches, not serialized DS state. They
    // are reset by RebuildAfterSavestateLoad(), after GPU.cpp invalidates
    // restored capture authority and before the next scanline executes.
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
    // Configuration acquisition only: the values are forwarded to the
    // low-latency controller, which decides what the hardware can honour.
    DX12LowLatencyController::Settings lowLatency;
    lowLatency.NvidiaReflexMode = settings.NvidiaReflexMode;
    lowLatency.AmdAntiLag2Enabled = settings.AmdAntiLag2Enabled;
    lowLatency.IntelXeLLEnabled = settings.IntelXeLLEnabled;
    lowLatency.IntelXeLLPacingPolicy = settings.IntelXeLLPacingPolicy;
    DX12LowLatencyController::Get().ApplySettings(lowLatency);
}

bool DX12Renderer::WaitForQueueIdleHook(void* userData) noexcept
{
    auto* self = static_cast<DX12Renderer*>(userData);
    if (!self)
        return false;
    auto* dx12 = self->GetDX12Renderer3D();
    return dx12 && dx12->WaitForQueueIdle();
}

void DX12Renderer::Start3DRendering()
{
    if (auto* latency = DX12LowLatencyController::GetIfActive())
        latency->BeginRenderSubmit();
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
    struct CoverageLogScope
    {
        SoftRenderer* Renderer;
        bool Published = false;
        const char* Source = "retained_last_complete";

        ~CoverageLogScope()
        {
            Renderer->LogGPU2DFrameCoverage(Published, Source);
        }
    } coverage{this};

    // Start3DRendering() opened the render-submit interval; every exit from
    // this function closes it, including the early returns below. A scope
    // guard is the only way to keep that true as the publication policy
    // changes shape.
    struct RenderSubmitScope
    {
        DX12LowLatencyController* Latency;

        ~RenderSubmitScope()
        {
            if (Latency)
                Latency->EndRenderSubmit();
        }
    } renderSubmit{DX12LowLatencyController::GetIfActive()};

    // Which of this frame's two candidate images reaches the screen is not a
    // D3D12 question. GPU2DFramePolicy owns it, and Vulkan asks the same
    // question with the same facts; only the log tags and perf counters below
    // are backend-specific.
    auto* dx12 = GetDX12Renderer3D();

    GPU2DFramePolicy::FrameFacts facts;
    facts.HasNativeRenderer = dx12 != nullptr;
    facts.HasNativeFrameForCurrentEmulatedFrame =
        HasNativeGPU2DFrameForCurrentEmulatedFrame();
    facts.HasNativeFrame = HasNativeGPU2DFrame();
    facts.NativeProducer = UsesNativeGPU2DProducerForFrame();
    facts.ExactValidationEnabled = GPU2DNative::ExactValidationEnabled();
    facts.CaptureEnabled = GPU.CaptureEnable;
    facts.FallbackAlreadyAnnounced = NativeGPU2DFallbackAnnounced;

    if (GPU2DFramePolicy::ShouldAttemptNativeCompose(
            facts.HasNativeRenderer,
            facts.HasNativeFrameForCurrentEmulatedFrame))
    {
        const GPU2DNative::FrameInput& nativeFrame = GetNativeGPU2DFrame();
        facts.NativeComposed = dx12->ComposeNativeGPU2D(
            nativeFrame,
            nativeFrame.Generation.Frame,
            !GPU.GPU3D.AbortFrame && dx12->HasFinalFBContent(),
            facts.ExactValidationEnabled ? GetSoftwareScreenFrame(0u) : nullptr,
            facts.ExactValidationEnabled ? GetSoftwareScreenFrame(1u) : nullptr);
        facts.ComposeResult = dx12->GetLastComposeResult();
        if (GPU2DFramePolicy::ShouldPublishCaptureProvenance(facts.ComposeResult))
        {
            PublishNativeCaptureProvenance(
                CaptureOwner::NativeDX12,
                nativeFrame,
                GetNativeCaptureStateIdentity());
        }
    }
    facts.RendererHasRuntimeFailure = dx12 && dx12->HasRuntimeFailure();

    const GPU2DFramePolicy::Decision decision = GPU2DFramePolicy::Evaluate(facts);

    if (facts.NativeComposed)
    {
        coverage.Published = true;
        coverage.Source = "native";
        // The 3D oracle runs on this path too. It compares the two Renderer3D
        // outputs, which is independent of the 2D composition that just
        // happened, and native composition is the only path a normal session
        // takes -- so leaving it to the structured branch meant the harness
        // never saw a frame.
        CompareRasterDifferentialFrame();
    }
    if (decision.AnnounceNativeSuccess && !NativeGPU2DAnnounced)
    {
        Platform::Log(Platform::LogLevel::Info,
            "DX12 renderer gpu2d=DX12 gpu3d=DX12 fallback=0\n");
        NativeGPU2DAnnounced = true;
    }
    if (decision.RecordStaleGenerationReject)
    {
        RecordGPU2DStaleGenerationReject();
        Platform::Log(Platform::LogLevel::Warn,
            "DX12 renderer gpu2d=Software fallback=1 reason=stale_generation_reject "
            "stale_generation_reject=1\n");
    }
    if (decision.RecordRuntimeNativeUnavailableFallback)
        RecordGPU2DRuntimeNativeUnavailableFallback();
    if (decision.RecordCaptureSoftwareFallback)
        RecordGPU2DCaptureSoftwareFallback();
    if (decision.CountNativeFallbackFrame)
        DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DFallbackFrames);
    if (decision.AnnounceFallback != GPU2DFramePolicy::FallbackReason::None)
    {
        Platform::Log(Platform::LogLevel::Warn,
            "DX12 renderer gpu2d=Software fallback=1 reason=%s\n",
            GPU2DFramePolicy::FallbackReasonText(decision.AnnounceFallback));
        NativeGPU2DFallbackAnnounced = true;
    }

    switch (decision.Result)
    {
    case GPU2DFramePolicy::Outcome::NativePublished:
    case GPU2DFramePolicy::Outcome::RetainLastFrame:
        return;
    case GPU2DFramePolicy::Outcome::FailNativeExact:
        if (dx12)
            dx12->FailNativeGPU2DExact(decision.FailureReason);
        return;
    case GPU2DFramePolicy::Outcome::ReportRuntimeFailure:
        Platform::Log(Platform::LogLevel::Error,
            "DX12 renderer gpu2d=Software fallback=1 disabled=1 reason=%s\n",
            dx12->GetRuntimeFailureReason().c_str());
        return;
    case GPU2DFramePolicy::Outcome::TryStructuredFallback:
        break;
    }

    StructuredVulkanFrameView view{};
    if (!dx12 || !GetStructuredVulkanFrame(view)
        || !GPU2DFramePolicy::ShouldComposeStructuredFrame(
            view.Valid,
            view.CompleteCoverage,
            view.ResumeFrameDiscontinuous,
            GPU2DNative::DropDiscontinuousSavestateFrameEnabled()))
    {
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
    if (composed)
    {
        coverage.Published = true;
        coverage.Source = "structured";
    }
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
    if (composed)
        CompareRasterDifferentialFrame();
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

void DX12Renderer::CompareRasterDifferentialFrame()
{
    auto* dx12 = GetDX12Renderer3D();
    if (!DifferentialReference || !dx12)
        return;
    // The oracle renders at native resolution, so a scaled frame has nothing
    // to compare against.
    if (dx12->GetScaleFactor() != 1)
        return;
    if (DifferentialState.CompareFrame(*Rend3D, *DifferentialReference, "DX12"))
        return;
    Platform::Log(
        Platform::LogLevel::Error,
        "[RasterDiffState] backend=DX12 dispCnt=%08X polygons=%u "
        "clear1=%08X clear2=%08X\n",
        GPU.GPU3D.RenderDispCnt,
        GPU.GPU3D.RenderNumPolygons,
        GPU.GPU3D.RenderClearAttr1,
        GPU.GPU3D.RenderClearAttr2);
}

bool DX12Renderer::CanUseNativeGPU2DForFrame() const noexcept
{
    const auto* dx12 = GetDX12Renderer3D();
    return dx12 && dx12->CanComposeNativeGPU2D();
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
