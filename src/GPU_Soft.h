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

#ifndef GPU_SOFT_H
#define GPU_SOFT_H

#include "GPU.h"
#include "GPU2D_Soft.h"
#include "GPU3D_Soft.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
#include <optional>
#endif

namespace melonDS
{

class SoftRenderer : public Renderer
{
public:
    explicit SoftRenderer(melonDS::NDS& nds);
    ~SoftRenderer() override;
    bool Init() override { return true; }
    void Reset() override;
    void Stop() override;

    void PreSavestate() override;
    void PostSavestate() override;

    void SetRenderSettings(RendererSettings& settings) override;

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;

    void VBlank() override {};
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // MELONPRIME-PORT: reference GPU2D_Soft.cpp SoftRenderer::VBlankEnd(Unit*, Unit*)
    // (~1244-1273) needs a real body when an accelerated, structured-metadata renderer is
    // active (see GPU_Soft.cpp); body moved out-of-line under the guard, empty otherwise.
    void VBlankEnd() override;
#else
    void VBlankEnd() override {};
#endif

    void AllocCapture(u32 bank, u32 start, u32 len) override {};
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override {};

    bool GetFramebuffers(void** top, void** bottom) override;

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // MELONPRIME-PORT: see GPU.h GPU::GetRenderer2D() / GPU2D_Soft.h StructuredVulkan2DRendererBridge
    // class-level comments. Owns the bridge (constructed once Rend2D_A/Rend2D_B exist, see ctor).
    [[nodiscard]] StructuredVulkan2DRendererBridge& GetRenderer2D() noexcept { return *Renderer2DBridge; }

    // MELONPRIME-PORT: reference-generation (Sapphire) accelerated Framebuffer channel
    // (melonDS-android-lib src/GPU.cpp InitFramebuffers(): `Framebuffer[buf][screen]` is
    // reallocated at stride `256*3+1` words/line -- 3 packed 2D+3D-compositing planes plus a
    // trailing per-line renderer-metadata word -- whenever GPU3D.IsRendererAccelerated(); see
    // GPU2D_Soft.cpp SoftRenderer::DrawScanline(line, unit) for the producer). MELONPRIME-PORT-ADAPT:
    // kept as a PARALLEL buffer here (not a reinterpretation of the existing flat `Framebuffer[2][2]`
    // below, which non-Vulkan renderers/consumers still read as plain 256-wide BGRA) so the
    // Vulkan-off build is byte-for-byte unaffected. Allocated lazily by EnsureAccelFramebuffers()
    // the first time the active Renderer3D reports UsesStructured2DMetadata() (mirrors reference's
    // InitFramebuffers() being sized by the accelerated flag at renderer-set time); packed per-line
    // by PackAccelFramebufferLine()/ClearAccelFramebufferLine() from SoftRenderer::DrawScanline
    // (GPU_Soft.cpp). Physical-screen-indexed (index 0 = top, 1 = bottom), matching this file's
    // DrawScanlineA/B ScreenSwap-aware Framebuffer[][] convention and the reference GPU's
    // AssignFramebuffers(): reference DrawScanline indexes its assigned pointers by unit, but
    // AssignFramebuffers has already mapped those pointers onto physical top/bottom storage.
    static constexpr u32 kAccelFramebufferStride = 256u * 3u + 1u;
    static constexpr u32 kAccelFramebufferLineCount = 192u;
    [[nodiscard]] const u32* GetAccelFramebuffer(int buf, int screen) const noexcept { return FramebufferAccel[buf][screen]; }
#endif

private:
    friend class SoftRenderer2D;
    friend class SoftRenderer3D;

    u32* Framebuffer[2][2];

    u32* Output3D;
    alignas(8) u32 Output2D[2][256];

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // MELONPRIME-PORT: reference GPU2D_Soft.h ~236 (CaptureLineUses3d), GPU2D_Soft.cpp ~350/1046
    // (reset), ~1794-1795 (write, inside DoCapture), ~2449-2450 (read, inside
    // DrawScanline_BGOBJ's engine-B captureBacked3DSourceClass classification).
    // MELONPRIME-PORT-ADAPT: reference's single CurUnit-switching SoftRenderer instance shares
    // this field naturally between engines; our fork splits engine A and B into separate
    // SoftRenderer2D instances (Rend2D_A/Rend2D_B), but this field is genuinely cross-engine --
    // SoftRenderer::DoCapture (below, engine-A-scoped) writes per-line "did this capture use 3D"
    // bits that engine B's SoftRenderer2D::DrawScanline_BGOBJ reads later on the same scanline.
    // SoftRenderer::DrawScanline preserves Sapphire's A -> capture -> B ordering. Hosted here, on
    // the shared Parent, rather than on either SoftRenderer2D instance. SoftRenderer2D reaches it
    // via the existing Parent reference (GPU2D_Soft.h).
    std::array<u8, 192> CaptureLineUses3d {};
    StructuredVulkan2DRendererBridge::DebugCaptureStats LastDebugCaptureStats {};
    bool HasLastDebugCapture3dSource = false;
    alignas(8) std::array<u32, 256 * 192> LastDebugCapture3dSource {};
    alignas(8) u32 AccelBGOBJLine[2][256 * 3] {};

    // MELONPRIME-PORT: see GetRenderer2D()/GetAccelFramebuffer() above for design rationale.
    std::optional<StructuredVulkan2DRendererBridge> Renderer2DBridge;
    u32* FramebufferAccel[2][2] = {};
    bool FramebufferAccelAllocated = false;
    void EnsureAccelFramebuffers();
    void FreeAccelFramebuffers();
    void PackAccelFramebufferLine(u32 row, u32 contentLine);
    void ClearAccelFramebufferLine(u32 row);
    void PackAccelFramebufferSlot(
        u32* slotBase, u32 row, u32 contentLine, bool topScreen,
        SoftRenderer2D& unitRenderer, melonDS::GPU2D& unit);
#endif

    void DrawScanlineA(u32 line, u32* dst);
    void DrawScanlineB(u32 line, u32* dst);

    void DoCapture(u32 line);

    void ApplyMasterBrightness(u16 regval, u32* dst);
    void ExpandColor(u32* dst);
};

}

#endif // GPU_SOFT_H
