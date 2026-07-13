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

#ifdef MELONPRIME_DS
// MELONPRIME_VULKAN_STRUCTURED_3D_COMPOSITION_V1
// Per-pixel Software 2D decision used by the Vulkan presenter. The packed word
// carries the second 2D color plus the exact effect that was applied while BG0/3D
// occupied the top composition slot.
enum class Soft3DCompositionMode : u32
{
    None = 0,
    Direct = 1,
    Alpha5 = 2,
    AlphaCoefficients = 3,
    BrightnessUp = 4,
    BrightnessDown = 5,
};

struct Soft3DCompositionPixel
{
    static constexpr u32 Pack(
        u32 underColor,
        Soft3DCompositionMode mode,
        u32 factorA = 0,
        u32 factorB = 0) noexcept
    {
        return (underColor & 0x3Fu) |
            (((underColor >> 8u) & 0x3Fu) << 6u) |
            (((underColor >> 16u) & 0x3Fu) << 12u) |
            ((static_cast<u32>(mode) & 0x7u) << 18u) |
            ((factorA & 0x1Fu) << 21u) |
            ((factorB & 0x1Fu) << 26u);
    }
};

static_assert(static_cast<u32>(Soft3DCompositionMode::BrightnessDown) <= 0x7u);
#endif

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

protected:
    virtual void OnRendered3DLine(u32 line, const u32* pixels) noexcept
    {
        (void)line;
        (void)pixels;
    }

#ifdef MELONPRIME_DS
    // MELONPRIME_VULKAN_EXPLICIT_3D_OWNERSHIP_V1
    // MELONPRIME_VULKAN_STRUCTURED_3D_COMPOSITION_V1
    // Reports the exact Software 2D operation used when BG0/3D is the top layer.
    // A zero word means that another 2D layer owns the final pixel.
    virtual void OnComposed3DCompositionLine(
        u32 line, const u32* composition) noexcept
    {
        (void)line;
        (void)composition;
    }
#endif

private:
    friend class SoftRenderer2D;
    friend class SoftRenderer3D;

    u32* Framebuffer[2][2];

    u32* Output3D;
    alignas(8) u32 Output2D[2][256];
#ifdef MELONPRIME_DS
    // MELONPRIME_VULKAN_EXPLICIT_3D_OWNERSHIP_V1
    // MELONPRIME_VULKAN_STRUCTURED_3D_COMPOSITION_V1
    alignas(8) u32 Output3DComposition[256];
#endif

    void DrawScanlineA(u32 line, u32* dst);
    void DrawScanlineB(u32 line, u32* dst);

    void DoCapture(u32 line);

    void ApplyMasterBrightness(u16 regval, u32* dst);
    void ExpandColor(u32* dst);
};

}

#endif // GPU_SOFT_H
