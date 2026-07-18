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

#pragma once

#include "GPU2D.h"

namespace melonDS
{
class SoftRenderer;

class SoftRenderer2D : public Renderer2D
{
public:
    SoftRenderer2D(melonDS::GPU2D& gpu2D, SoftRenderer& parent);
    ~SoftRenderer2D() override;
    bool Init() override { return true; }
    void Reset() override;

    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;
    void VBlank() override {}
    void VBlankEnd() override {};

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // Structured capture metadata (plane0/plane1/control per VRAM bank+line)
    // is owned by Parent (SoftRenderer) and shared by both engine instances
    // -- Sapphire drives Engine A and Engine B through a *single* shared
    // GPU2D::SoftRenderer object (CurUnit switches which unit it draws),
    // so its capture arrays are implicitly shared. melonPrimeDS instead has
    // two separate SoftRenderer2D instances (Rend2D_A / Rend2D_B); per-engine
    // storage here would mean display capture (Engine-A-only hardware) is
    // written to Rend2D_A but never visible to Rend2D_B reading that same
    // captured VRAM as a direct-color BG, causing a full Top/Bottom swap.
    // These methods operate on Parent's shared arrays, not per-instance
    // ones -- ownership unit is VRAM bank+address, not engine.
    void StoreStructuredCaptureLine(
        u32 line,
        u32 width,
        u32 destinationBank,
        u32 destinationAddress,
        u32 sourceBAddress,
        u32 sourceBBank,
        bool sourceBFromVram,
        const u16* captureOutput);
    [[nodiscard]] bool DrawStructuredCapturePixel(u32* destination, u32 flatByteAddress);
#endif

private:
    SoftRenderer& Parent;

    enum
    {
        OBJ_StandardPal = (1<<12),
        OBJ_DirectColor = (1<<15),
        OBJ_BGPrioMask = (0x3<<16),
        OBJ_IsOpaque = (1<<18),
        OBJ_OpaPrioMask = (OBJ_BGPrioMask | OBJ_IsOpaque),
        OBJ_IsSprite = (1<<19),
        OBJ_Mosaic = (1<<20),
    };

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // Sapphire's accelerated 2D contract keeps the layer below a 3D slot.
    // The third plane is only needed by the guarded Vulkan compositor path.
    alignas(8) u32 BGOBJLine[256*3];
#else
    alignas(8) u32 BGOBJLine[256*2];
#endif

    alignas(8) u8 WindowMask[256];

    alignas(8) u32 OBJLine[256];
    alignas(8) u8 OBJWindow[256];

    u32 NumSprites;

    u8* CurBGXMosaicTable;
    array2d<u8, 16, 256> MosaicTable = []() constexpr
    {
        array2d<u8, 16, 256> table {};
        // initialize mosaic table
        for (int m = 0; m < 16; m++)
        {
            for (int x = 0; x < 256; x++)
            {
                int offset = x % (m+1);
                table[m][x] = offset;
            }
        }

        return table;
    }();

    u32 ColorComposite(int i, u32 val1, u32 val2) const;

    template<u32 bgmode> void DrawScanlineBGMode(u32 line);
    void DrawScanlineBGMode6(u32 line);
    void DrawScanlineBGMode7(u32 line);
    void DrawScanline_BGOBJ(u32 line, u32* dst);

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    void DrawPixel(u32* dst, u16 color, u32 flag);
#else
    static void DrawPixel(u32* dst, u16 color, u32 flag);
#endif

    void DrawBG_3D();
    template<bool mosaic> void DrawBG_Text(u32 line, u32 bgnum);
    template<bool mosaic> void DrawBG_Affine(u32 line, u32 bgnum);
    template<bool mosaic> void DrawBG_Extended(u32 line, u32 bgnum);
    template<bool mosaic> void DrawBG_Large(u32 line);

    void ApplySpriteMosaicX();
    void InterleaveSprites(u32 prio);
    template<bool window> void DrawSpritePixel(int color, u32 pixelattr, s32 xpos);
    template<bool window> void DrawSprite_Rotscale(u32 num, u32 boundwidth, u32 boundheight, u32 width, u32 height, s32 xpos, s32 ypos);
    template<bool window> void DrawSprite_Normal(u32 num, u32 width, u32 height, s32 xpos, s32 ypos);
};

}
