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
#include "Platform.h"

#include <cassert>
#include <cstdlib>
#endif

namespace melonDS
{

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
static bool VulkanStructuredPhaseTraceEnabled() noexcept
{
    const char* value = std::getenv("MELONPRIME_VULKAN_2D_TRACE");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}
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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    {
        const std::lock_guard<std::mutex> completedFrameLock(CompletedStructuredVulkanFrameMutex);
        for (auto& completedFrame : CompletedStructuredVulkanFrames)
        {
            if (completedFrame.Completed3DReference.Valid)
                Rend3D->ReleaseCompletedFrameReference(completedFrame.Completed3DReference);
            completedFrame.Completed3DReference = {};
        }
    }
#endif
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
    for (auto& bufferPair : VulkanPackedFramebuffer)
    {
        bufferPair[0].fill(0);
        bufferPair[1].fill(0);
    }
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
    StructuredCaptureScreenSwapValid = false;
    StructuredPhysicalScreenSwap = false;
    StructuredPhysicalScreenSwapStable = true;
    StructuredScreenSwapAtLine0 = false;
    StructuredScreenSwapChangedMidFrame = false;
    StructuredEngineAOnTopLines = 0;
    StructuredEngineAOnBottomLines = 0;
    StructuredCaptureCompositeLineValid = false;
    StructuredCapturePreparedThisFrame = false;
    for (auto& completedFrame : CompletedStructuredVulkanFrames)
    {
        if (completedFrame.Completed3DReference.Valid)
            Rend3D->ReleaseCompletedFrameReference(completedFrame.Completed3DReference);
        completedFrame.Completed3DReference = {};
        completedFrame.Valid = false;
        completedFrame.Renderer3DOwnerIsTop = false;
        completedFrame.PhysicalScreenSwap = false;
        completedFrame.PhysicalScreenSwapStable = true;
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
        {
            if (completedFrame.Completed3DReference.Valid)
                Rend3D->ReleaseCompletedFrameReference(completedFrame.Completed3DReference);
            completedFrame.Completed3DReference = {};
            completedFrame.Valid = false;
        }
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
            if (VulkanStructuredPhaseTraceEnabled())
            {
                Platform::Log(
                    Platform::LogLevel::Info,
                    "Vulkan2DPhase event=Visible2DStart VCount=%u physicalScreenSwap=%u structuredGenerationCandidate=%llu backBuffer=%d rendererSerialAtLine0=%llu rendererOwnerAtLine0=%u",
                    GPU.VCount,
                    GPU.ScreenSwap ? 1u : 0u,
                    static_cast<unsigned long long>(StructuredVulkanGeneration + 1u),
                    BackBuffer,
                    static_cast<unsigned long long>(Rend3D->GetRenderSerial()),
                    GPU.GPU3D.GetRenderScreenSwapAt3D() ? 1u : 0u);
            }
            // A completed snapshot is published only by SwapBuffers after all
            // 192 physical output lines have been produced.  Clear validity at
            // the next frame boundary so VCOUNT overrides cannot publish a
            // mixture of two generations.
            StructuredFrameValid = false;
            StructuredCapture3DSource.fill(0);
            StructuredCapture3DSourceLineValid.fill(0);
            StructuredCapture3DSourceValid = false;
            StructuredCaptureScreenSwapValid = false;
            // Do NOT latch PhysicalScreenSwap from line 0 alone. Phase key is
            // derived at SwapBuffers from where Engine A actually wrote.
            StructuredScreenSwapAtLine0 = GPU.ScreenSwap;
            StructuredScreenSwapChangedMidFrame = false;
            StructuredEngineAOnTopLines = 0;
            StructuredEngineAOnBottomLines = 0;
            StructuredPhysicalScreenSwapStable = true;
            VulkanPackedFramebuffer[static_cast<std::size_t>(BackBuffer & 1)][0].fill(0);
            VulkanPackedFramebuffer[static_cast<std::size_t>(BackBuffer & 1)][1].fill(0);
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
                StructuredCaptureScreenSwapValid = true;
                Rend3D->SetCaptureScreenSwapHint(StructuredCaptureScreenSwap);
                Rend3D->BeginCaptureFrame();
                Rend3D->PrepareCaptureFrame();
                StructuredCapturePreparedThisFrame = true;
            }
        }
        else if (structuredVulkan2D
            && GPU.ScreenSwap != StructuredScreenSwapAtLine0)
        {
            StructuredScreenSwapChangedMidFrame = true;
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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        if (structuredVulkan2D)
        {
            // Sapphire order: Engine A draw + output + capture, then Engine B.
            // Engine B's DrawStructuredCapturePixel must see this scanline's
            // StoreStructuredCaptureLine result, not the previous frame.
            Rend2D_A->DrawScanline(line);
            DrawScanlineA(line, dstA);

            if (GPU.CaptureEnable)
            {
                const u32 captureMode = (GPU.CaptureCnt >> 29) & 0x3u;
                const bool sourceAContributes = captureMode == 0u
                    || (captureMode >= 2u && (GPU.CaptureCnt & 0x1Fu) != 0u);
                const bool captureNeeds3D = captureMode != 1u
                    && sourceAContributes
                    && (((GPU.CaptureCnt & (1u << 24u)) != 0u)
                        || ((GPU.GPU2D_A.DispCnt & 0x0108u) == 0x0108u));
                if (captureNeeds3D)
                {
                    if (!StructuredCapturePreparedThisFrame)
                    {
                        StructuredCaptureScreenSwap = GPU.ScreenSwap;
                        StructuredCaptureScreenSwapValid = true;
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

            Rend2D_B->DrawScanline(line);
            DrawScanlineB(line, dstB);
        }
        else
#endif
        {
            // draw BG/OBJ layers
            Rend2D_A->DrawScanline(line);
            Rend2D_B->DrawScanline(line);

            // draw the final screen output
            DrawScanlineA(line, dstA);
            DrawScanlineB(line, dstB);

            // perform display capture if enabled
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
            if (GPU.CaptureEnable)
                DoCapture(line);
#else
            if (GPU.CaptureEnable)
                DoCapture(line);
#endif
        }
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
        // Physical LCD index must follow the actual framebuffer destination
        // pointers, not a second independent ScreenSwap decode.
        const u32* const topLine = &Framebuffer[BackBuffer][0][dstoffset];
        const u32 screenA = dstA == topLine ? 0u : 1u;
        const u32 screenB = screenA ^ 1u;
#ifndef NDEBUG
        const u32* const bottomLine = &Framebuffer[BackBuffer][1][dstoffset];
        assert(dstA == topLine || dstA == bottomLine);
        assert(dstB == topLine || dstB == bottomLine);
        assert(dstB == (screenB == 0u ? topLine : bottomLine));
        assert(screenA == (GPU.ScreenSwap ? 0u : 1u));
#endif
        BuildStructuredScreenLine(
            0,
            screenA,
            outputLine,
            line < 192u ? line : outputLine,
            dstA,
            line >= 192u);
        BuildStructuredScreenLine(
            1,
            screenB,
            outputLine,
            line < 192u ? line : outputLine,
            dstB,
            line >= 192u);
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
        const u32* const topLine = &Framebuffer[BackBuffer][0][dstoffset];
        const u32 screenA = dstA == topLine ? 0u : 1u;
        const u32 screenB = screenA ^ 1u;
#ifndef NDEBUG
        const u32* const bottomLine = &Framebuffer[BackBuffer][1][dstoffset];
        assert(dstA == topLine || dstA == bottomLine);
        assert(dstB == topLine || dstB == bottomLine);
        assert(dstB == (screenB == 0u ? topLine : bottomLine));
        assert(screenA == (GPU.ScreenSwap ? 0u : 1u));
#endif
        BuildStructuredScreenLine(0, screenA, outputLine, outputLine, dstA, true);
        BuildStructuredScreenLine(1, screenB, outputLine, outputLine, dstB, true);
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
        // Display capture is Engine-A hardware, so the write is issued
        // through Rend2D_A -- but it lands in the shared capture store
        // (keyed by VRAM bank+address), which Engine B can also read.
        static_cast<SoftRenderer2D*>(Rend2D_A.get())->StoreStructuredCaptureLine(
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

void SoftRenderer::SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete)
{
    // The software renderer already writes captures straight to VRAM, so
    // there is nothing to sync back. This is still the correct signal that
    // the CPU/DMA is about to touch a VRAM range the shared structured
    // capture store shadows -- drop that range so later reads fall back to
    // plain VRAM color instead of stale plane/control data.
    (void)complete;
    InvalidateStructuredCaptureRange(bank, start, len);
}

void SoftRenderer::InvalidateStructuredCaptureRange(u32 bank, u32 start, u32 len)
{
    if (!UseStructuredVulkan2D() || bank >= 4u)
        return;

    // (bank, start, len) are 0x8000-byte VRAM capture blocks -- 64 display
    // lines each -- except the hardware's 128x128 capture size (len == 0),
    // which spans 128 lines from the same start. Mirrors GLRenderer's
    // SyncVRAMCapture block-to-line mapping so both renderers agree on which
    // VRAM bytes a given (bank, start, len) covers.
    const u32 lineCount = (len == 0u) ? 128u : (len * 64u);
    const u32 lineStart = start * 64u;
    const u32 lineEnd = std::min<u32>(lineStart + lineCount, 192u);
    if (lineStart >= lineEnd)
        return;

    const std::size_t captureBase = static_cast<std::size_t>(bank) * 3u * StructuredPixelCount;
    for (u32 line = lineStart; line < lineEnd; ++line)
    {
        const std::size_t validIndex = static_cast<std::size_t>(bank) * 192u + line;
        const std::size_t linePixelBase = static_cast<std::size_t>(line) * 256u;
        std::memset(
            StructuredCapturePlanes.data() + captureBase + linePixelBase,
            0,
            256u * sizeof(u32));
        std::memset(
            StructuredCapturePlanes.data() + captureBase + StructuredPixelCount + linePixelBase,
            0,
            256u * sizeof(u32));
        std::memset(
            StructuredCapturePlanes.data() + captureBase + (2u * StructuredPixelCount) + linePixelBase,
            0,
            256u * sizeof(u32));
        StructuredCaptureLineValid[validIndex] = 0;
        StructuredCaptureLineUses3D[validIndex] = 0;
    }
}

void SoftRenderer::BuildStructuredScreenLine(
    u32 engine,
    u32 screen,
    u32 screenLine,
    u32 engineLine,
    const u32* output,
    bool forcePlain)
{
    if (!UseStructuredVulkan2D()
        || engine >= 2u
        || screen >= 2u
        || screenLine >= 192u
        || engineLine >= 192u
        || output == nullptr)
    {
        return;
    }

    const u32 displayMode = engine == 0u
        ? ((GPU.GPU2D_A.DispCnt >> 16u) & 0x3u)
        : ((GPU.GPU2D_B.DispCnt >> 16u) & 0x1u);
    // Engine pixels are stored by VCount (engineLine). Physical screen rows
    // follow the scheduler output line (screenLine). Mixing them copies the
    // wrong Engine row into the published snapshot.
    const std::size_t screenRowBase = static_cast<std::size_t>(screenLine) * 256u;
    const std::size_t engineRowBase = static_cast<std::size_t>(engineLine) * 256u;
    const std::size_t sourceBase = static_cast<std::size_t>(engine) * 3u * StructuredPixelCount;
    const std::size_t destinationBase = static_cast<std::size_t>(screen) * 3u * StructuredPixelCount;

    // Authoritative physical destination: Sapphire packed stride line.
    auto& packedScreen =
        VulkanPackedFramebuffer[static_cast<std::size_t>(BackBuffer & 1)][screen];
    const std::size_t packedRowBase =
        static_cast<std::size_t>(screenLine) * VulkanPackedStride;
    u32* const packedPlane0 = packedScreen.data() + packedRowBase;
    u32* const packedPlane1 = packedScreen.data() + packedRowBase + 256u;
    u32* const packedControl = packedScreen.data() + packedRowBase + 512u;

    auto engineLineHasStructuredPayload = [&]() {
        for (std::size_t x = 0; x < 256u; ++x)
        {
            const std::size_t index = engineRowBase + x;
            if (StructuredEnginePlanes[sourceBase + index] != 0u
                || StructuredEnginePlanes[sourceBase + StructuredPixelCount + index] != 0u
                || StructuredEnginePlanes[sourceBase + (2u * StructuredPixelCount) + index] != 0u)
            {
                return true;
            }
        }
        return false;
    };

    bool copiedStructured = false;
    u32 lineMeta = 0u;
    if (!forcePlain && displayMode == 1u)
    {
        std::memcpy(
            packedPlane0,
            StructuredEnginePlanes.data() + sourceBase + engineRowBase,
            256u * sizeof(u32));
        std::memcpy(
            packedPlane1,
            StructuredEnginePlanes.data() + sourceBase + StructuredPixelCount + engineRowBase,
            256u * sizeof(u32));
        std::memcpy(
            packedControl,
            StructuredEnginePlanes.data()
                + sourceBase
                + (2u * StructuredPixelCount)
                + engineRowBase,
            256u * sizeof(u32));
        const u16 brightness = engine == 0u ? GPU.MasterBrightnessA : GPU.MasterBrightnessB;
        lineMeta =
            (1u << 16u)
            | (static_cast<u32>(brightness >> 14u) << 8u)
            | static_cast<u32>(brightness & 0x1Fu);
        if (StructuredEngineLineUsesCapture3D[(static_cast<std::size_t>(engine) * 192u) + engineLine] != 0u)
            lineMeta |= 1u << 21u;
        copiedStructured = true;
    }
    else if (!forcePlain && engine == 0u && displayMode == 2u)
    {
        // VRAM Display mode is Engine-A-only hardware; the captured
        // metadata it reads back lives in the shared capture store, keyed
        // by VRAM bank+address, not by engine.
        const u32 bank = (GPU.GPU2D_A.DispCnt >> 18u) & 0x3u;
        const std::size_t validIndex = static_cast<std::size_t>(bank) * 192u + engineLine;
        if ((GPU.VRAMMap_LCDC & (1u << bank)) != 0u && StructuredCaptureLineValid[validIndex] != 0u)
        {
            const std::size_t captureBase = static_cast<std::size_t>(bank) * 3u * StructuredPixelCount;
            std::memcpy(
                packedPlane0,
                StructuredCapturePlanes.data() + captureBase + engineRowBase,
                256u * sizeof(u32));
            std::memcpy(
                packedPlane1,
                StructuredCapturePlanes.data() + captureBase + StructuredPixelCount + engineRowBase,
                256u * sizeof(u32));
            std::memcpy(
                packedControl,
                StructuredCapturePlanes.data()
                    + captureBase
                    + (2u * StructuredPixelCount)
                    + engineRowBase,
                256u * sizeof(u32));
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

    if (!copiedStructured && !forcePlain && engineLineHasStructuredPayload())
    {
        // Prefer Engine provenance over flattened BGRA. Filling forcePlain-style
        // 2D-only content here marks the whole LCD "explicit" and blocks Engine A
        // phase-cache restore on alternating ScreenSwap scenes.
        std::memcpy(
            packedPlane0,
            StructuredEnginePlanes.data() + sourceBase + engineRowBase,
            256u * sizeof(u32));
        std::memcpy(
            packedPlane1,
            StructuredEnginePlanes.data() + sourceBase + StructuredPixelCount + engineRowBase,
            256u * sizeof(u32));
        std::memcpy(
            packedControl,
            StructuredEnginePlanes.data()
                + sourceBase
                + (2u * StructuredPixelCount)
                + engineRowBase,
            256u * sizeof(u32));
        lineMeta = (displayMode << 16u);
        if (StructuredEngineLineUsesCapture3D[(static_cast<std::size_t>(engine) * 192u) + engineLine] != 0u)
            lineMeta |= 1u << 21u;
        copiedStructured = true;
    }

    if (!copiedStructured)
    {
        for (std::size_t x = 0; x < 256u; ++x)
        {
            packedPlane0[x] = (output[x] & 0x00FFFFFFu) | 0x01000000u;
            packedPlane1[x] = 0;
            const bool protectedBlack = StructuredVulkan2DSourceClass(output[x]) != 0u
                && StructuredVulkan2DIsOpaqueBlack(output[x]);
            packedControl[x] = protectedBlack ? 0xA7000000u : 0x87000000u;
        }
        lineMeta = (forcePlain ? 0u : displayMode) << 16u;
    }
    packedScreen[packedRowBase + 768u] = lineMeta;

    // Plane-major ScreenPlanes / LineMeta are derived views of the packed
    // physical line — never the other way around.
    std::memcpy(
        StructuredScreenPlanes.data() + destinationBase + screenRowBase,
        packedPlane0,
        256u * sizeof(u32));
    std::memcpy(
        StructuredScreenPlanes.data() + destinationBase + StructuredPixelCount + screenRowBase,
        packedPlane1,
        256u * sizeof(u32));
    std::memcpy(
        StructuredScreenPlanes.data()
            + destinationBase
            + (2u * StructuredPixelCount)
            + screenRowBase,
        packedControl,
        256u * sizeof(u32));
    StructuredScreenLineMeta[(static_cast<std::size_t>(screen) * 192u) + screenLine] = lineMeta;

    // Vote Engine A physical target from the destination pointer used this line.
    if (engine == 0u)
    {
        if (screen == 0u)
            ++StructuredEngineAOnTopLines;
        else
            ++StructuredEngineAOnBottomLines;
    }

    if (screenLine == 191u)
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
    if (snapshot.Completed3DReference.Valid
        && !Rend3D->RetainCompletedFrameReference(snapshot.Completed3DReference))
    {
        snapshot.Completed3DReference = {};
        snapshot.Renderer3DRenderSerial = 0u;
    }
    return true;
}

void SoftRenderer::SwapBuffers()
{
    if (UseStructuredVulkan2D() && StructuredFrameValid)
    {
        const std::lock_guard<std::mutex> completedFrameLock(CompletedStructuredVulkanFrameMutex);
        StructuredVulkanFrameSnapshot& completedFrame =
            CompletedStructuredVulkanFrames[static_cast<std::size_t>(BackBuffer & 1)];
        if (completedFrame.Completed3DReference.Valid)
            Rend3D->ReleaseCompletedFrameReference(completedFrame.Completed3DReference);
        completedFrame.Completed3DReference = {};
        // Packed physical buffers are the authoritative producer output.
        completedFrame.PackedTop =
            VulkanPackedFramebuffer[static_cast<std::size_t>(BackBuffer & 1)][0];
        completedFrame.PackedBottom =
            VulkanPackedFramebuffer[static_cast<std::size_t>(BackBuffer & 1)][1];
        // Derive plane-major ScreenPlanes / LineMeta from packed so consumers
        // always see the same generation as PackedTop/Bottom.
        for (std::size_t y = 0; y < 192u; ++y)
        {
            const std::size_t packedRow = y * VulkanPackedStride;
            const std::size_t planeRow = y * 256u;
            std::memcpy(
                completedFrame.ScreenPlanes.data() + planeRow,
                completedFrame.PackedTop.data() + packedRow,
                256u * sizeof(u32));
            std::memcpy(
                completedFrame.ScreenPlanes.data() + StructuredPixelCount + planeRow,
                completedFrame.PackedTop.data() + packedRow + 256u,
                256u * sizeof(u32));
            std::memcpy(
                completedFrame.ScreenPlanes.data() + (2u * StructuredPixelCount) + planeRow,
                completedFrame.PackedTop.data() + packedRow + 512u,
                256u * sizeof(u32));
            completedFrame.ScreenLineMeta[y] =
                completedFrame.PackedTop[packedRow + 768u];

            constexpr std::size_t bottomPlaneBase = 3u * StructuredPixelCount;
            std::memcpy(
                completedFrame.ScreenPlanes.data() + bottomPlaneBase + planeRow,
                completedFrame.PackedBottom.data() + packedRow,
                256u * sizeof(u32));
            std::memcpy(
                completedFrame.ScreenPlanes.data()
                    + bottomPlaneBase
                    + StructuredPixelCount
                    + planeRow,
                completedFrame.PackedBottom.data() + packedRow + 256u,
                256u * sizeof(u32));
            std::memcpy(
                completedFrame.ScreenPlanes.data()
                    + bottomPlaneBase
                    + (2u * StructuredPixelCount)
                    + planeRow,
                completedFrame.PackedBottom.data() + packedRow + 512u,
                256u * sizeof(u32));
            completedFrame.ScreenLineMeta[192u + y] =
                completedFrame.PackedBottom[packedRow + 768u];
        }
        completedFrame.EnginePlanes = StructuredEnginePlanes;
        completedFrame.EngineLineUsesCapture3D = StructuredEngineLineUsesCapture3D;
        completedFrame.Capture3DSource = StructuredCapture3DSource;
        completedFrame.Capture3DSourceLineValid = StructuredCapture3DSourceLineValid;
        constexpr u32 captureUseMetaMask = (1u << 21u) | (1u << 22u);
        for (std::size_t line = 0; line < 192u; ++line)
        {
            completedFrame.TopScreenNeedsCapture3D[line] =
                (completedFrame.ScreenLineMeta[line] & captureUseMetaMask) != 0u ? 1u : 0u;
            completedFrame.BottomScreenNeedsCapture3D[line] =
                (completedFrame.ScreenLineMeta[192u + line] & captureUseMetaMask) != 0u ? 1u : 0u;
        }
        completedFrame.HasCapture3DSource = StructuredCapture3DSourceValid;
        completedFrame.CaptureScreenSwap = StructuredCaptureScreenSwap;
        completedFrame.CaptureScreenSwapValid = StructuredCaptureScreenSwapValid;
        // Phase key matches this generation's actual Engine A physical targets.
        if (StructuredEngineAOnTopLines != StructuredEngineAOnBottomLines)
        {
            StructuredPhysicalScreenSwap =
                StructuredEngineAOnTopLines > StructuredEngineAOnBottomLines;
        }
        else
        {
            StructuredPhysicalScreenSwap = StructuredScreenSwapAtLine0;
        }
        StructuredPhysicalScreenSwapStable = !StructuredScreenSwapChangedMidFrame;
        completedFrame.PhysicalScreenSwap = StructuredPhysicalScreenSwap;
        completedFrame.PhysicalScreenSwapStable = StructuredPhysicalScreenSwapStable;
        Renderer3DCompletedFrameReference completed3DReference{};
        if (Rend3D->AcquireCompletedFrameForStructured(completed3DReference))
            completedFrame.Completed3DReference = completed3DReference;
        completedFrame.Renderer3DOwnerIsTop = completedFrame.Completed3DReference.Valid
            ? completedFrame.Completed3DReference.OwnerIsTop()
            : false;
        completedFrame.CaptureBackedClass4Only =
            StructuredCaptureBacked3DLines > 0u
            && StructuredCaptureBackedBestClassLines[4] == StructuredCaptureBacked3DLines
            && StructuredCaptureBackedBestClassLines[0] == 0u
            && StructuredCaptureBackedBestClassLines[1] == 0u
            && StructuredCaptureBackedBestClassLines[2] == 0u
            && StructuredCaptureBackedBestClassLines[8] == 0u
            && StructuredCaptureBackedBestClassLines[16] == 0u;
        u32 captureBackedDominantStructured2DLines = StructuredCaptureBackedBestClassLines[1];
        if (StructuredCaptureBackedBestClassLines[2] > captureBackedDominantStructured2DLines)
            captureBackedDominantStructured2DLines = StructuredCaptureBackedBestClassLines[2];
        if (StructuredCaptureBackedBestClassLines[4] > captureBackedDominantStructured2DLines)
            captureBackedDominantStructured2DLines = StructuredCaptureBackedBestClassLines[4];
        if (StructuredCaptureBackedBestClassLines[8] > captureBackedDominantStructured2DLines)
            captureBackedDominantStructured2DLines = StructuredCaptureBackedBestClassLines[8];
        if (StructuredCaptureBackedBestClassLines[16] > captureBackedDominantStructured2DLines)
            captureBackedDominantStructured2DLines = StructuredCaptureBackedBestClassLines[16];
        completedFrame.CaptureBackedHasStructured2DSource =
            StructuredCaptureBacked3DLines > 0u
            && captureBackedDominantStructured2DLines > (StructuredCaptureBacked3DLines / 2u)
            && captureBackedDominantStructured2DLines > StructuredCaptureBackedBestClassLines[0];
        completedFrame.FrontBuffer = BackBuffer & 1;
        completedFrame.Generation = ++StructuredVulkanGeneration;
        completedFrame.Renderer3DRenderSerial = completedFrame.Completed3DReference.Valid
            ? completedFrame.Completed3DReference.Serial
            : 0u;
        completedFrame.Valid = true;
        if (VulkanStructuredPhaseTraceEnabled())
        {
            Platform::Log(
                Platform::LogLevel::Info,
                "Vulkan2DPhase event=StructuredPublish structuredGeneration=%llu publishedReferenced3dSerial=%llu publishedReferenced3dOwner=%u publishedReferencedImageSlot=%u publishedTimelineValue=%llu currentRendererSerial=%llu currentRendererOwner=%u currentColorImageSlot=mutable exactReference=%u",
                static_cast<unsigned long long>(completedFrame.Generation),
                static_cast<unsigned long long>(completedFrame.Renderer3DRenderSerial),
                completedFrame.Renderer3DOwnerIsTop ? 1u : 0u,
                completedFrame.Completed3DReference.ImageSlot,
                static_cast<unsigned long long>(completedFrame.Completed3DReference.CompletionValue),
                static_cast<unsigned long long>(Rend3D->GetRenderSerial()),
                GPU.GPU3D.GetRenderScreenSwapAt3D() ? 1u : 0u,
                completedFrame.Completed3DReference.Valid ? 1u : 0u);
        }
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
