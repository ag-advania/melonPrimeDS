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
#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
#include "MelonPrimeStructuredComposition.h"
#include "VulkanPerf.h"
#endif

namespace melonDS
{

#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
// Canonical bit layout of the structured 2D contract this renderer publishes.
// The DX12 and Vulkan compositors decode the same values.
namespace Contract = StructuredComposition;
#endif

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
#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
    StructuredEnginePlanes.fill(0);
    StructuredScreenPlanes.fill(0);
    StructuredScreenSource.fill(Contract::kScreenSourceFallback);
    StructuredScreenLineMeta.fill(0);
    StructuredCapturePlanes.fill(0);
    StructuredCaptureLineValid.fill(0);
    StructuredCapturePixelValid.fill(0);
    StructuredCapturePixelVersion.fill(0);
    StructuredCaptureBankVersion.fill(0);
    StructuredCaptureBankWrittenThisFrame.fill(0);
    StructuredCaptureCommandWrittenThisFrame.fill(0);
    StructuredCaptureSourceBWidthThisFrame.fill(0);
    StructuredCaptureSourceBNative.fill(0);
    StructuredCaptureSourceBReference.fill(0);
    StructuredCaptureCommands.fill(0);
    std::fill_n(Structured3DPlaceholderLine, 256, StructuredComposition::k3DPlaceholderPixel);
    std::fill_n(StructuredCaptureCompositeLine, 256, 0u);
    StructuredFrameValid = false;
    StructuredCaptureCompositeLineValid = false;
    StructuredCapturePreparedThisFrame = false;
    StructuredCapture3DValid = false;
    StructuredFrameNativeMenuHeld = false;
    NativeMenuHeldForFrame = false;
    StructuredScreenRouteCopyBytes = 0;
    StructuredScreenRouteCopyNanoseconds = 0;
    StructuredRegularLines = 0;
    StructuredFallbackLines = 0;
    StructuredFrameGeneration = 0;
    StructuredContentGeneration = {};
    StructuredPendingPlaneDirtyMask = 0;
    StructuredPendingLineMetaDirtyMask = 0;
    StructuredPendingCaptureCommandsDirty = false;
    StructuredEngineChangedMask[0] = 0;
    StructuredEngineChangedMask[1] = 0;
#endif
}

void SoftRenderer::Stop()
{
    // clear framebuffers to black
    const size_t len = 256 * 192 * sizeof(u32);
    memset(Framebuffer[0][0], 0, len);
    memset(Framebuffer[0][1], 0, len);
    memset(Framebuffer[1][0], 0, len);
    memset(Framebuffer[1][1], 0, len);
}

void SoftRenderer::AllocCapture(u32 bank, u32 start, u32 len)
{
    (void)bank;
    (void)start;
    (void)len;
    // Claiming a destination is not an invalidation. The old pixels are still
    // the source for same-bank Display Capture until each new pixel is written;
    // OpenGL preserves them in CaptureVRAMTex for the same reason. GPU.cpp calls
    // InvalidateVRAMCapture explicitly when a capture is actually retired or
    // CPU/DMA writes make emulated VRAM authoritative.
}

void SoftRenderer::SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete)
{
    (void)bank;
    (void)start;
    (void)len;
    (void)complete;
    // Native VRAM is already updated line-by-line by this renderer. A read-only
    // synchronization must not discard the high-resolution sidecar; OpenGL's
    // SyncVRAMCapture likewise keeps its capture texture after downscaling.
}

void SoftRenderer::InvalidateVRAMCapture(u32 bank, u32 start, u32 len)
{
#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
    InvalidateStructuredCaptureBlocks(bank, start, len);
#else
    (void)bank;
    (void)start;
    (void)len;
#endif
}

#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
void SoftRenderer::InvalidateStructuredCaptureBlocks(u32 bank, u32 start, u32 len)
{
    if (!UseStructuredVulkan2D() || bank >= 4u)
        return;

    // `len` is the DS capture size field: 0 means one 128x128 block, otherwise
    // it counts 64-line blocks, matching GLRenderer's own interpretation.
    const u32 blockCount = len == 0u ? 1u : std::min<u32>(len, 3u);
    const std::size_t bankLineBase = static_cast<std::size_t>(bank) * StructuredCaptureLineCount;
    const std::size_t bankPlaneBase =
        static_cast<std::size_t>(bank) * 3u * StructuredCapturePixelCount;
    for (u32 blockOffset = 0; blockOffset < blockCount; ++blockOffset)
    {
        const u32 block = (start + blockOffset) & 0x3u;
        const std::size_t firstLine = static_cast<std::size_t>(block) * 64u;
        std::fill_n(StructuredCaptureLineValid.data() + bankLineBase + firstLine, 64u, 0u);
        const std::size_t bankPixelBase = static_cast<std::size_t>(bank) * StructuredCapturePixelCount;
        const std::size_t firstPixelInBank = firstLine * 256u;
        std::fill_n(
            StructuredCapturePixelValid.data() + bankPixelBase + firstPixelInBank,
            64u * 256u,
            0u);

        const std::size_t firstPixel = firstLine * 256u;
        for (std::size_t plane = 0; plane < 3u; ++plane)
        {
            std::fill_n(
                StructuredCapturePlanes.data()
                    + bankPlaneBase + plane * StructuredCapturePixelCount + firstPixel,
                64u * 256u,
                0u);
        }
    }
}
#endif


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
#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
    const u32 outputLine = line;
    const bool measureStructured2D = outputLine < 192u && UseStructuredVulkan2D();
    bool structuredVramDisplaySnapshotted = false;
    if (measureStructured2D && outputLine == 0u)
        VulkanPerf::BeginStructured2DFrame();
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
#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
        const bool structuredVulkan2D = UseStructuredVulkan2D();
        if (structuredVulkan2D && outputLine == 0u)
        {
            ++StructuredFrameGeneration;
            StructuredFrameValid = false;
            StructuredCaptureCompositeLineValid = false;
            StructuredCapturePreparedThisFrame = false;
            StructuredCapture3DValid = false;
            StructuredCaptureBankWrittenThisFrame.fill(0);
            StructuredCaptureCommandWrittenThisFrame.fill(0);
            StructuredCaptureSourceBWidthThisFrame.fill(0);
            StructuredScreenRouteCopyBytes = 0;
            StructuredScreenRouteCopyNanoseconds = 0;
            StructuredRegularLines = 0;
            StructuredFallbackLines = 0;
            StructuredPendingPlaneDirtyMask = 0;
            StructuredPendingLineMetaDirtyMask = 0;
            StructuredPendingCaptureCommandsDirty = false;
            StructuredEngineChangedMask[0] = 0;
            StructuredEngineChangedMask[1] = 0;

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
                Rend3D->BeginCaptureFrame();
                Rend3D->PrepareCaptureFrame();
                StructuredCapturePreparedThisFrame = true;
            }
        }
        Output3D = structuredVulkan2D ? Structured3DPlaceholderLine : Rend3D->GetLine(line);
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
#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
        if (structuredVulkan2D && GPU.ScreensEnabled)
        {
            const u32 screenA = GPU.ScreenSwap ? 0u : 1u;
            structuredVramDisplaySnapshotted =
                SnapshotStructuredVramDisplayLine(screenA, outputLine, line);
        }

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
                    Rend3D->BeginCaptureFrame();
                    Rend3D->PrepareCaptureFrame();
                    StructuredCapturePreparedThisFrame = true;
                }
                Output3D = Rend3D->GetLine(static_cast<int>(line));
                StructuredCapture3DValid = Rend3D->HasValidCaptureFrame();
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
#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
        const u32 screenA = GPU.ScreenSwap ? 0u : 1u;
        const u32 screenB = screenA ^ 1u;
        BuildStructuredScreenLine(
            0, screenA, outputLine, dstA, line >= 192u,
            structuredVramDisplaySnapshotted);
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
#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
        const u32 screenA = GPU.ScreenSwap ? 0u : 1u;
        const u32 screenB = screenA ^ 1u;
        BuildStructuredScreenLine(0, screenA, outputLine, dstA, true);
        BuildStructuredScreenLine(1, screenB, outputLine, dstB, true);
#endif
    }
#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
    if (UseStructuredVulkan2D() && outputLine == 191u)
        FinalizeStructuredCaptureFrame();
    if (UseStructuredVulkan2D() && outputLine == 191u)
        FlushStructuredGeneration();
    if (measureStructured2D && outputLine == 191u)
        VulkanPerf::EndStructured2DFrame();
#endif
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
#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
    else if (UseStructuredVulkan2D() && StructuredCaptureCompositeLineValid)
        srcA = StructuredCaptureCompositeLine;
#endif
    else
        srcA = Output2D[0];

    u16* srcB = nullptr;
    u32 srcBbank = 0xFFFFFFFFu;
    u32 srcBaddr = 0u;
    if (captureCnt & (1<<25))
        srcB = GPU.DispFIFOBuffer;
    else
    {
        u32 dispcnt = GPU.GPU2D_A.DispCnt;
        u32 srcvram = (dispcnt >> 18) & 0x3;
        if (GPU.VRAMMap_LCDC & (1<<srcvram))
        {
            srcBbank = srcvram;
            srcB = (u16*)GPU.VRAM[srcvram];
            u32 offset = line * 256;
            if (((dispcnt >> 16) & 0x3) != 2)
                offset += (((captureCnt >> 26) & 0x3) << 14);

            srcBaddr = offset & 0xFFFFu;
            srcB += srcBaddr;
        }
    }

#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
    if (UseStructuredVulkan2D())
    {
        RecordStructuredCaptureLine(
            line,
            width,
            dstvram,
            dstaddr & 0xFFFFu,
            captureCnt,
            srcB,
            srcBbank,
            srcBaddr);
    }
#endif

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
#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
    if (UseStructuredVulkan2D())
    {
        StoreStructuredCaptureLine(
            line,
            width,
            dstvram,
            dstaddr & 0xFFFFu,
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

#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
bool SoftRenderer::UseStructuredVulkan2D() const noexcept
{
    return Rend3D != nullptr && Rend3D->UsesStructured2DMetadata();
}

namespace
{
u32 PackedCaptureColorToColor6(u16 color)
{
    const u32 red = (color & 0x001Fu) << 1u;
    const u32 green = (color & 0x03E0u) >> 4u;
    const u32 blue = (color & 0x7C00u) >> 9u;
    // A captured RGBA5551 alpha bit becomes the DS 3D pipeline's full 5-bit
    // alpha when the capture is later sampled as a direct-colour texture.
    // Keeping it as 1 only happened to work for 2D's boolean alpha test and
    // made retained captures almost transparent in 3D blend modes.
    const u32 alpha = (color & 0x8000u) != 0u ? 31u : 0u;
    return red | (green << 8u) | (blue << 16u) | (alpha << 24u);
}

u16 Color6ToPackedCaptureColor(u32 color)
{
    const u16 red = static_cast<u16>((color >> 1u) & 0x1Fu);
    const u16 green = static_cast<u16>((color >> 9u) & 0x1Fu);
    const u16 blue = static_cast<u16>((color >> 17u) & 0x1Fu);
    const u16 alpha = (color >> 24u) != 0u ? 0x8000u : 0u;
    return static_cast<u16>(red | (green << 5u) | (blue << 10u) | alpha);
}

}

bool SoftRenderer::SnapshotStructuredVramDisplayLine(
    u32 screen,
    u32 outputLine,
    u32 sourceLine)
{
    namespace Contract = StructuredComposition;
    if (!UseStructuredVulkan2D() || screen >= 2u
        || outputLine >= 192u || sourceLine >= 192u)
    {
        return false;
    }

    const u32 displayControl = GPU.GPU2D_A.DispCnt;
    if (((displayControl >> 16u) & 0x3u) != Contract::kDisplayModeVram)
        return false;

    const u32 bank = (displayControl >> 18u) & 0x3u;
    if ((GPU.VRAMMap_LCDC & (1u << bank)) == 0u)
        return false;

    const std::size_t destinationRow = static_cast<std::size_t>(outputLine) * 256u;
    const std::size_t sourceRow = static_cast<std::size_t>(sourceLine) * 256u;
    const std::size_t captureBase =
        static_cast<std::size_t>(bank) * 3u * StructuredCapturePixelCount;
    const std::size_t stateBase =
        static_cast<std::size_t>(bank) * StructuredCapturePixelCount;
    const u16* nativeVram = reinterpret_cast<const u16*>(GPU.VRAM[bank]);
    u8 changedPlaneMask = 0;

    for (u32 x = 0; x < 256u; ++x)
    {
        const std::size_t destination = destinationRow + x;
        const u32 address = static_cast<u32>(sourceRow + x);
        const u16 native = nativeVram[address];
        const std::size_t stateIndex = stateBase + address;

        // VRAM display ignores bit 15 for visibility. Plane 0 therefore always
        // carries the display-time native RGB fallback, including the half of
        // a 128-wide row that has no retained high-resolution capture.
        StoreStructuredScreenPlaneWord(
            screen, 0u, destination, PackedCaptureColorToColor6(native), &changedPlaneMask);
        StoreStructuredScreenPlaneWord(screen, 1u, destination, 0u, &changedPlaneMask);
        StoreStructuredScreenPlaneWord(
            screen, 2u, destination,
            Contract::kControlPlain2D << Contract::kControlFlagShift, &changedPlaneMask);

        u32 reference = 0u;
        if (StructuredCapturePixelValid[stateIndex] != 0u
            && Color6ToPackedCaptureColor(
                StructuredCapturePlanes[captureBase + address]) == native)
        {
            reference = Contract::PackCaptureReference(
                bank, StructuredCapturePixelVersion[stateIndex], address);
        }
        StoreStructuredScreenPlaneWord(screen, Contract::kPlaneCaptureReference,
            destination, reference, &changedPlaneMask);
    }
    for (u32 plane = 0; plane < Contract::kPlaneCount; ++plane)
    {
        if (changedPlaneMask & static_cast<u8>(1u << plane))
            MarkStructuredPlaneDirty(screen * Contract::kPlaneCount + plane);
    }

    const u16 brightness = GPU.MasterBrightnessA;
    u32 lineMeta =
        (Contract::kDisplayModeVram << Contract::kLineMetaDisplayModeShift)
        | (static_cast<u32>(brightness >> 14u) << Contract::kLineMetaBrightnessModeShift)
        | static_cast<u32>(brightness & Contract::kLineMetaBrightnessFactorMask);
    lineMeta |= (static_cast<u32>(GPU.GPU3D.GetRenderXPos())
            & Contract::kLineMetaRenderXPosMask)
        << Contract::kLineMetaRenderXPosShift;
    u32& lineMetaDestination =
        StructuredScreenLineMeta[static_cast<std::size_t>(screen) * 192u + outputLine];
    if (lineMetaDestination != lineMeta)
    {
        lineMetaDestination = lineMeta;
        MarkStructuredLineMetaDirty(screen);
    }
    return true;
}

void SoftRenderer::PrepareStructuredCaptureLine(u32 line, const u32* exact3DLine)
{
    StructuredCaptureCompositeLineValid = false;
    if (!UseStructuredVulkan2D() || line >= 192u || exact3DLine == nullptr)
        return;

    const std::size_t rowBase = static_cast<std::size_t>(line) * 256u;
    const std::size_t engineBase = 0;
    for (std::size_t x = 0; x < 256u; ++x)
    {
        const std::size_t index = rowBase + x;
        const u32 below = StructuredEnginePlanes[engineBase + index];
        const u32 above = StructuredEnginePlanes[engineBase + StructuredPixelCount + index];
        const u32 control = StructuredEnginePlanes[engineBase + (2u * StructuredPixelCount) + index];
        const u32 captureReference = StructuredEnginePlanes[
            engineBase + (Contract::kPlaneCaptureReference * StructuredPixelCount) + index];
        const u32 controlAlpha = control >> Contract::kControlFlagShift;
        if ((controlAlpha & Contract::kControlHas3DSlot) == 0u)
        {
            StructuredCaptureCompositeLine[x] = Output2D[0][x];
            continue;
        }

        u32 exact3D = exact3DLine[x];
        if ((captureReference & Contract::kCaptureReferenceValid) != 0u)
        {
            const u32 address = captureReference & Contract::kCaptureReferenceAddressMask;
            const u32 bank =
                (captureReference >> Contract::kCaptureReferenceBankShift) & 3u;
            const u32 version =
                (captureReference >> Contract::kCaptureReferenceVersionShift) & 1u;
            const std::size_t stateIndex =
                static_cast<std::size_t>(bank) * StructuredCapturePixelCount + address;
            if (StructuredCapturePixelValid[stateIndex] != 0u
                && StructuredCapturePixelVersion[stateIndex] == version)
            {
                const std::size_t captureBase =
                    static_cast<std::size_t>(bank) * 3u * StructuredCapturePixelCount;
                exact3D = StructuredCapturePlanes[captureBase + address];
            }
        }
        const u32 compositionMode = controlAlpha & Contract::kControlCompositionModeMask;
        if ((exact3D >> 24u) == 0u)
        {
            StructuredCaptureCompositeLine[x] = below;
            continue;
        }

        const u32 eva = (control >> Contract::kControlEvaShift) & Contract::kControlBlendFactorMask;
        const u32 evb = (control >> Contract::kControlEvbShift) & Contract::kControlBlendFactorMask;
        switch (compositionMode)
        {
        case Contract::kCompositionModeBlend4:
            StructuredCaptureCompositeLine[x] = (controlAlpha & Contract::kControlAbovePlane) != 0u
                ? ColorBlend4(above, exact3D, eva, evb)
                : exact3D;
            break;
        case Contract::kCompositionModeBrightnessUp:
            StructuredCaptureCompositeLine[x] = ColorBrightnessUp(exact3D, eva, 0x8u);
            break;
        case Contract::kCompositionModeBrightnessDown:
            StructuredCaptureCompositeLine[x] = ColorBrightnessDown(exact3D, eva, 0x7u);
            break;
        case Contract::kCompositionModeBlend5:
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
    const u16* captureOutput)
{
    if (!UseStructuredVulkan2D() || line >= 192u || destinationBank >= 4u || captureOutput == nullptr)
        return;

    const std::size_t captureBase =
        static_cast<std::size_t>(destinationBank) * 3u * StructuredCapturePixelCount;

    const u32 copyWidth = std::min<u32>(width, 256u);
    const u32 version = StructuredCaptureBankVersion[destinationBank] & 1u;
    for (u32 x = 0; x < copyWidth; ++x)
    {
        const u32 captureAddress = (destinationAddress + x) & 0xFFFFu;
        const std::size_t destinationIndex = static_cast<std::size_t>(captureAddress);
        StructuredCapturePlanes[captureBase + destinationIndex] =
            PackedCaptureColorToColor6(captureOutput[x]);
        StructuredCapturePlanes[captureBase + StructuredCapturePixelCount + destinationIndex] = 0u;
        StructuredCapturePlanes[
            captureBase + (2u * StructuredCapturePixelCount) + destinationIndex] =
            Contract::kControlPlain2D << Contract::kControlFlagShift;
        const std::size_t stateIndex =
            static_cast<std::size_t>(destinationBank) * StructuredCapturePixelCount
            + destinationIndex;
        StructuredCapturePixelValid[stateIndex] = 1u;
        StructuredCapturePixelVersion[stateIndex] = static_cast<u8>(version);
    }

    if (copyWidth != 0u)
    {
        const std::size_t destinationLine = static_cast<std::size_t>((destinationAddress & 0xFFFFu) / 256u);
        if (destinationLine < StructuredCaptureLineCount)
        {
            const std::size_t validIndex =
                static_cast<std::size_t>(destinationBank) * StructuredCaptureLineCount + destinationLine;
            StructuredCaptureLineValid[validIndex] = 1;
        }
    }
}

u32 SoftRenderer::GetStructuredCaptureReference(
    u32 engine,
    u32 flatByteAddress,
    u16 nativeColor,
    bool object) const
{
    if (!UseStructuredVulkan2D() || engine >= 2u)
        return 0u;

    const u32 maskedAddress = flatByteAddress
        & (engine != 0u ? 0x1FFFFu : (object ? 0x3FFFFu : 0x7FFFFu));
    const u32 mapMask = object
        ? (engine != 0u
            ? GPU.VRAMMap_BOBJ[(maskedAddress >> 14u) & 0x7u]
            : GPU.VRAMMap_AOBJ[(maskedAddress >> 14u) & 0xFu])
        : (engine != 0u
            ? GPU.VRAMMap_BBG[(maskedAddress >> 14u) & 0x7u]
            : GPU.VRAMMap_ABG[(maskedAddress >> 14u) & 0x1Fu]);
    // Software VRAM reads OR overlapping banks. A retained capture plane only
    // represents one bank, so overlapping mappings must use the normal path.
    if (mapMask == 0u || (mapMask & (mapMask - 1u)) != 0u)
        return 0u;
    const u32 captureAddress = (maskedAddress & 0x1FFFFu) >> 1u;
    if (captureAddress >= StructuredCapturePixelCount)
        return 0u;

    for (u32 bank = 0; bank < 4u; ++bank)
    {
        if ((mapMask & (1u << bank)) == 0u)
            continue;
        const std::size_t stateIndex =
            static_cast<std::size_t>(bank) * StructuredCapturePixelCount + captureAddress;
        if (StructuredCapturePixelValid[stateIndex] == 0u)
            continue;

        const std::size_t captureBase =
            static_cast<std::size_t>(bank) * 3u * StructuredCapturePixelCount;
        const std::size_t index = static_cast<std::size_t>(captureAddress);
        const u32 below = StructuredCapturePlanes[captureBase + index];
        if (Color6ToPackedCaptureColor(below) != nativeColor)
            continue;
        return Contract::PackCaptureReference(
            bank,
            StructuredCapturePixelVersion[stateIndex],
            captureAddress);
    }
    return 0u;
}

u32 SoftRenderer::GetCaptureTextureReference(u32 bank, u32 address) const noexcept
{
    if (!UseStructuredVulkan2D() || bank >= 4u || address >= StructuredCapturePixelCount)
        return 0u;

    const std::size_t stateIndex =
        static_cast<std::size_t>(bank) * StructuredCapturePixelCount + address;
    if (StructuredCapturePixelValid[stateIndex] == 0u)
        return 0u;

    return Contract::PackCaptureReference(
        bank, StructuredCapturePixelVersion[stateIndex], address);
}

void SoftRenderer::RecordStructuredCaptureLine(
    u32 line,
    u32 width,
    u32 destinationBank,
    u32 destinationAddress,
    u32 captureCnt,
    const u16* sourceB,
    u32 sourceBBank,
    u32 sourceBAddress)
{
    if (!UseStructuredVulkan2D() || line >= 192u || destinationBank >= 4u)
        return;

    if (StructuredCaptureBankWrittenThisFrame[destinationBank] == 0u)
    {
        StructuredCaptureBankVersion[destinationBank] ^= 1u;
        StructuredCaptureBankWrittenThisFrame[destinationBank] = 1u;
    }

    const std::size_t rowBase = static_cast<std::size_t>(line) * 256u;
    const u32 copyWidth = std::min<u32>(width, 256u);
    StructuredCaptureCommandWrittenThisFrame[line] = 1u;
    StructuredCaptureSourceBWidthThisFrame[line] = static_cast<u16>(copyWidth);
    for (u32 x = 0; x < copyWidth; ++x)
    {
        const u16 packed = sourceB != nullptr ? sourceB[x] : 0u;
        StoreStructuredCaptureSourceWord(
            12u, StructuredCaptureSourceBNative[rowBase + x],
            PackedCaptureColorToColor6(packed));
        u32 reference = 0u;
        if (sourceB != nullptr && sourceBBank < 4u)
        {
            const u32 address = (sourceBAddress + x) & 0xFFFFu;
            const std::size_t stateIndex =
                static_cast<std::size_t>(sourceBBank) * StructuredCapturePixelCount + address;
            if (StructuredCapturePixelValid[stateIndex] != 0u)
            {
                const std::size_t captureBase =
                    static_cast<std::size_t>(sourceBBank) * 3u * StructuredCapturePixelCount;
                const u32 retained = StructuredCapturePlanes[captureBase + address];
                if (Color6ToPackedCaptureColor(retained) == packed)
                {
                    reference = Contract::PackCaptureReference(
                        sourceBBank,
                        StructuredCapturePixelVersion[stateIndex],
                        address);
                }
            }
        }
        StoreStructuredCaptureSourceWord(
            13u, StructuredCaptureSourceBReference[rowBase + x], reference);
    }

    u32* command = StructuredCaptureCommands.data()
        + static_cast<std::size_t>(line) * Contract::kCaptureCommandWords;
    StoreStructuredCaptureCommandWord(command[0], captureCnt);
    StoreStructuredCaptureCommandWord(command[1], Contract::kCaptureCommandValid
        | (destinationBank << Contract::kCaptureCommandDestinationBankShift)
        | ((static_cast<u32>(StructuredCaptureBankVersion[destinationBank]) & 1u)
            << Contract::kCaptureCommandDestinationVersionShift)
        | ((GPU.ScreenSwap ? 0u : 1u) << Contract::kCaptureCommandSourceScreenShift)
        | (StructuredCapture3DValid ? Contract::kCaptureCommandSource3DValid : 0u));
    StoreStructuredCaptureCommandWord(command[2], destinationAddress & 0xFFFFu);
    StoreStructuredCaptureCommandWord(command[3], copyWidth);
}

void SoftRenderer::BuildStructuredScreenLine(
    u32 engine,
    u32 screen,
    u32 line,
    const u32* output,
    bool forcePlain,
    bool preserveVramSnapshot)
{
    if (!UseStructuredVulkan2D() || engine >= 2u || screen >= 2u || line >= 192u || output == nullptr)
        return;

    // A VRAM-display line is captured before DoCapture() mutates the bank.
    // Keep those pixels and their old-generation references intact, but defer
    // frame publication until after this scanline's capture command exists.
    if (preserveVramSnapshot)
    {
        StoreStructuredScreenSource(screen, line, Contract::kScreenSourceFallback);
        ++StructuredFallbackLines;
        if (line == 191u)
        {
            StructuredFrameNativeMenuHeld = NativeMenuHeldForFrame;
            StructuredFrameValid = true;
        }
        return;
    }

    const u32 displayMode = engine == 0u
        ? ((GPU.GPU2D_A.DispCnt >> 16u) & 0x3u)
        : ((GPU.GPU2D_B.DispCnt >> 16u) & 0x1u);
    const std::size_t rowBase = static_cast<std::size_t>(line) * 256u;
    bool copiedStructured = false;
    u32 lineMeta = 0u;
    if (!forcePlain && displayMode == 1u)
    {
        StoreStructuredScreenSource(screen, line, static_cast<u8>(engine));
        ++StructuredRegularLines;
        const u16 brightness = engine == 0u ? GPU.MasterBrightnessA : GPU.MasterBrightnessB;
        lineMeta =
            (Contract::kDisplayModeRegular << Contract::kLineMetaDisplayModeShift)
            | (static_cast<u32>(brightness >> 14u) << Contract::kLineMetaBrightnessModeShift)
            | static_cast<u32>(brightness & Contract::kLineMetaBrightnessFactorMask);
        copiedStructured = true;
    }

    if (!copiedStructured)
    {
        StoreStructuredScreenSource(screen, line, Contract::kScreenSourceFallback);
        ++StructuredFallbackLines;
        u8 changedPlaneMask = 0;
        for (std::size_t x = 0; x < 256u; ++x)
        {
            const std::size_t pixelIndex = rowBase + x;
            StoreStructuredScreenPlaneWord(
                screen, 0u, pixelIndex,
                (output[x] & 0x00FFFFFFu) | 0x01000000u, &changedPlaneMask);
            StoreStructuredScreenPlaneWord(screen, 1u, pixelIndex, 0u, &changedPlaneMask);
            StoreStructuredScreenPlaneWord(
                screen, 2u, pixelIndex,
                Contract::kControlPlain2D << Contract::kControlFlagShift, &changedPlaneMask);
            StoreStructuredScreenPlaneWord(
                screen, Contract::kPlaneCaptureReference, pixelIndex, 0u, &changedPlaneMask);
        }
        // Keep the capture-reference plane in the same scanline-level dirty
        // batch; it is normally zero but can retain a stale capture marker.
        for (u32 plane = 0; plane < Contract::kPlaneCount; ++plane)
        {
            if (changedPlaneMask & static_cast<u8>(1u << plane))
                MarkStructuredPlaneDirty(screen * Contract::kPlaneCount + plane);
        }
        // Every line that reaches this path carries the software renderer's
        // final pixel, which DrawScanlineA/DrawScanlineB already ran
        // ApplyMasterBrightness over. Publishing the real display mode here
        // would make the compositor apply master brightness a second time on
        // VRAM- and FIFO-display lines that fell back to the flattened output.
        // Display-mode 0 is the contract's "already final, do not post-process"
        // marker, which is also how OpenGL Compute ends up applying brightness
        // exactly once after selecting the VRAM/FIFO source.
        lineMeta = Contract::kDisplayModeOff << Contract::kLineMetaDisplayModeShift;
    }

    // The 3D X scroll belongs to this scanline, matching where the software
    // renderer reads it in SoftRenderer3D::GetLine() and where OpenGL Compute
    // stores it as CaptureConfig.uSrcAOffset[line].
    lineMeta |= (static_cast<u32>(GPU.GPU3D.GetRenderXPos()) & Contract::kLineMetaRenderXPosMask)
        << Contract::kLineMetaRenderXPosShift;

    u32& lineMetaDestination =
        StructuredScreenLineMeta[(static_cast<std::size_t>(screen) * 192u) + line];
    if (lineMetaDestination != lineMeta)
    {
        lineMetaDestination = lineMeta;
        MarkStructuredLineMetaDirty(screen);
    }
    if (line == 191u)
    {
        StructuredFrameNativeMenuHeld = NativeMenuHeldForFrame;
        StructuredFrameValid = true;
    }
}

void SoftRenderer::FinalizeStructuredCaptureFrame()
{
    if (!UseStructuredVulkan2D())
        return;

    for (u32 line = 0; line < 192u; ++line)
    {
        const u32 width = StructuredCaptureSourceBWidthThisFrame[line];
        const std::size_t rowBase = static_cast<std::size_t>(line) * 256u;
        for (u32 x = width; x < 256u; ++x)
        {
            StoreStructuredCaptureSourceWord(
                12u, StructuredCaptureSourceBNative[rowBase + x], 0u);
            StoreStructuredCaptureSourceWord(
                13u, StructuredCaptureSourceBReference[rowBase + x], 0u);
        }

        if (StructuredCaptureCommandWrittenThisFrame[line] == 0u)
        {
            u32* command = StructuredCaptureCommands.data()
                + static_cast<std::size_t>(line)
                    * StructuredComposition::kCaptureCommandWords;
            for (u32 word = 0; word < StructuredComposition::kCaptureCommandWords; ++word)
                StoreStructuredCaptureCommandWord(command[word], 0u);
        }
    }
}

bool SoftRenderer::GetStructuredVulkanFrame(StructuredVulkanFrameView& view) const noexcept
{
    view = {};
    if (!UseStructuredVulkan2D() || !StructuredFrameValid)
        return false;
    for (std::size_t screen = 0; screen < 2u; ++screen)
    {
        const std::size_t screenBase = screen * Contract::kPlaneCount * StructuredPixelCount;
        for (std::size_t plane = 0; plane < Contract::kPlaneCount; ++plane)
        {
            view.Plane[screen][plane] = StructuredScreenPlanes.data() + screenBase + (plane * StructuredPixelCount);
            view.ScreenRouting.FallbackPlane[screen][plane] = view.Plane[screen][plane];
            const std::size_t engineABase = plane * StructuredPixelCount;
            const std::size_t engineBBase =
                Contract::kPlaneCount * StructuredPixelCount + engineABase;
            view.ScreenRouting.EnginePlane[0][plane] =
                StructuredEnginePlanes.data() + engineABase;
            view.ScreenRouting.EnginePlane[1][plane] =
                StructuredEnginePlanes.data() + engineBBase;
        }
        view.ScreenRouting.ScreenSource[screen] =
            StructuredScreenSource.data() + screen * Contract::kScreenHeight;
        view.LineMeta[screen] = StructuredScreenLineMeta.data() + (screen * 192u);
    }
    for (std::size_t plane = 0; plane < Contract::kPlaneCount; ++plane)
        view.CaptureSourcePlane[plane] = StructuredEnginePlanes.data() + plane * StructuredPixelCount;
    view.CaptureSourceBNative = StructuredCaptureSourceBNative.data();
    view.CaptureSourceBReference = StructuredCaptureSourceBReference.data();
    view.CaptureCommands = StructuredCaptureCommands.data();
    view.NativeMenuHeld = StructuredFrameNativeMenuHeld;
    view.Valid = true;
    view.ScreenRouteCopyBytes = StructuredScreenRouteCopyBytes;
    view.ScreenRouteCopyNanoseconds = StructuredScreenRouteCopyNanoseconds;
    view.StructuredRegularLines = StructuredRegularLines;
    view.StructuredFallbackLines = StructuredFallbackLines;
    view.Generation = StructuredFrameGeneration;
    view.ContentGeneration = StructuredContentGeneration;
    return true;
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
