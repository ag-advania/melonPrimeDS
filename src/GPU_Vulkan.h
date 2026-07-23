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

#ifndef GPU_VULKAN_H
#define GPU_VULKAN_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "GPU.h"
#include "GPU_Soft.h"
#include "GPU3D_Vulkan.h"

namespace melonDS
{

// MelonPrime glue for the verbatim-ported Sapphire VulkanRenderer3D
// (src/GPU3D_Vulkan.h/.cpp, never edited). Modeled after GLRenderer (GPU_OpenGL.h/.cpp)
// for the outer Renderer shape.
//
// MELONPRIME-PORT: architecture decision (W4). VulkanGpuRenderer inherits SoftRenderer
// wholesale instead of Renderer directly, reusing its entire CPU 2D/capture/compositor
// stack (Rend2D_A/Rend2D_B = real SoftRenderer2D instances, Output2D/Output3D,
// DrawScanlineA/B, ExpandColor, DoCapture, and the newly-ported W1 VBlankEnd()) as-is.
// SoftRenderer's own ctor unconditionally constructs Rend3D as a SoftRenderer3D with no
// factory seam to inject a different Renderer3D; rather than touch that shared,
// upstream-adjacent constructor (used by every software-rendering backend) to add one,
// VulkanGpuRenderer's ctor lets it run as-is and then replaces Rend3D with the real
// VulkanRenderer3D immediately afterward (see GPU_Vulkan.cpp) -- a few-line, fully
// self-contained diff that leaves GPU_Soft.h/.cpp completely untouched. The only overrides
// needed are the ones SoftRenderer's own overrides assume a concrete SoftRenderer3D for
// (SetRenderSettings/PreSavestate/PostSavestate all dynamic_cast<SoftRenderer3D*>(Rend3D.get())
// and would silently no-op -- or, for SetRenderSettings, dereference a null cast result --
// against a VulkanRenderer3D) plus the Sapphire-generation (GPU&-parameter) 3D entry points
// the old no-arg Renderer3D virtuals don't reach (Reset/Stop's 3D half, Start/Finish/Restart
// 3DRendering, and VBlank's Blit call). VBlankEnd needs no override: the W1-ported
// SoftRenderer::VBlankEnd() already dispatches polymorphically through Rend3D->Accelerated /
// Rend3D->UsesStructured2DMetadata() with no concrete-type assumption.
class VulkanGpuRenderer : public SoftRenderer
{
public:
    explicit VulkanGpuRenderer(melonDS::NDS& nds) noexcept;
    ~VulkanGpuRenderer() override = default;

    // Init() is inherited from SoftRenderer unchanged (`return true`); VulkanRenderer3D
    // does not override the old-generation Renderer3D::Init() either (it lazily initializes
    // from Reset(GPU&)/RenderFrame(GPU&) instead), so there is nothing Vulkan-specific to add.
    void Reset() override;
    void Stop() override;

    void SetRenderSettings(RendererSettings& settings) override;

    void PreSavestate() override;
    void PostSavestate() override;

    void Start3DRendering() override;
    void Finish3DRendering() override;
    void Restart3DRendering() override;

    void VBlank() override;

private:
    // Rend3D is always a VulkanRenderer3D for this Renderer subclass (swapped in by the
    // ctor in the .cpp, right after the SoftRenderer base ctor constructs the placeholder
    // SoftRenderer3D); this narrows the protected base pointer.
    [[nodiscard]] VulkanRenderer3D& VulkanRend3D() noexcept
    {
        return static_cast<VulkanRenderer3D&>(*Rend3D);
    }
};

}

#endif // defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#endif // GPU_VULKAN_H
