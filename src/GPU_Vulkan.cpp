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

namespace melonDS
{

VulkanRenderer::VulkanRenderer(melonDS::NDS& nds)
    : SoftRenderer(nds)
{
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
}

void VulkanRenderer::SetRenderSettings(RendererSettings& settings)
{
    // Same reason as PreSavestate(): the base implementation casts Rend3D to
    // SoftRenderer3D to forward the `Threaded` option, which does not apply.
    //
    // The two low-latency fields are only recorded here; the emulation thread
    // reads them back through GetNvidiaReflexMode() / GetAmdAntiLag2Enabled()
    // and hands them to the screen panel. The VK_NV_low_latency2 /
    // VK_AMD_anti_lag implementation behind that is phase 13.
    NvidiaReflexMode = settings.NvidiaReflexMode;
    AmdAntiLag2Enabled = settings.AmdAntiLag2Enabled;

    if (auto* vulkan = GetVulkanRenderer3D())
        vulkan->SetRenderSettings(settings.ScaleFactor, settings.BetterPolygons, settings.HiresCoordinates);
}

void VulkanRenderer::Start3DRendering()
{
    // Renderer::Start3DRendering() drives Rend3D->RenderFrame(). Overridden
    // explicitly so the phase-13 Reflex render-submit marker has an owner and
    // so the call does not depend on which base happens to define it.
    Renderer::Start3DRendering();
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
    // frame before it filled the planes, so there is nothing left to guess and
    // nothing latched from a previous frame to disagree with.
    auto* vulkan = GetVulkanRenderer3D();
    StructuredVulkanFrameView view{};
    if (vulkan && GetStructuredVulkanFrame(view) && view.Valid)
    {
        const std::array<const u32*, 6> planes = {
            view.Plane[0][0],
            view.Plane[0][1],
            view.Plane[0][2],
            view.Plane[1][0],
            view.Plane[1][1],
            view.Plane[1][2],
        };
        const std::array<const u32*, 2> lineMeta = {
            view.LineMeta[0],
            view.LineMeta[1],
        };

        // Generation is carried through so a frame the producer has not
        // refreshed is never recomposed, and a stale one is never composed at
        // all.
        vulkan->ComposeStructuredOutput(planes, lineMeta, view.Generation);
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

    const u32* top = vulkan->GetComposedScreen(0);
    const u32* bottom = vulkan->GetComposedScreen(1);
    if (!top || !bottom)
    {
        // The Vulkan pipelines compile incrementally after a ROM starts, so the
        // first few frames have no composed output yet. The software buffers
        // carry correct 2D and a placeholder for the 3D layer -- wrong, but
        // initialised and stable, which is what the panel needs to draw
        // something rather than uninitialised memory. DX12 uses the same
        // fallback for the same window.
        return SoftRenderer::GetOutput();
    }

    // The internal-resolution surface. Returning 256x192 here would throw away
    // every pixel the high-resolution 3D path produced, so the width and height
    // travel with the pointers.
    return RendererOutput::CpuBgra(
        const_cast<u32*>(top),
        const_cast<u32*>(bottom),
        vulkan->GetComposedWidth(),
        vulkan->GetComposedHeight());
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
