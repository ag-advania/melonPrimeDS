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

#include "NDS.h"
#include "GPU_Vulkan.h"

namespace melonDS
{

VulkanGpuRenderer::VulkanGpuRenderer(melonDS::NDS& nds) noexcept
    : SoftRenderer(nds)
{
    // SoftRenderer's ctor (just run above) unconditionally constructed Rend3D as a
    // SoftRenderer3D and Rend2D_A/Rend2D_B as real SoftRenderer2D instances bound to
    // *this. Keep the 2D pair -- that is the whole point of inheriting SoftRenderer (see
    // class-level comment in GPU_Vulkan.h) -- and replace only Rend3D with the real
    // Sapphire-generation VulkanRenderer3D. The briefly-constructed SoftRenderer3D is
    // destroyed here (its ctor only allocates a few semaphores; safe/cheap to discard).
    //
    // Amendment A ctor-context wiring: VulkanRenderer3D::New() constructs via the
    // verbatim Sapphire ctor `VulkanRenderer3D() : Renderer3D(true)`, which carries no
    // GPU3D& parameter. Renderer3D(bool)'s definition (GPU3D.cpp) asserts this context
    // is set whenever Accelerated is true; it is not otherwise consumed today (see the
    // comments there and in GPU3D.h next to Renderer3DLegacyBase for why).
    MelonPrimeSetAcceleratedRendererCtorContext(&nds.GPU.GPU3D);
    Rend3D = VulkanRenderer3D::New();
    MelonPrimeSetAcceleratedRendererCtorContext(nullptr);
}

void VulkanGpuRenderer::Reset()
{
    // SoftRenderer::Reset() resets Rend2D_A/Rend2D_B and calls the old-generation
    // Rend3D->Reset() (a no-op default on VulkanRenderer3D); the real reset work happens
    // via the explicit GPU&-parameter call below.
    SoftRenderer::Reset();
    VulkanRend3D().Reset(GPU);
}

void VulkanGpuRenderer::Stop()
{
    // SoftRenderer::Stop() clears the CPU framebuffers to black; add the Vulkan-specific
    // resource teardown alongside it.
    SoftRenderer::Stop();
    VulkanRend3D().Stop(GPU);
}

void VulkanGpuRenderer::SetRenderSettings(RendererSettings& settings)
{
    // MELONPRIME-PORT: RendererSettings only carries ScaleFactor/Threaded/
    // HiresCoordinates/BetterPolygons today; VulkanRenderer3D::SetRenderSettings()
    // also takes simple-pipeline and conservative-coverage-fix knobs that have no
    // schema/settings-dialog surface yet. Wiring those through (with the accompanying
    // schema/defaults/edit-mode parity the SRP contract requires) is left to whichever
    // phase adds Vulkan-specific settings UI; sensible fixed defaults are used here so
    // the renderer is at least functional in the meantime.
    VulkanRend3D().SetRenderSettings(
        settings.Threaded,
        settings.BetterPolygons,
        settings.ScaleFactor,
        /* useSimplePipeline */ true,
        /* conservativeCoverageEnabled */ false,
        /* conservativeCoveragePx */ 0.0f,
        /* conservativeCoverageDepthBias */ 0.0f,
        /* conservativeCoverageApplyRepeat */ true,
        /* conservativeCoverageApplyClamp */ false,
        /* debug3dClearMagenta */ false,
        GPU);
    // Deliberately does NOT call SoftRenderer::SetRenderSettings() -- that implementation
    // does `dynamic_cast<SoftRenderer3D*>(Rend3D.get())->SetThreaded(...)`, which would
    // dereference a null pointer here since Rend3D is a VulkanRenderer3D.
}

void VulkanGpuRenderer::PreSavestate()
{
    // Deliberately does NOT call SoftRenderer::PreSavestate() -- that implementation
    // does `dynamic_cast<SoftRenderer3D*>(Rend3D.get())->IsThreaded()`, which would
    // dereference a null pointer here since Rend3D is a VulkanRenderer3D. VulkanRenderer3D
    // has no equivalent CPU render-thread to pause around a savestate.
}

void VulkanGpuRenderer::PostSavestate()
{
    // See PreSavestate() above.
}

void VulkanGpuRenderer::Start3DRendering()
{
    // The base Renderer::Start3DRendering() default (`Rend3D->RenderFrame();`) targets
    // the old no-arg Renderer3D generation, which is a no-op for VulkanRenderer3D. This
    // override reaches the real Sapphire-generation entry point instead. Called from
    // GPU::StartHBlank() at VCount==215 -- the same VCount reference melonDS-android-lib
    // GPU::StartHBlank() calls GPU3D.VCount215(*this) (-> CurrentRenderer->RenderFrame())
    // at, so timing parity is exact; see GPU3D::VCount215() in GPU3D.cpp for the
    // RenderScreenSwapAt3D half of that reference method, which runs just before this.
    VulkanRend3D().RenderFrame(GPU);
}

void VulkanGpuRenderer::Finish3DRendering()
{
    // Mirrors reference GPU3D::VCount144() (-> CurrentRenderer->VCount144(gpu)), called
    // from GPU::StartScanline() at VCount==192 in both forks (reference's comment there
    // notes rendering "in reality" finishes at line 144, hence the method's name despite
    // firing at VCount 192). Same bridging rationale as Start3DRendering() above.
    VulkanRend3D().VCount144(GPU);
}

void VulkanGpuRenderer::Restart3DRendering()
{
    // Called from GPU::FinishFrame() (on GPU3D.AbortFrame) and GPU::Restart3DFrame(),
    // matching reference's GPU3D.RestartFrame(*this) call site in GPU::FinishFrame().
    VulkanRend3D().RestartFrame(GPU);
}

void VulkanGpuRenderer::VBlank()
{
    // Mirrors reference GPU::StartScanline()'s VCount==192 sequence:
    // GPU3D.VBlank(); ...; if (GPU3D.IsRendererAccelerated()) GPU3D.Blit(*this);
    // GPU3D.VBlank() itself already runs (unconditionally, before Rend->VBlank()) at
    // the call site in our GPU.cpp; the accelerated-only Blit gating is satisfied here
    // simply by this being the Vulkan-specific Renderer subclass -- VBlank() is never
    // called on a VulkanGpuRenderer unless it is the active (accelerated) renderer.
    VulkanRend3D().Blit(GPU);

    // Display-capture setup (SetupAccelFrame/PrepareCaptureFrame/BeginCaptureFrame) now
    // happens in the inherited SoftRenderer::VBlankEnd() (see GPU_Soft.cpp, W1 port),
    // which dispatches through Rend3D polymorphically -- no override needed here.
}

// DrawScanline/DrawSprites/VBlankEnd/AllocCapture/SyncVRAMCapture/GetFramebuffers are all
// inherited from SoftRenderer unchanged: 2D compositing and capture run through the real
// Rend2D_A/Rend2D_B (SoftRenderer2D) pair and Output3D = Rend3D->GetLine(line) (implemented
// by VulkanRenderer3D, see GPU3D_Vulkan.h), and GetFramebuffers() returns the same CPU BGRA
// Framebuffer[][] SoftRenderer already maintains.

}

#endif // defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
