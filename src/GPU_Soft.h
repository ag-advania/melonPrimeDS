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
    void VBlankEnd() override {};

    void AllocCapture(u32 bank, u32 start, u32 len) override {};
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override {};

    bool GetFramebuffers(void** top, void** bottom) override;
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    bool CopyStructured2DFrameSnapshot(SapphireStructured2DFrameSnapshot& snapshot) const override;
#endif

private:
    friend class SoftRenderer2D;
    friend class SoftRenderer3D;

    u32* Framebuffer[2][2];

    u32* Output3D;
    alignas(8) u32 Output2D[2][256];
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // MELONPRIME_SAPPHIRE_VULKAN_STRUCTURED_2D_A2
    alignas(64) u32 StructuredPlane0[2][256];
    alignas(64) u32 StructuredPlane1[2][256];
    alignas(64) u32 StructuredControl[2][256];
    alignas(64) u32 StructuredNativeFinal[2][256];
    std::array<SapphireStructured2DFrameSnapshot, 2> StructuredFrames{};
    std::array<u8, SapphireStructured2DFrameSnapshot::EngineCount
        * SapphireStructured2DFrameSnapshot::LineCount> StructuredLineReceived{};
    std::size_t StructuredWriteFrame = 0;
    std::size_t StructuredPublishedFrame = 1;
    bool HasPublishedStructuredFrame = false;

    void BeginStructured2DFrame(u64 frameSerial, u64 generation);
    void SubmitStructured2DLine(const SapphireStructured2DLine& line);
    void EndStructured2DFrame(u64 frameSerial, u64 generation, bool screenSwap);
#endif

    void DrawScanlineA(u32 line, u32* dst);
    void DrawScanlineB(u32 line, u32* dst);

    void DoCapture(u32 line);

    void ApplyMasterBrightness(u16 regval, u32* dst);
    void ExpandColor(u32* dst);
};

}

#endif // GPU_SOFT_H
