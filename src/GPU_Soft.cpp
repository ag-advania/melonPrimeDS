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

#include "NDS.h"
#include "GPU_Soft.h"
#include "GPU_ColorOp.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
#include <algorithm>
#include <cassert>
#include <cstring>
#include "SapphireGPU2DSoftAccess.h"
#endif

namespace melonDS
{

SoftRenderer::SoftRenderer(melonDS::NDS& nds)
    : Renderer(nds.GPU)
{
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    const size_t len = kPackedFramebufferPixels;
#else
    const size_t len = 256 * 192;
#endif
    Framebuffer[0][0] = new u32[len];
    Framebuffer[0][1] = new u32[len];
    Framebuffer[1][0] = new u32[len];
    Framebuffer[1][1] = new u32[len];
    BackBuffer = 0;

    Rend2D_A = std::make_unique<SoftRenderer2D>(GPU.GPU2D_A, *this);
    Rend2D_B = std::make_unique<SoftRenderer2D>(GPU.GPU2D_B, *this);
    Rend3D = std::make_unique<SoftRenderer3D>(GPU.GPU3D, *this);
}

SoftRenderer::~SoftRenderer()
{
    delete[] Framebuffer[0][0];
    delete[] Framebuffer[0][1];
    delete[] Framebuffer[1][0];
    delete[] Framebuffer[1][1];
}

void SoftRenderer::Reset()
{
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    const size_t len = kPackedFramebufferPixels * sizeof(u32);
#else
    const size_t len = 256 * 192 * sizeof(u32);
#endif
    memset(Framebuffer[0][0], 0, len);
    memset(Framebuffer[0][1], 0, len);
    memset(Framebuffer[1][0], 0, len);
    memset(Framebuffer[1][1], 0, len);
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    memset(StructuredPlane0, 0, sizeof(StructuredPlane0));
    memset(StructuredPlane1, 0, sizeof(StructuredPlane1));
    memset(StructuredControl, 0, sizeof(StructuredControl));
    memset(StructuredNativeFinal, 0, sizeof(StructuredNativeFinal));
    StructuredVulkan2DPlanes.fill(0);
    HasLastDebugCapture3dSource = false;
    std::fill_n(LastDebugCapture3dSource, kStructuredPixelCount, 0u);
    CaptureLineUses3d.fill(0);
    SapphireDebugCaptureStats = {};
#endif
    Rend2D_A->Reset();
    Rend2D_B->Reset();
    GetRenderer3D().Reset();
}

void SoftRenderer::Stop()
{
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    GetRenderer3D().StopRenderer();
#endif
    // clear framebuffers to black
    const size_t len = 256 * 192 * sizeof(u32);
    memset(Framebuffer[0][0], 0, len);
    memset(Framebuffer[0][1], 0, len);
    memset(Framebuffer[1][0], 0, len);
    memset(Framebuffer[1][1], 0, len);
}


void SoftRenderer::PreSavestate()
{
    auto rend3d = dynamic_cast<SoftRenderer3D*>(&GetRenderer3D());
    if (rend3d && rend3d->IsThreaded())
        rend3d->SetupRenderThread();
}

void SoftRenderer::PostSavestate()
{
    auto rend3d = dynamic_cast<SoftRenderer3D*>(&GetRenderer3D());
    if (rend3d && rend3d->IsThreaded())
        rend3d->EnableRenderThread();
}


void SoftRenderer::SetRenderSettings(RendererSettings& settings)
{
    Renderer3D& renderer3D = GetRenderer3D();
    if (auto* soft = dynamic_cast<SoftRenderer3D*>(&renderer3D))
        soft->SetThreaded(settings.Threaded);
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    renderer3D.SetRenderSettings(
        settings.Threaded, settings.BetterPolygons,
        settings.ScaleFactor, settings.HiresCoordinates);
#endif
}


void SoftRenderer::DrawScanline(u32 line)
{
    u32 *dstA, *dstB;
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    bool writeCpuFinalA = true;
    bool writeCpuFinalB = true;
    const bool accelerated = GetRenderer3D().UsesStructured2DMetadata();
    const u32 stride = accelerated ? static_cast<u32>(kPackedStride) : 256u;
#else
    const u32 stride = 256u;
#endif
    u32 dstoffset = stride * line;
    if (GPU.ScreenSwap)
    {
        dstA = &Framebuffer[BackBuffer][0][dstoffset];
        dstB = &Framebuffer[BackBuffer][1][dstoffset];
    }
    else
    {
        dstA = &Framebuffer[BackBuffer][1][dstoffset];
        dstB = &Framebuffer[BackBuffer][0][dstoffset];
    }

    // the position used for drawing operations is based on VCOUNT
    line = GPU.VCount;
    if (line < 192)
    {
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        const bool structuredSourceActive = GetRenderer3D().UsesStructured2DMetadata();
        if (structuredSourceActive && line == 0)
        {
            HasLastDebugCapture3dSource = false;
            std::fill_n(LastDebugCapture3dSource, kStructuredPixelCount, 0u);
            CaptureLineUses3d.fill(0);
        }
#endif
        // retrieve 3D output
        Output3D = GetRenderer3D().GetLine(line);

        // draw BG/OBJ layers
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        // A line can return early for disabled/forced-blank engines. Clear the
        // staging pair first so stale metadata can never cross frame boundaries.
        memset(StructuredPlane0, 0, sizeof(StructuredPlane0));
        memset(StructuredPlane1, 0, sizeof(StructuredPlane1));
        memset(StructuredControl, 0, sizeof(StructuredControl));
        memset(StructuredNativeFinal, 0, sizeof(StructuredNativeFinal));
#endif
        Rend2D_A->DrawScanline(line);
        Rend2D_B->DrawScanline(line);

        // draw the final screen output
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        if (structuredSourceActive)
        {
            const u32 displayModeA = (GPU.GPU2D_A.DispCnt >> 16u) & 0x3u;
            const u32 displayModeB = (GPU.GPU2D_B.DispCnt >> 16u) & 0x1u;
            writeCpuFinalA = displayModeA != 1u
                || !GPU.ScreensEnabled
                || !GPU.GPU2D_A.Enabled
                || GPU.GPU2D_A.ForcedBlank;
            writeCpuFinalB = displayModeB != 1u
                || !GPU.ScreensEnabled
                || !GPU.GPU2D_B.Enabled
                || GPU.GPU2D_B.ForcedBlank;
        }
        if (writeCpuFinalA)
            DrawScanlineA(line, dstA);
        if (writeCpuFinalB)
            DrawScanlineB(line, dstB);
#else
        DrawScanlineA(line, dstA);
        DrawScanlineB(line, dstB);
#endif

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        if (structuredSourceActive)
        {
            for (u32 engine = 0; engine < 2; ++engine)
            {
                if (!accelerated)
                    continue;

                const GPU2D& engine2D = engine == 0 ? GPU.GPU2D_A : GPU.GPU2D_B;
                UpdateStructuredVulkan2DLine(engine, line);
                WriteAcceleratedPackedRow(
                    engine == 0 ? dstA : dstB,
                    engine,
                    line,
                    engine == 0 ? GPU.MasterBrightnessA : GPU.MasterBrightnessB,
                    engine2D.DispCnt,
                    engine2D.ForcedBlank,
                    engine2D.Enabled);
            }
        }
#endif

        // perform display capture if enabled
        if (GPU.CaptureEnable)
            DoCapture(line);
    }
    else
    {
        // if scanlines outside VCOUNT range 0..191 were to be visible, fill them white
        // this may happen if VCOUNT is written to during active display
        // the actual hardware behavior depends on the screen model, and suggests that
        // no video signal is output for such scanlines

        for (int i = 0; i < 256; i++)
        {
            dstA[i] = 0x3F3F3F;
            dstB[i] = 0x3F3F3F;
        }
    }

    if (GPU.ScreensEnabled)
    {
        // convert to 32-bit BGRA
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        const bool skipExpand =
            accelerated && GetRenderer3D().UsesStructured2DMetadata();
        if (writeCpuFinalA && !skipExpand)
            ExpandColor(dstA);
        if (writeCpuFinalB && !skipExpand)
            ExpandColor(dstB);
#else
        ExpandColor(dstA);
        ExpandColor(dstB);
#endif
    }
    else
    {
        // if the screens are disabled: fill the framebuffer black
        for (int i = 0; i < 256; i++)
        {
            dstA[i] = 0xFF000000;
            dstB[i] = 0xFF000000;
        }
    }
}

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
size_t SoftRenderer::ScreenIndexForEngine(u32 engine) const noexcept
{
    if (GPU.ScreenSwap)
        return engine == 0 ? 0u : 1u;
    return engine == 0 ? 1u : 0u;
}

void SoftRenderer::UpdateStructuredVulkan2DLine(u32 engine, u32 line)
{
    const size_t screenIndex = ScreenIndexForEngine(engine);
    const size_t rowBase = static_cast<size_t>(line) * kStructuredScreenWidth;
    const size_t screenBase =
        screenIndex * kStructuredPlaneCount * kStructuredPixelCount;
    std::memcpy(
        StructuredVulkan2DPlanes.data() + screenBase + rowBase,
        StructuredPlane0[engine],
        kStructuredScreenWidth * sizeof(u32));
    std::memcpy(
        StructuredVulkan2DPlanes.data() + screenBase + kStructuredPixelCount + rowBase,
        StructuredPlane1[engine],
        kStructuredScreenWidth * sizeof(u32));
    std::memcpy(
        StructuredVulkan2DPlanes.data() + screenBase + (kStructuredPixelCount * 2u) + rowBase,
        StructuredControl[engine],
        kStructuredScreenWidth * sizeof(u32));
    SapphireDebugCaptureStats.StructuredCopyLines++;
}

void SoftRenderer::WriteAcceleratedPackedRow(
    u32* dstRow,
    u32 engine,
    u32 line,
    u16 masterBrightness,
    u32 dispCnt,
    bool forcedBlank,
    bool engineEnabled)
{
    if (dstRow == nullptr
        || engine >= kStructuredScreenCount
        || line >= kStructuredScreenHeight)
    {
        return;
    }

    std::memcpy(dstRow, StructuredPlane0[engine], kStructuredScreenWidth * sizeof(u32));
    std::memcpy(
        dstRow + kStructuredScreenWidth,
        StructuredPlane1[engine],
        kStructuredScreenWidth * sizeof(u32));
    std::memcpy(
        dstRow + (kStructuredScreenWidth * 2u),
        StructuredControl[engine],
        kStructuredScreenWidth * sizeof(u32));

    const u32 dispmode = (dispCnt >> 16u) & (engine == 0 ? 0x3u : 0x1u);
    u32 meta = static_cast<u32>(masterBrightness)
        | (dispCnt & 0x30000u)
        | (dispmode << 16u);
    const u32 xpos = static_cast<u32>(GPU.GPU3D.GetRenderXPos()) & 0x1FFu;
    meta |= (xpos << 24u) | ((xpos & 0x100u) << 15u);
    if (forcedBlank || !engineEnabled || !GPU.ScreensEnabled)
        meta = 0u;
    dstRow[kPackedStride - 1u] = meta;
}

const u32* SoftRenderer::GetStructuredVulkan2DPlane(bool topScreen, u32 plane) const noexcept
{
    if (!GetRenderer3D().UsesStructured2DMetadata() || plane >= kStructuredPlaneCount)
        return nullptr;

    const size_t screenIndex = topScreen ? 0u : 1u;
    const size_t offset =
        ((screenIndex * kStructuredPlaneCount) + static_cast<size_t>(plane)) * kStructuredPixelCount;
    return StructuredVulkan2DPlanes.data() + offset;
}

const u32* SoftRenderer::GetSapphireDebugCapture3dSource() const noexcept
{
    return HasLastDebugCapture3dSource ? LastDebugCapture3dSource : nullptr;
}

const std::array<u8, SoftRenderer::kStructuredScreenHeight>&
SoftRenderer::GetSapphireCaptureLineUses3dMask() const noexcept
{
    return CaptureLineUses3d;
}

void SoftRenderer::ClearStructuredVulkan2DState() noexcept
{
    SapphireDebugCaptureStats = {};
    StructuredVulkan2DPlanes.fill(0);
    HasLastDebugCapture3dSource = false;
    std::fill_n(LastDebugCapture3dSource, kStructuredPixelCount, 0u);
    CaptureLineUses3d.fill(0);
}

void SoftRenderer::SyncSapphireFramebufferBindings() noexcept
{
    GPU.FrontBuffer = BackBuffer ^ 1;

    GPU.Framebuffer[0][0] = Framebuffer[0][0];
    GPU.Framebuffer[0][1] = Framebuffer[0][1];
    GPU.Framebuffer[1][0] = Framebuffer[1][0];
    GPU.Framebuffer[1][1] = Framebuffer[1][1];

#ifndef NDEBUG
    assert(GPU.FrontBuffer == 0 || GPU.FrontBuffer == 1);
    assert(GPU.Framebuffer[GPU.FrontBuffer][0] == Framebuffer[BackBuffer ^ 1][0]);
    assert(GPU.Framebuffer[GPU.FrontBuffer][1] == Framebuffer[BackBuffer ^ 1][1]);
#endif
}
#endif

void SoftRenderer::DrawSprites(u32 line)
{
    Rend2D_A->DrawSprites(line);
    Rend2D_B->DrawSprites(line);
}

void SoftRenderer::DrawScanlineA(u32 line, u32* dst)
{
    u32 dispcnt = GPU.GPU2D_A.DispCnt;
    switch ((dispcnt >> 16) & 0x3)
    {
    case 0: // screen off
        {
            for (int i = 0; i < 256; i++)
                dst[i] = 0x3F3F3F;
        }
        return;

    case 1: // regular display
        {
            for (int i = 0; i < 256; i+=2)
                *(u64*)&dst[i] = *(u64*)&Output2D[0][i];
        }
        break;

    case 2: // VRAM display
        {
            u32 vrambank = (dispcnt >> 18) & 0x3;
            if (GPU.VRAMMap_LCDC & (1<<vrambank))
            {
                u16* vram = (u16*)GPU.VRAM[vrambank];
                vram = &vram[line * 256];

                for (int i = 0; i < 256; i++)
                {
                    u16 color = vram[i];
                    u8 r = (color & 0x001F) << 1;
                    u8 g = (color & 0x03E0) >> 4;
                    u8 b = (color & 0x7C00) >> 9;

                    dst[i] = r | (g << 8) | (b << 16);
                }
            }
            else
            {
                for (int i = 0; i < 256; i++)
                    dst[i] = 0;
            }
        }
        break;

    case 3: // FIFO display
        {
            for (int i = 0; i < 256; i++)
            {
                u16 color = GPU.DispFIFOBuffer[i];
                u8 r = (color & 0x001F) << 1;
                u8 g = (color & 0x03E0) >> 4;
                u8 b = (color & 0x7C00) >> 9;

                dst[i] = r | (g << 8) | (b << 16);
            }
        }
        break;
    }

    ApplyMasterBrightness(GPU.MasterBrightnessA, dst);
}

void SoftRenderer::DrawScanlineB(u32 line, u32* dst)
{
    u32 dispcnt = GPU.GPU2D_B.DispCnt;
    switch ((dispcnt >> 16) & 0x1)
    {
    case 0: // screen off
        {
            for (int i = 0; i < 256; i++)
                dst[i] = 0xFF3F3F3F;
        }
        return;

    case 1: // regular display
        {
            for (int i = 0; i < 256; i+=2)
                *(u64*)&dst[i] = *(u64*)&Output2D[1][i];
        }
        break;
    }

    ApplyMasterBrightness(GPU.MasterBrightnessB, dst);
}

void SoftRenderer::DoCapture(u32 line)
{
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    const u32 captureCnt = GPU.CaptureFrameCnt;
#else
    const u32 captureCnt = GPU.CaptureCnt;
#endif

    u32 width, height;
    u32 sz = (captureCnt >> 20) & 0x3;
    if (sz == 0)
    {
        width = 128;
        height = 128;
    }
    else
    {
        width = 256;
        height = 64 * sz;
    }

    if (line >= height)
        return;

    u32 dstvram = (captureCnt >> 16) & 0x3;
    if (!(GPU.VRAMMap_LCDC & (1<<dstvram)))
        return;

    u16* dst = (u16*)GPU.VRAM[dstvram];
    u32 dstaddr = (((captureCnt >> 18) & 0x3) << 14) + (line * width);
    dst += (dstaddr & 0xFFFF);

    u32* srcA;
    if (captureCnt & (1<<24))
        srcA = Output3D;
    else
        srcA = Output2D[0];

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    if (GetRenderer3D().UsesStructured2DMetadata() && line < kStructuredScreenHeight)
    {
        CaptureLineUses3d[line] = 0;
        if ((captureCnt & (1 << 24)) != 0 && srcA != nullptr)
        {
            std::memcpy(
                &LastDebugCapture3dSource[static_cast<size_t>(line) * kStructuredScreenWidth],
                srcA,
                kStructuredScreenWidth * sizeof(u32));
            CaptureLineUses3d[line] = 1;
            HasLastDebugCapture3dSource = true;
            SapphireDebugCaptureStats.CaptureLineUses3dLines++;
        }
        SapphireDebugCaptureStats.CaptureLines++;
    }
#endif

    u16* srcB = nullptr;
    if (captureCnt & (1<<25))
        srcB = GPU.DispFIFOBuffer;
    else
    {
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        const u32 dispcnt = GPU.CaptureFrameDispCntA;
#else
        const u32 dispcnt = GPU.GPU2D_A.DispCnt;
#endif
        u32 srcvram = (dispcnt >> 18) & 0x3;
        if (GPU.VRAMMap_LCDC & (1<<srcvram))
        {
            srcB = (u16*)GPU.VRAM[srcvram];

            u32 offset = line * 256;
            if (((dispcnt >> 16) & 0x3) != 2)
                offset += (((captureCnt >> 26) & 0x3) << 14);

            srcB += (offset & 0xFFFF);
        }
    }

    static_assert(VRAMDirtyGranularity == 512);
    GPU.VRAMDirty[dstvram][(dstaddr * 2) / VRAMDirtyGranularity] = true;

    switch ((captureCnt >> 29) & 0x3)
    {
    case 0: // source A
        {
            for (u32 i = 0; i < width; i++)
            {
                u32 val = srcA[i];

                u32 r = (val >> 1) & 0x1F;
                u32 g = (val >> 9) & 0x1F;
                u32 b = (val >> 17) & 0x1F;
                u32 a = ((val >> 24) != 0) ? 0x8000 : 0;

                dst[i] = r | (g << 5) | (b << 10) | a;
            }
        }
        break;

    case 1: // source B
        {
            if (srcB)
            {
                for (u32 i = 0; i < width; i++)
                    dst[i] = srcB[i];
            }
            else
            {
                for (u32 i = 0; i < width; i++)
                    dst[i] = 0;
            }
        }
        break;

    case 2: // sources A+B
    case 3:
        {
            u32 eva = captureCnt & 0x1F;
            u32 evb = (captureCnt >> 8) & 0x1F;

            // checkme
            if (eva > 16) eva = 16;
            if (evb > 16) evb = 16;

            if (srcB)
            {
                for (u32 i = 0; i < width; i++)
                {
                    u32 val = srcA[i];

                    u32 rA = (val >> 1) & 0x1F;
                    u32 gA = (val >> 9) & 0x1F;
                    u32 bA = (val >> 17) & 0x1F;
                    u32 aA = ((val >> 24) != 0) ? 1 : 0;

                    val = srcB[i];

                    u32 rB = val & 0x1F;
                    u32 gB = (val >> 5) & 0x1F;
                    u32 bB = (val >> 10) & 0x1F;
                    u32 aB = val >> 15;

                    u32 rD = ((rA * aA * eva) + (rB * aB * evb) + 8) >> 4;
                    u32 gD = ((gA * aA * eva) + (gB * aB * evb) + 8) >> 4;
                    u32 bD = ((bA * aA * eva) + (bB * aB * evb) + 8) >> 4;
                    u32 aD = (eva>0 ? aA : 0) | (evb>0 ? aB : 0);

                    if (rD > 0x1F) rD = 0x1F;
                    if (gD > 0x1F) gD = 0x1F;
                    if (bD > 0x1F) bD = 0x1F;

                    dst[i] = rD | (gD << 5) | (bD << 10) | (aD << 15);
                }
            }
            else
            {
                for (u32 i = 0; i < width; i++)
                {
                    u32 val = srcA[i];

                    u32 rA = (val >> 1) & 0x1F;
                    u32 gA = (val >> 9) & 0x1F;
                    u32 bA = (val >> 17) & 0x1F;
                    u32 aA = ((val >> 24) != 0) ? 1 : 0;

                    u32 rD = ((rA * aA * eva) + 8) >> 4;
                    u32 gD = ((gA * aA * eva) + 8) >> 4;
                    u32 bD = ((bA * aA * eva) + 8) >> 4;
                    u32 aD = (eva>0 ? aA : 0);

                    dst[i] = rD | (gD << 5) | (bD << 10) | (aD << 15);
                }
            }
        }
        break;
    }
}

void SoftRenderer::ApplyMasterBrightness(u16 regval, u32* dst)
{
    u16 mode = regval >> 14;
    if (mode == 1)
    {
        // up
        u32 factor = regval & 0x1F;
        if (factor > 16) factor = 16;

        for (int i = 0; i < 256; i++)
            dst[i] = ColorBrightnessUp(dst[i], factor, 0x0);
    }
    else if (mode == 2)
    {
        // down
        u32 factor = regval & 0x1F;
        if (factor > 16) factor = 16;

        for (int i = 0; i < 256; i++)
            dst[i] = ColorBrightnessDown(dst[i], factor, 0xF);
    }
}

void SoftRenderer::ExpandColor(u32* dst)
{
    // convert to 32-bit BGRA
    // note: 32-bit RGBA would be more straightforward, but
    // BGRA seems to be more compatible (Direct2D soft, cairo...)
    for (int i = 0; i < 256; i+=2)
    {
        u64 c = *(u64*)&dst[i];

        u64 r = (c << 18) & 0xFC000000FC0000;
        u64 g = (c << 2) & 0xFC000000FC00;
        u64 b = (c >> 14) & 0xFC000000FC;
        c = r | g | b;

        *(u64*)&dst[i] = c | ((c & 0x00C0C0C000C0C0C0) >> 6) | 0xFF000000FF000000;
    }
}


bool SoftRenderer::GetFramebuffers(void** top, void** bottom)
{
    int frontbuf = BackBuffer ^ 1;
    *top = Framebuffer[frontbuf][0];
    *bottom = Framebuffer[frontbuf][1];
    return true;
}

}
