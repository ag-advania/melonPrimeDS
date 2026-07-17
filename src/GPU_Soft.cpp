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

namespace melonDS
{

SoftRenderer::SoftRenderer(melonDS::NDS& nds)
    : Renderer(nds.GPU)
{
    const size_t len = 256 * 192;
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
    const size_t len = 256 * 192 * sizeof(u32);
    memset(Framebuffer[0][0], 0, len);
    memset(Framebuffer[0][1], 0, len);
    memset(Framebuffer[1][0], 0, len);
    memset(Framebuffer[1][1], 0, len);

    Rend2D_A->Reset();
    Rend2D_B->Reset();
    Rend3D->Reset();
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    const std::lock_guard<std::mutex> completedFrameLock(CompletedStructuredVulkanFrameMutex);
    StructuredEnginePlanes.fill(0);
    StructuredScreenPlanes.fill(0);
    StructuredScreenLineMeta.fill(0);
    StructuredCapturePlanes.fill(0);
    StructuredCaptureLineValid.fill(0);
    StructuredCaptureLineUses3D.fill(0);
    StructuredEngineLineUsesCapture3D.fill(0);
    StructuredCaptureBackedSourceClassPixels.fill(0);
    StructuredCaptureBackedExplicitSlot.fill(0);
    StructuredCaptureBackedBestClassLines.fill(0);
    StructuredCaptureBacked3DLines = 0;
    StructuredCapture3DSource.fill(0);
    StructuredCapture3DSourceLineValid.fill(0);
    std::fill_n(Structured3DPlaceholderLine, 256, 0x20000000u);
    std::fill_n(StructuredCaptureCompositeLine, 256, 0u);
    StructuredFrameValid = false;
    StructuredCapture3DSourceValid = false;
    StructuredCaptureScreenSwap = false;
    StructuredCaptureCompositeLineValid = false;
    StructuredCapturePreparedThisFrame = false;
    for (auto& completedFrame : CompletedStructuredVulkanFrames)
    {
        completedFrame.Valid = false;
        completedFrame.ScreenSwapAt3D = false;
        completedFrame.FrontBuffer = -1;
        completedFrame.Generation = 0;
    }
    StructuredVulkanGeneration = 0;
#endif
}

void SoftRenderer::Stop()
{
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    {
        const std::lock_guard<std::mutex> completedFrameLock(CompletedStructuredVulkanFrameMutex);
        for (auto& completedFrame : CompletedStructuredVulkanFrames)
            completedFrame.Valid = false;
    }
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
    auto rend3d = dynamic_cast<SoftRenderer3D*>(Rend3D.get());
    if (rend3d->IsThreaded())
        rend3d->SetupRenderThread();
}

void SoftRenderer::PostSavestate()
{
    auto rend3d = dynamic_cast<SoftRenderer3D*>(Rend3D.get());
    if (rend3d->IsThreaded())
        rend3d->EnableRenderThread();
}


void SoftRenderer::SetRenderSettings(RendererSettings& settings)
{
    auto rend3d = dynamic_cast<SoftRenderer3D*>(Rend3D.get());
    rend3d->SetThreaded(settings.Threaded);
}


void SoftRenderer::DrawScanline(u32 line)
{
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    const u32 outputLine = line;
#endif
    u32 *dstA, *dstB;
    u32 dstoffset = 256 * line;
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
        // retrieve 3D output
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        const bool structuredVulkan2D = UseStructuredVulkan2D();
        if (structuredVulkan2D && outputLine == 0u)
        {
            // A completed snapshot is published only by SwapBuffers after all
            // 192 physical output lines have been produced.  Clear validity at
            // the next frame boundary so VCOUNT overrides cannot publish a
            // mixture of two generations.
            StructuredFrameValid = false;
            StructuredCapture3DSource.fill(0);
            StructuredCapture3DSourceLineValid.fill(0);
            StructuredCapture3DSourceValid = false;
            StructuredCaptureCompositeLineValid = false;
            StructuredCapturePreparedThisFrame = false;
            StructuredCaptureBackedSourceClassPixels.fill(0);
            StructuredCaptureBackedExplicitSlot.fill(0);
            StructuredCaptureBackedBestClassLines.fill(0);
            StructuredCaptureBacked3DLines = 0;

            const u32 captureMode = (GPU.CaptureCnt >> 29u) & 0x3u;
            const bool sourceAContributes = captureMode == 0u
                || (captureMode >= 2u && (GPU.CaptureCnt & 0x1Fu) != 0u);
            const bool captureNeeds3D = GPU.CaptureEnable
                && captureMode != 1u
                && sourceAContributes
                && (((GPU.CaptureCnt & (1u << 24u)) != 0u)
                    || ((GPU.GPU2D_A.DispCnt & 0x0108u) == 0x0108u));
            if (captureNeeds3D)
            {
                StructuredCaptureScreenSwap = GPU.ScreenSwap;
                Rend3D->SetCaptureScreenSwapHint(StructuredCaptureScreenSwap);
                Rend3D->BeginCaptureFrame();
                Rend3D->PrepareCaptureFrame();
                StructuredCapturePreparedThisFrame = true;
            }
        }
        Output3D = structuredVulkan2D ? Structured3DPlaceholderLine : Rend3D->GetLine(line);
        if (structuredVulkan2D)
        {
            StructuredEngineLineUsesCapture3D[static_cast<std::size_t>(line)] = 0;
            StructuredEngineLineUsesCapture3D[192u + static_cast<std::size_t>(line)] = 0;
        }
#else
        Output3D = Rend3D->GetLine(line);
#endif

        // draw BG/OBJ layers
        Rend2D_A->DrawScanline(line);
        Rend2D_B->DrawScanline(line);

        // draw the final screen output
        DrawScanlineA(line, dstA);
        DrawScanlineB(line, dstB);

        // perform display capture if enabled
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        if (GPU.CaptureEnable)
        {
            const u32 captureMode = (GPU.CaptureCnt >> 29) & 0x3u;
            const bool sourceAContributes = captureMode == 0u
                || (captureMode >= 2u && (GPU.CaptureCnt & 0x1Fu) != 0u);
            const bool captureNeeds3D = structuredVulkan2D
                && captureMode != 1u
                && sourceAContributes
                && (((GPU.CaptureCnt & (1u << 24u)) != 0u)
                    || ((GPU.GPU2D_A.DispCnt & 0x0108u) == 0x0108u));
            if (captureNeeds3D)
            {
                if (!StructuredCapturePreparedThisFrame)
                {
                    StructuredCaptureScreenSwap = GPU.ScreenSwap;
                    Rend3D->SetCaptureScreenSwapHint(StructuredCaptureScreenSwap);
                    Rend3D->BeginCaptureFrame();
                    Rend3D->PrepareCaptureFrame();
                    StructuredCapturePreparedThisFrame = true;
                }
                Output3D = Rend3D->GetLine(static_cast<int>(line));
                PrepareStructuredCaptureLine(line, Output3D);
            }
            else
                StructuredCaptureCompositeLineValid = false;
            DoCapture(line);
        }
#else
        if (GPU.CaptureEnable)
            DoCapture(line);
#endif
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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        const u32 screenA = GPU.ScreenSwap ? 0u : 1u;
        const u32 screenB = screenA ^ 1u;
        BuildStructuredScreenLine(0, screenA, outputLine, dstA, line >= 192u);
        BuildStructuredScreenLine(1, screenB, outputLine, dstB, line >= 192u);
#endif
        // expand the color from 6-bit to 8-bit
        ExpandColor(dstA);
        ExpandColor(dstB);
    }
    else
    {
        // if the screens are disabled: fill the framebuffer black
        for (int i = 0; i < 256; i++)
        {
            dstA[i] = 0xFF000000;
            dstB[i] = 0xFF000000;
        }
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        const u32 screenA = GPU.ScreenSwap ? 0u : 1u;
        const u32 screenB = screenA ^ 1u;
        BuildStructuredScreenLine(0, screenA, outputLine, dstA, true);
        BuildStructuredScreenLine(1, screenB, outputLine, dstB, true);
#endif
    }
}

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
    u32 captureCnt = GPU.CaptureCnt;

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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    else if (UseStructuredVulkan2D() && StructuredCaptureCompositeLineValid)
        srcA = StructuredCaptureCompositeLine;
#endif
    else
        srcA = Output2D[0];

    u16* srcB = nullptr;
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    u32 structuredSourceBAddress = line * 256u;
    u32 structuredSourceBBank = 4u;
    bool structuredSourceBFromVram = false;
#endif
    if (captureCnt & (1<<25))
        srcB = GPU.DispFIFOBuffer;
    else
    {
        u32 dispcnt = GPU.GPU2D_A.DispCnt;
        u32 srcvram = (dispcnt >> 18) & 0x3;
        if (GPU.VRAMMap_LCDC & (1<<srcvram))
        {
            srcB = (u16*)GPU.VRAM[srcvram];
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
            structuredSourceBBank = srcvram;
            structuredSourceBFromVram = true;
#endif

            u32 offset = line * 256;
            if (((dispcnt >> 16) & 0x3) != 2)
                offset += (((captureCnt >> 26) & 0x3) << 14);

            srcB += (offset & 0xFFFF);
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
            structuredSourceBAddress = offset & 0xFFFFu;
#endif
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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    if (UseStructuredVulkan2D())
    {
        StoreStructuredCaptureLine(
            line,
            width,
            dstvram,
            dstaddr & 0xFFFFu,
            structuredSourceBAddress,
            structuredSourceBBank,
            structuredSourceBFromVram,
            dst);
    }
#endif
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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
namespace
{
u32 StructuredVulkan2DSourceClass(u32 value)
{
    const u32 flags = value >> 24u;
    if (flags == 0u || flags == 0x20u)
        return 0u;
    if ((flags & 0xC0u) == 0x40u)
        return 0u;
    if ((flags & 0x80u) != 0u || (flags & 0x10u) != 0u)
        return 0x10u;
    return flags & 0x0Fu;
}

bool StructuredVulkan2DSourceIsReal2D(u32 sourceClass)
{
    return sourceClass != 0u;
}

bool StructuredVulkan2DIsOpaqueBlack(u32 value)
{
    return value != 0u
        && (value >> 24u) != 0x40u
        && (value & 0x00FFFFFFu) == 0u;
}
}

bool SoftRenderer::UseStructuredVulkan2D() const noexcept
{
    return Rend3D != nullptr && Rend3D->UsesStructured2DMetadata();
}

void SoftRenderer::StoreStructuredEnginePixel(
    u32 engine,
    u32 line,
    u32 x,
    u32 originalVal1,
    u32 originalVal2,
    u32 originalVal3,
    u32 legacyVal1,
    u32 legacyVal2,
    u32 legacyControl)
{
    if (engine >= 2u || line >= 192u || x >= 256u)
        return;

    const std::size_t pixelIndex = static_cast<std::size_t>(line) * 256u + x;
    const std::size_t engineBase = static_cast<std::size_t>(engine) * 3u * StructuredPixelCount;
    const u32 flags0 = originalVal1 >> 24u;
    const u32 flags1 = originalVal2 >> 24u;
    const u32 flags2 = originalVal3 >> 24u;
    const bool slotInPlane0 = (flags0 & 0xC0u) == 0x40u;
    const bool slotInPlane1 = (flags1 & 0xC0u) == 0x40u;
    const bool slotInPlane2 = (flags2 & 0xC0u) == 0x40u;
    const bool has3DSlot = slotInPlane0 || slotInPlane1 || slotInPlane2;
    const bool no3DCoverage =
        (slotInPlane0 && (flags0 & 0x10u) != 0u)
        || (slotInPlane1 && (flags1 & 0x10u) != 0u)
        || (slotInPlane2 && (flags2 & 0x10u) != 0u);
    const u32 legacyAlpha = (legacyControl >> 24u) & 0x0Fu;

    u32 plane0 = legacyVal1;
    u32 plane1 = 0u;
    u32 control = legacyControl;
    const u32 sourceClass0 = StructuredVulkan2DSourceClass(originalVal1);
    const u32 sourceClass1 = StructuredVulkan2DSourceClass(originalVal2);
    const u32 sourceClass2 = StructuredVulkan2DSourceClass(originalVal3);
    bool protectedBlack2D = false;

    // Sapphire classifies capture-backed 3D carried through engine B by the
    // dominant original 2D source class on each completed line. Preserve the
    // same per-frame cadence signal instead of inferring it from final RGB.
    if (engine == 1u
        && ((GPU.GPU2D_B.DispCnt >> 16u) & 0x1u) == 1u
        && StructuredEngineLineUsesCapture3D[192u + static_cast<std::size_t>(line)] != 0u)
    {
        const std::size_t classBase = static_cast<std::size_t>(line) * 17u;
        if (sourceClass0 <= 16u)
            StructuredCaptureBackedSourceClassPixels[classBase + sourceClass0]++;
        if (has3DSlot)
            StructuredCaptureBackedExplicitSlot[static_cast<std::size_t>(line)] = 1u;

        if (x == 255u)
        {
            u32 bestClass = 0u;
            u32 bestCount = 0u;
            if (StructuredCaptureBackedExplicitSlot[static_cast<std::size_t>(line)] == 0u)
            {
                constexpr std::array<u32, 4> candidateClasses{1u, 2u, 4u, 8u};
                for (const u32 candidateClass : candidateClasses)
                {
                    const u32 count =
                        StructuredCaptureBackedSourceClassPixels[classBase + candidateClass];
                    if (count > bestCount)
                    {
                        bestCount = count;
                        bestClass = candidateClass;
                    }
                }
                if (bestCount < 128u)
                    bestClass = 0u;
            }
            StructuredCaptureBacked3DLines++;
            StructuredCaptureBackedBestClassLines[bestClass]++;
        }
    }

    if (has3DSlot)
    {
        bool hasAbovePlane = false;
        if (slotInPlane0)
        {
            plane0 = legacyVal2;
        }
        else if (slotInPlane1)
        {
            plane0 = legacyVal2;
            if (StructuredVulkan2DSourceIsReal2D(sourceClass0))
            {
                plane1 = originalVal1;
                hasAbovePlane = true;
                protectedBlack2D = StructuredVulkan2DIsOpaqueBlack(plane1);
            }
        }
        else
        {
            plane0 = legacyVal1;
            if (StructuredVulkan2DSourceIsReal2D(sourceClass0)
                || StructuredVulkan2DSourceIsReal2D(sourceClass1))
            {
                plane1 = legacyVal1;
                hasAbovePlane = true;
                protectedBlack2D = StructuredVulkan2DIsOpaqueBlack(plane1);
            }
        }

        const u32 structuredAlpha = legacyAlpha
            | 0x40u
            | (hasAbovePlane ? 0x80u : 0u)
            | (no3DCoverage ? 0x10u : 0u)
            | (protectedBlack2D ? 0x20u : 0u);
        control = (legacyControl & 0x00FFFFFFu) | (structuredAlpha << 24u);
    }
    else
    {
        protectedBlack2D =
            (StructuredVulkan2DSourceIsReal2D(sourceClass0)
                || StructuredVulkan2DSourceIsReal2D(sourceClass1)
                || StructuredVulkan2DSourceIsReal2D(sourceClass2))
            && StructuredVulkan2DIsOpaqueBlack(legacyVal1);
        control = (legacyControl & 0x00FFFFFFu)
            | ((legacyAlpha | 0x80u | (protectedBlack2D ? 0x20u : 0u)) << 24u);
    }

    StructuredEnginePlanes[engineBase + pixelIndex] = plane0;
    StructuredEnginePlanes[engineBase + StructuredPixelCount + pixelIndex] = plane1;
    StructuredEnginePlanes[engineBase + (2u * StructuredPixelCount) + pixelIndex] = control;
}

namespace
{
u32 PackedCaptureColorToColor6(u16 color)
{
    const u32 red = (color & 0x001Fu) << 1u;
    const u32 green = (color & 0x03E0u) >> 4u;
    const u32 blue = (color & 0x7C00u) >> 9u;
    const u32 alpha = (color & 0x8000u) != 0u ? 1u : 0u;
    return red | (green << 8u) | (blue << 16u) | (alpha << 24u);
}

u16 Color6ToPackedCaptureColor(u32 color)
{
    return static_cast<u16>(
        ((color >> 1u) & 0x1Fu)
        | (((color >> 9u) & 0x1Fu) << 5u)
        | (((color >> 17u) & 0x1Fu) << 10u)
        | ((color >> 24u) != 0u ? 0x8000u : 0u));
}

bool PackedCaptureColorsClose(u16 lhs, u16 rhs)
{
    if (((lhs ^ rhs) & 0x8000u) != 0u)
        return false;
    const int lhsR = lhs & 0x1Fu;
    const int lhsG = (lhs >> 5u) & 0x1Fu;
    const int lhsB = (lhs >> 10u) & 0x1Fu;
    const int rhsR = rhs & 0x1Fu;
    const int rhsG = (rhs >> 5u) & 0x1Fu;
    const int rhsB = (rhs >> 10u) & 0x1Fu;
    const auto closeChannel = [](int left, int right) {
        return (left > right ? left - right : right - left) <= 2;
    };
    return closeChannel(lhsR, rhsR)
        && closeChannel(lhsG, rhsG)
        && closeChannel(lhsB, rhsB);
}

void PushStructuredRawPixel(u32* destination, u32 value)
{
    destination[512] = destination[256];
    destination[256] = destination[0];
    destination[0] = value;
}
}

void SoftRenderer::PrepareStructuredCaptureLine(u32 line, const u32* exact3DLine)
{
    StructuredCaptureCompositeLineValid = false;
    if (!UseStructuredVulkan2D() || line >= 192u || exact3DLine == nullptr)
        return;

    const std::size_t rowBase = static_cast<std::size_t>(line) * 256u;
    std::memcpy(
        StructuredCapture3DSource.data() + rowBase,
        exact3DLine,
        256u * sizeof(u32));
    StructuredCapture3DSourceLineValid[static_cast<std::size_t>(line)] = 1;
    StructuredCapture3DSourceValid = true;

    const std::size_t engineBase = 0;
    for (std::size_t x = 0; x < 256u; ++x)
    {
        const std::size_t index = rowBase + x;
        const u32 below = StructuredEnginePlanes[engineBase + index];
        const u32 above = StructuredEnginePlanes[engineBase + StructuredPixelCount + index];
        const u32 control = StructuredEnginePlanes[engineBase + (2u * StructuredPixelCount) + index];
        const u32 controlAlpha = control >> 24u;
        if ((controlAlpha & 0x40u) == 0u)
        {
            StructuredCaptureCompositeLine[x] = Output2D[0][x];
            continue;
        }

        const u32 exact3D = exact3DLine[x];
        const u32 compositionMode = controlAlpha & 0xFu;
        if ((exact3D >> 24u) == 0u)
        {
            StructuredCaptureCompositeLine[x] = below;
            continue;
        }

        switch (compositionMode)
        {
        case 1:
            StructuredCaptureCompositeLine[x] = (controlAlpha & 0x80u) != 0u
                ? ColorBlend4(above, exact3D, (control >> 8u) & 0x1Fu, (control >> 16u) & 0x1Fu)
                : exact3D;
            break;
        case 2:
            StructuredCaptureCompositeLine[x] = ColorBrightnessUp(exact3D, (control >> 8u) & 0x1Fu, 0x8u);
            break;
        case 3:
            StructuredCaptureCompositeLine[x] = ColorBrightnessDown(exact3D, (control >> 8u) & 0x1Fu, 0x7u);
            break;
        case 4:
            StructuredCaptureCompositeLine[x] = ColorBlend5(exact3D, below);
            break;
        default:
            StructuredCaptureCompositeLine[x] = exact3D;
            break;
        }
    }
    StructuredCaptureCompositeLineValid = true;
}

void SoftRenderer::StoreStructuredCaptureLine(
    u32 line,
    u32 width,
    u32 destinationBank,
    u32 destinationAddress,
    u32 sourceBAddress,
    u32 sourceBBank,
    bool sourceBFromVram,
    const u16* captureOutput)
{
    if (!UseStructuredVulkan2D() || line >= 192u || destinationBank >= 4u || captureOutput == nullptr)
        return;

    const u32 captureCnt = GPU.CaptureCnt;
    const u32 captureMode = (captureCnt >> 29u) & 0x3u;
    const bool direct3D = (captureCnt & (1u << 24u)) != 0u;
    const u32 eva = std::min<u32>(captureCnt & 0x1Fu, 16u);
    const u32 evb = std::min<u32>((captureCnt >> 8u) & 0x1Fu, 16u);
    const std::size_t sourceARowBase = static_cast<std::size_t>(line) * 256u;
    const std::size_t captureBase = static_cast<std::size_t>(destinationBank) * 3u * StructuredPixelCount;
    bool lineUses3D = false;
    bool wroteMetadata = false;

    const u32 copyWidth = std::min<u32>(width, 256u);
    for (u32 x = 0; x < copyWidth; ++x)
    {
        const u32 captureAddress = (destinationAddress + x) & 0xFFFFu;
        if (captureAddress >= StructuredPixelCount)
            continue;

        const std::size_t destinationIndex = static_cast<std::size_t>(captureAddress);
        const std::size_t sourceAIndex = sourceARowBase + static_cast<std::size_t>(x);
        u32 sourceAPlane0 = StructuredEnginePlanes[sourceAIndex];
        u32 sourceAPlane1 = StructuredEnginePlanes[StructuredPixelCount + sourceAIndex];
        u32 sourceAControl = StructuredEnginePlanes[(2u * StructuredPixelCount) + sourceAIndex];
        bool sourceAHas3D = (sourceAControl >> 24u & 0x40u) != 0u;
        if (direct3D)
        {
            sourceAPlane0 = 0u;
            sourceAPlane1 = 0u;
            sourceAControl = 0x40000000u;
            sourceAHas3D = true;
        }
        const bool sourceA3DCoverageKnown =
            StructuredCapture3DSourceLineValid[static_cast<std::size_t>(line)] != 0u;
        const bool sourceA3DCoverage = sourceA3DCoverageKnown
            && (StructuredCapture3DSource[sourceAIndex] >> 24u) != 0u;

        u32 sourceBPlane0 = 0u;
        u32 sourceBPlane1 = 0u;
        u32 sourceBControl = 0u;
        bool sourceBHas3D = false;
        if (sourceBFromVram && sourceBBank < 4u)
        {
            const u32 address = (sourceBAddress + x) & 0xFFFFu;
            if (address < StructuredPixelCount)
            {
                const std::size_t sourceLine = static_cast<std::size_t>(address / 256u);
                const std::size_t validIndex = static_cast<std::size_t>(sourceBBank) * 192u + sourceLine;
                if (StructuredCaptureLineValid[validIndex] != 0u)
                {
                    const std::size_t sourceBase = static_cast<std::size_t>(sourceBBank) * 3u * StructuredPixelCount;
                    const std::size_t sourceIndex = static_cast<std::size_t>(address);
                    sourceBPlane0 = StructuredCapturePlanes[sourceBase + sourceIndex];
                    sourceBPlane1 = StructuredCapturePlanes[sourceBase + StructuredPixelCount + sourceIndex];
                    sourceBControl = StructuredCapturePlanes[sourceBase + (2u * StructuredPixelCount) + sourceIndex];
                    sourceBHas3D = ((sourceBControl >> 24u) & 0x40u) != 0u;
                }
            }
        }

        const u32 flatOutput = PackedCaptureColorToColor6(captureOutput[x]);
        u32 plane0 = flatOutput;
        u32 plane1 = 0u;
        u32 control = StructuredVulkan2DIsOpaqueBlack(flatOutput)
            ? 0xA7000000u
            : 0x87000000u;
        bool selectedSourceA3D = false;

        if (captureMode == 0u && sourceAHas3D)
        {
            plane0 = sourceAPlane0;
            plane1 = sourceAPlane1;
            control = sourceAControl;
            selectedSourceA3D = true;
        }
        else if (captureMode == 1u && sourceBControl != 0u)
        {
            plane0 = sourceBPlane0;
            plane1 = sourceBPlane1;
            control = sourceBControl;
        }
        else if (captureMode >= 2u && sourceAHas3D && eva != 0u)
        {
            plane0 = flatOutput;
            const u16 sourceBPacked = (captureCnt & (1u << 25u)) != 0u
                ? GPU.DispFIFOBuffer[x]
                : (sourceBFromVram && sourceBBank < 4u
                    ? reinterpret_cast<const u16*>(GPU.VRAM[sourceBBank])[(sourceBAddress + x) & 0xFFFFu]
                    : 0u);
            plane1 = ((sourceBPacked & 0x8000u) != 0u && evb != 0u)
                ? PackedCaptureColorToColor6(sourceBPacked)
                : 0u;
            const u32 structuredAlpha = 0x41u | (plane1 != 0u ? 0x80u : 0u);
            control = (structuredAlpha << 24u)
                | ((eva & 0x1Fu) << 16u)
                | ((evb & 0x1Fu) << 8u);
            if (StructuredVulkan2DIsOpaqueBlack(plane1))
                control |= 0x20000000u;
            selectedSourceA3D = true;
        }
        else if (captureMode >= 2u && sourceBHas3D && evb != 0u)
        {
            plane0 = flatOutput;
            plane1 = (eva != 0u) ? sourceAPlane0 : 0u;
            const u32 sourceBNoCoverage = (sourceBControl >> 24u) & 0x10u;
            const u32 structuredAlpha = 0x41u
                | (plane1 != 0u ? 0x80u : 0u)
                | sourceBNoCoverage;
            control = (structuredAlpha << 24u)
                | ((evb & 0x1Fu) << 16u)
                | ((eva & 0x1Fu) << 8u);
            if (StructuredVulkan2DIsOpaqueBlack(plane1))
                control |= 0x20000000u;
        }
        if (selectedSourceA3D && sourceA3DCoverageKnown && !sourceA3DCoverage)
            control |= 0x10000000u;

        // Preserve a real 2D overlay carried by structured source B when its
        // color survives the hardware A+B capture blend. This is Sapphire's
        // capture-overlay merge contract and keeps opaque black UI pixels
        // distinct from an empty 3D slot.
        if (captureMode >= 2u && sourceBFromVram && evb != 0u && sourceBControl != 0u)
        {
            const u32 sourceBAlpha = sourceBControl >> 24u;
            const bool sourceBSlot = (sourceBAlpha & 0x40u) != 0u;
            u32 overlayPixel = 0u;
            if (sourceBSlot && (sourceBAlpha & 0x80u) != 0u && sourceBPlane1 != 0u)
                overlayPixel = sourceBPlane1;
            else if (!sourceBSlot && (sourceBAlpha & 0x80u) != 0u && sourceBPlane0 != 0u)
                overlayPixel = sourceBPlane0;

            if (overlayPixel != 0u
                && PackedCaptureColorsClose(
                    Color6ToPackedCaptureColor(overlayPixel),
                    captureOutput[x]))
            {
                u32 destinationAlpha = control >> 24u;
                const u32 protectedBlack = sourceBAlpha & 0x20u;
                if ((destinationAlpha & 0x40u) != 0u)
                {
                    plane1 = overlayPixel;
                    destinationAlpha |= 0x80u | protectedBlack;
                }
                else
                {
                    plane0 = overlayPixel;
                    const u32 compMode = destinationAlpha & 0x0Fu;
                    destinationAlpha = (compMode <= 7u ? compMode : 5u)
                        | 0x80u
                        | protectedBlack;
                }
                control = (control & 0x00FFFFFFu) | (destinationAlpha << 24u);
            }
        }

        StructuredCapturePlanes[captureBase + destinationIndex] = plane0;
        StructuredCapturePlanes[captureBase + StructuredPixelCount + destinationIndex] = plane1;
        StructuredCapturePlanes[captureBase + (2u * StructuredPixelCount) + destinationIndex] = control;
        lineUses3D = lineUses3D || (((control >> 24u) & 0x40u) != 0u);
        wroteMetadata = true;
    }

    if (wroteMetadata)
    {
        const std::size_t destinationLine = static_cast<std::size_t>((destinationAddress & 0xFFFFu) / 256u);
        if (destinationLine < 192u)
        {
            const std::size_t validIndex = static_cast<std::size_t>(destinationBank) * 192u + destinationLine;
            StructuredCaptureLineValid[validIndex] = 1;
            StructuredCaptureLineUses3D[validIndex] = lineUses3D ? 1 : 0;
        }
    }
}

bool SoftRenderer::DrawStructuredCapturePixel(u32 engine, u32* destination, u32 flatByteAddress)
{
    if (!UseStructuredVulkan2D() || destination == nullptr || engine >= 2u)
        return false;

    const u32 maskedAddress = flatByteAddress & (engine != 0u ? 0x1FFFFu : 0x7FFFFu);
    const u32 mapMask = engine != 0u
        ? GPU.VRAMMap_BBG[(maskedAddress >> 14u) & 0x7u]
        : GPU.VRAMMap_ABG[(maskedAddress >> 14u) & 0x1Fu];
    const u32 captureAddress = (maskedAddress & 0x1FFFFu) >> 1u;
    if (captureAddress >= StructuredPixelCount)
        return false;

    for (u32 bank = 0; bank < 4u; ++bank)
    {
        if ((mapMask & (1u << bank)) == 0u)
            continue;
        const std::size_t validIndex = static_cast<std::size_t>(bank) * 192u + (captureAddress / 256u);
        if (StructuredCaptureLineValid[validIndex] == 0u)
            continue;

        const std::size_t captureBase = static_cast<std::size_t>(bank) * 3u * StructuredPixelCount;
        const std::size_t index = static_cast<std::size_t>(captureAddress);
        const u32 below = StructuredCapturePlanes[captureBase + index];
        const u32 above = StructuredCapturePlanes[captureBase + StructuredPixelCount + index];
        const u32 control = StructuredCapturePlanes[captureBase + (2u * StructuredPixelCount) + index];
        const u32 controlAlpha = control >> 24u;
        if ((controlAlpha & 0x40u) != 0u)
        {
            if (below != 0u)
                PushStructuredRawPixel(destination, below);
            PushStructuredRawPixel(
                destination,
                0x40000000u | ((controlAlpha & 0x10u) << 24u));
            if ((controlAlpha & 0x80u) != 0u && above != 0u)
                PushStructuredRawPixel(destination, above);
            const u32 line = std::min<u32>(GPU.VCount, 191u);
            StructuredEngineLineUsesCapture3D[static_cast<std::size_t>(engine) * 192u + line] = 1;
            return true;
        }
        if ((controlAlpha & 0x80u) != 0u && below != 0u)
        {
            PushStructuredRawPixel(destination, below);
            return true;
        }
    }
    return false;
}

void SoftRenderer::BuildStructuredScreenLine(
    u32 engine,
    u32 screen,
    u32 line,
    const u32* output,
    bool forcePlain)
{
    if (!UseStructuredVulkan2D() || engine >= 2u || screen >= 2u || line >= 192u || output == nullptr)
        return;

    const u32 displayMode = engine == 0u
        ? ((GPU.GPU2D_A.DispCnt >> 16u) & 0x3u)
        : ((GPU.GPU2D_B.DispCnt >> 16u) & 0x1u);
    const std::size_t rowBase = static_cast<std::size_t>(line) * 256u;
    const std::size_t sourceBase = static_cast<std::size_t>(engine) * 3u * StructuredPixelCount;
    const std::size_t destinationBase = static_cast<std::size_t>(screen) * 3u * StructuredPixelCount;

    bool copiedStructured = false;
    u32 lineMeta = 0u;
    if (!forcePlain && displayMode == 1u)
    {
        for (std::size_t plane = 0; plane < 3u; ++plane)
        {
            std::memcpy(
                StructuredScreenPlanes.data() + destinationBase + (plane * StructuredPixelCount) + rowBase,
                StructuredEnginePlanes.data() + sourceBase + (plane * StructuredPixelCount) + rowBase,
                256u * sizeof(u32));
        }
        const u16 brightness = engine == 0u ? GPU.MasterBrightnessA : GPU.MasterBrightnessB;
        lineMeta =
            (1u << 16u)
            | (static_cast<u32>(brightness >> 14u) << 8u)
            | static_cast<u32>(brightness & 0x1Fu);
        if (StructuredEngineLineUsesCapture3D[(static_cast<std::size_t>(engine) * 192u) + line] != 0u)
            lineMeta |= 1u << 21u;
        copiedStructured = true;
    }
    else if (!forcePlain && engine == 0u && displayMode == 2u)
    {
        const u32 bank = (GPU.GPU2D_A.DispCnt >> 18u) & 0x3u;
        const std::size_t validIndex = static_cast<std::size_t>(bank) * 192u + line;
        if ((GPU.VRAMMap_LCDC & (1u << bank)) != 0u && StructuredCaptureLineValid[validIndex] != 0u)
        {
            const std::size_t captureBase = static_cast<std::size_t>(bank) * 3u * StructuredPixelCount;
            for (std::size_t plane = 0; plane < 3u; ++plane)
            {
                std::memcpy(
                    StructuredScreenPlanes.data() + destinationBase + (plane * StructuredPixelCount) + rowBase,
                    StructuredCapturePlanes.data() + captureBase + (plane * StructuredPixelCount) + rowBase,
                    256u * sizeof(u32));
            }
            const u16 brightness = GPU.MasterBrightnessA;
            lineMeta =
                (2u << 16u)
                | (static_cast<u32>(brightness >> 14u) << 8u)
                | static_cast<u32>(brightness & 0x1Fu);
            if (StructuredCaptureLineUses3D[validIndex] != 0u)
                lineMeta |= 1u << 22u;
            copiedStructured = true;
        }
    }

    if (!copiedStructured)
    {
        for (std::size_t x = 0; x < 256u; ++x)
        {
            const std::size_t pixelIndex = rowBase + x;
            StructuredScreenPlanes[destinationBase + pixelIndex] =
                (output[x] & 0x00FFFFFFu) | 0x01000000u;
            StructuredScreenPlanes[destinationBase + StructuredPixelCount + pixelIndex] = 0;
            const bool protectedBlack = StructuredVulkan2DSourceClass(output[x]) != 0u
                && StructuredVulkan2DIsOpaqueBlack(output[x]);
            StructuredScreenPlanes[destinationBase + (2u * StructuredPixelCount) + pixelIndex] =
                protectedBlack ? 0xA7000000u : 0x87000000u;
        }
        lineMeta = (forcePlain ? 0u : displayMode) << 16u;
    }
    StructuredScreenLineMeta[(static_cast<std::size_t>(screen) * 192u) + line] = lineMeta;
    if (line == 191u)
        StructuredFrameValid = true;
}

bool SoftRenderer::CopyStructuredVulkanFrame(StructuredVulkanFrameSnapshot& snapshot) const
{
    snapshot.Valid = false;
    if (!UseStructuredVulkan2D())
        return false;

    const std::lock_guard<std::mutex> completedFrameLock(CompletedStructuredVulkanFrameMutex);
    const StructuredVulkanFrameSnapshot* completedFrame = nullptr;
    for (const auto& candidate : CompletedStructuredVulkanFrames)
    {
        if (candidate.Valid
            && (completedFrame == nullptr || candidate.Generation > completedFrame->Generation))
        {
            completedFrame = &candidate;
        }
    }
    if (completedFrame == nullptr)
        return false;

    snapshot = *completedFrame;
    return true;
}

void SoftRenderer::SwapBuffers()
{
    if (UseStructuredVulkan2D() && StructuredFrameValid)
    {
        const std::lock_guard<std::mutex> completedFrameLock(CompletedStructuredVulkanFrameMutex);
        StructuredVulkanFrameSnapshot& completedFrame =
            CompletedStructuredVulkanFrames[static_cast<std::size_t>(BackBuffer & 1)];
        completedFrame.ScreenPlanes = StructuredScreenPlanes;
        completedFrame.ScreenLineMeta = StructuredScreenLineMeta;
        completedFrame.Capture3DSource = StructuredCapture3DSource;
        completedFrame.CaptureLineUses3D = StructuredCapture3DSourceLineValid;
        completedFrame.HasCapture3DSource = StructuredCapture3DSourceValid;
        completedFrame.CaptureScreenSwap = StructuredCaptureScreenSwap;
        completedFrame.ScreenSwapAt3D = GPU.GPU3D.GetRenderScreenSwapAt3D();
        completedFrame.CaptureBackedClass4Only =
            StructuredCaptureBacked3DLines > 0u
            && StructuredCaptureBackedBestClassLines[4] == StructuredCaptureBacked3DLines
            && StructuredCaptureBackedBestClassLines[0] == 0u
            && StructuredCaptureBackedBestClassLines[1] == 0u
            && StructuredCaptureBackedBestClassLines[2] == 0u
            && StructuredCaptureBackedBestClassLines[8] == 0u
            && StructuredCaptureBackedBestClassLines[16] == 0u;
        completedFrame.FrontBuffer = BackBuffer & 1;
        completedFrame.Generation = ++StructuredVulkanGeneration;
        completedFrame.Valid = true;
    }

    Renderer::SwapBuffers();
}
#endif


bool SoftRenderer::GetFramebuffers(void** top, void** bottom)
{
    int frontbuf = BackBuffer ^ 1;
    *top = Framebuffer[frontbuf][0];
    *bottom = Framebuffer[frontbuf][1];
    return true;
}

}
