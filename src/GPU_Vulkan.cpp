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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "GPU_Vulkan.h"

#include <array>

#include "GPU3D_Vulkan.h"
#include "NDS.h"
#include "Platform.h"
#include "VulkanPerf.h"

namespace melonDS
{

VulkanRenderer::VulkanRenderer(melonDS::NDS& nds)
    : SoftRenderer(nds)
{
    if (RasterDifferential::Enabled())
        DifferentialReference = std::move(Rend3D);

    // Replaces the SoftRenderer3D the base constructor installed. A null result
    // leaves Rend3D empty, which Init() reports as a failure so the frontend can
    // fall back to Software with a truthful reason instead of running a dead
    // renderer.
    if (auto renderer3D = VulkanRenderer3D::New(GPU.GPU3D))
        Rend3D = std::move(renderer3D);
    else
        Rend3D.reset();
}

VulkanRenderer::~VulkanRenderer() = default;

bool VulkanRenderer::Init()
{
    if (!Rend3D || !Rend3D->Init())
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "Vulkan renderer init failed stage=3D-device actual=Software\n");
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

    Platform::Log(
        Platform::LogLevel::Info,
        "Vulkan renderer init succeeded requested=Vulkan actual=Vulkan\n");
    return true;
}

void VulkanRenderer::Stop()
{
    if (auto* vulkan = GetVulkanRenderer3D())
        vulkan->Stop();
    SoftRenderer::Stop();
}

void VulkanRenderer::PreSavestate()
{
    // SoftRenderer::PreSavestate() suspends the software 3D render thread
    // through an unchecked dynamic_cast to SoftRenderer3D. Rend3D is a
    // VulkanRenderer3D here, so that cast would return null and the base
    // implementation would dereference it.
    //
    // There is nothing to do instead: the Vulkan renderer owns its own GPU
    // synchronization and has no software render thread to suspend.
}

void VulkanRenderer::PostSavestate()
{
    // Match GLRenderer's savestate lifecycle. Renderer-private images,
    // high-resolution capture sidecars and structured 2D provenance are not
    // serialized, so none of them may survive across a loaded GPU state (or a
    // save that synchronized native VRAM captures). VulkanRenderer3D::Reset()
    // retires in-flight texture-cache resources without a device-wide idle.
    SoftRenderer::Reset();
    if (DifferentialReference)
    {
        DifferentialReference->Reset();
        DifferentialState.Reset();
    }
}

void VulkanRenderer::SetRenderSettings(RendererSettings& settings)
{
    // Same reason as PreSavestate(): the base implementation casts Rend3D to
    // SoftRenderer3D to forward the `Threaded` option, which does not apply.
    //
    // The two low-latency fields are recorded here as the renderer-owned policy
    // source. The emulation thread reads them back through
    // GetNvidiaReflexMode() / GetAmdAntiLag2Enabled() and hands them to both
    // the presenter pacing path and the screen panel's slot-admission policy.
    // The vendor extensions remain capability-gated by the presenter.
    NvidiaReflexMode = settings.NvidiaReflexMode;
    AmdAntiLag2Enabled = settings.AmdAntiLag2Enabled;

    if (auto* vulkan = GetVulkanRenderer3D())
    {
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (RasterDifferential::Enabled())
            settings.ScaleFactor = 1;
#endif

        // BetterPolygons is a triangle-splitting workaround for the classic
        // OpenGL/native Metal raster paths. Vulkan follows GPU3D_Compute and
        // rasterizes each DS polygon directly as scanline spans, so only the
        // scale and coordinate-mode settings apply.
        vulkan->SetRenderSettings(settings.ScaleFactor, settings.HiresCoordinates);
    }
}

void VulkanRenderer::Start3DRendering()
{
    // Renderer::Start3DRendering() drives Rend3D->RenderFrame(). Overridden
    // explicitly so the phase-13 Reflex render-submit marker has an owner and
    // so the call does not depend on which base happens to define it.
    Renderer::Start3DRendering();
    if (DifferentialReference)
    {
        // Vulkan's texture cache must be the sole destructive dirty-state
        // consumer. The software oracle then renders from the coherent mirrors.
        static_cast<SoftRenderer3D*>(DifferentialReference.get())->RenderReferenceFrame();
    }
}

void VulkanRenderer::VBlank()
{
    // The one point in the DS frame where this frame's structured 2D planes and
    // this frame's 3D image both exist: the software engines have finished all
    // 192 scanlines, and RenderFrame() submitted the 3D work at the start of the
    // frame.
    //
    // Composing here rather than at present time is the whole architecture. A
    // deferred compositor would have to decide, at present time, which 2D engine
    // drives which LCD -- and Metroid Prime Hunters flips POWCNT1 bit 15 every
    // frame, so that assignment alternates while the control word does not.
    // SoftRenderer::DrawScanline() already resolved engine -> LCD for *this*
    // frame into a per-scanline route table. The packer follows that table, so
    // there is nothing left to guess and no previous-frame assignment to
    // disagree with.
    auto* vulkan = GetVulkanRenderer3D();
    bool nativeComposed = false;
    const bool exactValidation = GPU2DNative::ExactValidationEnabled();
    if (vulkan && HasNativeGPU2DFrame())
    {
        const GPU2DNative::FrameInput& nativeFrame = GetNativeGPU2DFrame();
        nativeComposed = vulkan->ComposeNativeGPU2D(
            nativeFrame,
            nativeFrame.Generation.Frame,
            !GPU.GPU3D.AbortFrame && vulkan->HasFinalFBContent(),
            exactValidation ? GetSoftwareScreenFrame(0u) : nullptr,
            exactValidation ? GetSoftwareScreenFrame(1u) : nullptr);
        if (nativeComposed)
        {
            if (!NativeGPU2DAnnounced)
            {
                Platform::Log(Platform::LogLevel::Info,
                    "Vulkan renderer gpu2d=Vulkan gpu3d=Vulkan fallback=0\n");
                NativeGPU2DAnnounced = true;
            }
        }
        else
        {
            VulkanPerf::AddCounter(VulkanPerf::Counter::NativeGPU2DFallbackFrames);
            if (!NativeGPU2DFallbackAnnounced)
            {
                Platform::Log(Platform::LogLevel::Warn,
                    "Vulkan renderer gpu2d=Software fallback=1 reason=native dispatch unavailable\n");
                NativeGPU2DFallbackAnnounced = true;
            }
        }
    }

    if (vulkan && !nativeComposed && vulkan->HasRuntimeFailure())
    {
        Platform::Log(Platform::LogLevel::Error,
            "Vulkan renderer gpu2d=Software fallback=1 disabled=1 reason=%s\n",
            vulkan->GetRuntimeFailureReason().c_str());
        if (VBlankObserverFn)
            VBlankObserverFn(VBlankObserverData);
        return;
    }
    if (exactValidation && vulkan && !nativeComposed)
    {
        vulkan->FailNativeGPU2DExact(
            "native GPU2D exact gate rejected a fallback or unavailable frame");
        if (VBlankObserverFn)
            VBlankObserverFn(VBlankObserverData);
        return;
    }
    if (vulkan && !nativeComposed && !NativeGPU2DFallbackAnnounced)
    {
        VulkanPerf::AddCounter(VulkanPerf::Counter::NativeGPU2DFallbackFrames);
        Platform::Log(Platform::LogLevel::Warn,
            "Vulkan renderer gpu2d=Software fallback=1 reason=native frame unavailable\n");
        NativeGPU2DFallbackAnnounced = true;
    }

    StructuredVulkanFrameView view{};
    if (!nativeComposed && vulkan && GetStructuredVulkanFrame(view) && view.Valid)
    {
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

        // Generation is carried through so a frame the producer has not
        // refreshed is never recomposed, and a stale one is never composed at
        // all.
        const bool composed = vulkan->ComposeStructuredOutput(
            planes, lineMeta, view.CaptureCommands, view.ScreenRouting, view.Generation,
            view.ContentGeneration);
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::StructuredScreenRouteCopyBytes,
            view.ScreenRouteCopyBytes);
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::StructuredScreenRouteCopyNanoseconds,
            view.ScreenRouteCopyNanoseconds);
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::StructuredRegularLines,
            view.StructuredRegularLines);
        VulkanPerf::AddCounter(
            VulkanPerf::Counter::StructuredFallbackLines,
            view.StructuredFallbackLines);
        if (composed
            && DifferentialReference
            && vulkan->GetScaleFactor() == 1
            && !GPU.GPU3D.AbortFrame)
            DifferentialState.CompareFrame(*Rend3D, *DifferentialReference, "Vulkan");
    }

    // MELONPRIME_VULKAN_PRESENT_HOOK_V1
    //
    // Notified even when nothing was composed this frame: the panel's snapshot
    // then simply keeps pointing at the previously published surface, which is
    // still the newest frame that exists.
    if (VBlankObserverFn)
        VBlankObserverFn(VBlankObserverData);
}

RendererOutput VulkanRenderer::GetOutput()
{
    auto* vulkan = GetVulkanRenderer3D();
    if (!vulkan)
        return {};

    RendererOutput output = vulkan->GetComposedOutput();
    if (output.Kind == RendererOutputKind::None)
    {
        // The Vulkan pipelines compile incrementally after a ROM starts, so the
        // first few frames have no composed output yet. The software buffers
        // carry correct 2D and a placeholder for the 3D layer -- wrong, but
        // initialised and stable, which is what the panel needs to draw
        // something rather than uninitialised memory. DX12 uses the same
        // fallback for the same window.
        if (!NativeGPU2DStartupFallbackAnnounced)
        {
            Platform::Log(Platform::LogLevel::Warn,
                "requested=Vulkan actual=Vulkan gpu2d=Software gpu3d=Vulkan "
                "fallback=1 startupFallback=1 reason=pipeline compilation\n");
            NativeGPU2DStartupFallbackAnnounced = true;
        }
        return SoftRenderer::GetOutput();
    }

    return output;
}

RendererOutputLease VulkanRenderer::AcquireOutputLease()
{
    auto* vulkan = GetVulkanRenderer3D();
    if (!vulkan)
        return {};

    RendererOutputLease lease = vulkan->AcquireComposedOutputLease();
    if (lease.Output.Kind != RendererOutputKind::None)
        return lease;
    if (!NativeGPU2DStartupFallbackAnnounced)
    {
        Platform::Log(Platform::LogLevel::Warn,
            "requested=Vulkan actual=Vulkan gpu2d=Software gpu3d=Vulkan "
            "fallback=1 startupFallback=1 reason=pipeline compilation\n");
        NativeGPU2DStartupFallbackAnnounced = true;
    }
    return RendererOutputLease(SoftRenderer::GetOutput(), nullptr, nullptr);
}

bool VulkanRenderer::NeedsShaderCompile()
{
    return Rend3D && Rend3D->NeedsShaderCompile();
}

void VulkanRenderer::ShaderCompileStep(int& current, int& count)
{
    if (Rend3D)
        Rend3D->ShaderCompileStep(current, count);
}

bool VulkanRenderer::HasRuntimeFailure() const noexcept
{
    const auto* vulkan = GetVulkanRenderer3D();
    return vulkan && vulkan->HasRuntimeFailure();
}

const std::string& VulkanRenderer::GetRuntimeFailureReason() const noexcept
{
    static const std::string empty;
    const auto* vulkan = GetVulkanRenderer3D();
    return vulkan ? vulkan->GetRuntimeFailureReason() : empty;
}

VulkanRenderer3D* VulkanRenderer::GetVulkanRenderer3D() noexcept
{
    return dynamic_cast<VulkanRenderer3D*>(Rend3D.get());
}

const VulkanRenderer3D* VulkanRenderer::GetVulkanRenderer3D() const noexcept
{
    return dynamic_cast<const VulkanRenderer3D*>(Rend3D.get());
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
