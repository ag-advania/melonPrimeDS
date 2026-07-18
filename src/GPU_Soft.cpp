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
    for (auto& bufferPair : VulkanPackedFramebuffer)
    {
        bufferPair[0].fill(0);
        bufferPair[1].fill(0);
    }
    StructuredCapturePlanes.fill(0);
    StructuredCaptureLineValid.fill(0);
    StructuredFrameCaptureLineUses3D.fill(0);
    StructuredEngineLineUsesCapture3D.fill(0);
    StructuredCaptureBackedBestClassLines.fill(0);
    StructuredCaptureBacked3DLines = 0;
    StructuredCopyLines = 0;
    StructuredCaptureMode = 0;
    StructuredCapture3DSource.fill(0);
    StructuredCapture3DSourceLineValid.fill(0);
    // Sapphire's accelerated BG0 path publishes the 3D layer slot itself as
    // 0x40000000. 0x20000000 is the packed backdrop placeholder and must not
    // be ORed into the slot marker (that would produce a false 0x20
    // protected-black flag in the alpha byte).
    std::fill_n(Structured3DPlaceholderLine, 256, 0x40000000u);
    std::fill_n(StructuredCaptureCompositeLine, 256, 0u);
    StructuredFrameValid = false;
    StructuredCapture3DSourceValid = false;
    StructuredCaptureScreenSwap = false;
    StructuredCaptureScreenSwapValid = false;
    StructuredCaptureCompositeLineValid = false;
    StructuredCapturePreparedThisFrame = false;
    for (auto& completedFrame : CompletedStructuredVulkanFrames)
    {
        if (completedFrame.Completed3DReference.Valid)
            Rend3D->ReleaseCompletedFrameReference(completedFrame.Completed3DReference);
        completedFrame.Completed3DReference = {};
        completedFrame.Valid = false;
        completedFrame.Renderer3DOwnerIsTop = false;
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
            StructuredFrameCaptureLineUses3D.fill(0);
            if (StructuredVulkanResyncRequested.exchange(false, std::memory_order_acq_rel))
            {
                // Sapphire ClearStructuredVulkan2DState clears both visible
                // structured planes and capture history in one operation.
                StructuredEnginePlanes.fill(0);
                StructuredScreenPlanes.fill(0);
                StructuredCapturePlanes.fill(0);
                StructuredCaptureLineValid.fill(0);
                StructuredEngineLineUsesCapture3D.fill(0);
                StructuredCapture3DSource.fill(0);
                StructuredCapture3DSourceLineValid.fill(0);
                std::fill_n(StructuredCaptureCompositeLine, 256u, 0u);
                StructuredCapture3DSourceValid = false;
                StructuredCaptureCompositeLineValid = false;
            }
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
            VulkanPackedFramebuffer[static_cast<std::size_t>(BackBuffer & 1)][0].fill(0);
            VulkanPackedFramebuffer[static_cast<std::size_t>(BackBuffer & 1)][1].fill(0);
            StructuredCaptureCompositeLineValid = false;
            StructuredCapturePreparedThisFrame = false;
            StructuredCaptureBackedBestClassLines.fill(0);
            StructuredCaptureBacked3DLines = 0;
            StructuredCopyLines = 0;
            StructuredCaptureMode = (GPU.CaptureCnt >> 29u) & 0x3u;

            const u32 captureMode = (GPU.CaptureCnt >> 29u) & 0x3u;
            const bool sourceAContributes = captureMode == 0u
                || (captureMode >= 2u && (GPU.CaptureCnt & 0x1Fu) != 0u);
            const bool direct3D = (GPU.CaptureCnt & (1u << 24u)) != 0u;
            const bool bg0Uses3D = (GPU.GPU2D_A.DispCnt & 0x0108u) == 0x0108u;
            const bool captureNeeds3D = GPU.CaptureEnable
                && captureMode != 1u
                && (direct3D || (bg0Uses3D && sourceAContributes));
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
                const bool direct3D = (GPU.CaptureCnt & (1u << 24u)) != 0u;
                bool needs3DComposite = false;
                if (!direct3D && captureMode != 1u && sourceAContributes)
                {
                    const u32* const rawPacked =
                        static_cast<SoftRenderer2D*>(Rend2D_A.get())->GetStructuredPackedLine();
                    for (u32 x = 0; x < 256u; ++x)
                    {
                        if (((rawPacked[512u + x] >> 24u) & 0xFu) <= 4u)
                        {
                            needs3DComposite = true;
                            break;
                        }
                    }
                }
                const bool captureNeeds3D = direct3D || needs3DComposite;
                if (captureNeeds3D)
                {
                    // Sapphire refreshes the physical-screen hint immediately
                    // before every accelerated GetLine() used by display
                    // capture.  POWCNT1 can change after the frame-level
                    // preparation, so a line must not inherit a stale owner.
                    StructuredCaptureScreenSwap = GPU.ScreenSwap;
                    StructuredCaptureScreenSwapValid = true;
                    Rend3D->SetCaptureScreenSwapHint(StructuredCaptureScreenSwap);
                    if (!StructuredCapturePreparedThisFrame)
                    {
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
            static_cast<SoftRenderer2D*>(Rend2D_A.get())->GetStructuredPackedLine(),
            dstA,
            line >= 192u);
        BuildStructuredScreenLine(
            1,
            screenB,
            outputLine,
            line < 192u ? line : outputLine,
            static_cast<SoftRenderer2D*>(Rend2D_B.get())->GetStructuredPackedLine(),
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
        BuildStructuredScreenLine(
            0,
            screenA,
            outputLine,
            outputLine,
            static_cast<SoftRenderer2D*>(Rend2D_A.get())->GetStructuredPackedLine(),
            dstA,
            true);
        BuildStructuredScreenLine(
            1,
            screenB,
            outputLine,
            outputLine,
            static_cast<SoftRenderer2D*>(Rend2D_B.get())->GetStructuredPackedLine(),
            dstB,
            true);
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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // Sapphire returns the accelerated packed line before applying master
    // brightness on the CPU. The compositor consumes the register value from
    // the per-line metadata and applies it exactly once.
    if (!UseStructuredVulkan2D())
#endif
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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    if (!UseStructuredVulkan2D())
#endif
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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    bool structuredCaptureLineUses3D = false;
#endif
    if (captureCnt & (1<<24))
    {
        srcA = Output3D;
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        structuredCaptureLineUses3D = UseStructuredVulkan2D() && srcA != nullptr;
#endif
    }
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    else if (UseStructuredVulkan2D() && StructuredCaptureCompositeLineValid)
    {
        srcA = StructuredCaptureCompositeLine;
        structuredCaptureLineUses3D = true;
    }
#endif
    else
        srcA = Output2D[0];

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // Sapphire publishes CaptureLineUses3d from DoCapture after selecting the
    // real source-A line.  This is a current-frame 192-line mask, not the
    // persistent per-VRAM-bank structured-capture validity store.
    if (UseStructuredVulkan2D() && line < StructuredFrameCaptureLineUses3D.size())
    {
        StructuredFrameCaptureLineUses3D[static_cast<std::size_t>(line)] =
            structuredCaptureLineUses3D ? 1u : 0u;
    }
#endif

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
            srcB != nullptr,
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

bool StructuredVulkan2DHas3DSlot(u32 value)
{
    return ((value >> 24u) & 0xC0u) == 0x40u;
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

u32 SoftRenderer::ClassifyStructuredCaptureBackedLine(
    u32 engine,
    u32 line,
    const u32* structuredPixels)
{
    if (!UseStructuredVulkan2D()
        || engine != 1u
        || line >= 192u
        || structuredPixels == nullptr
        || ((GPU.GPU2D_B.DispCnt >> 16u) & 0x1u) != 1u
        || StructuredFrameCaptureLineUses3D[static_cast<std::size_t>(line)] == 0u)
    {
        return 0u;
    }

    ++StructuredCaptureBacked3DLines;
    u32 sourceCounts[17]{};
    bool lineHasExplicit3DSlot = false;
    for (std::size_t x = 0; x < 256u; ++x)
    {
        lineHasExplicit3DSlot =
            lineHasExplicit3DSlot
            || StructuredVulkan2DHas3DSlot(structuredPixels[x])
            || StructuredVulkan2DHas3DSlot(structuredPixels[256u + x])
            || StructuredVulkan2DHas3DSlot(structuredPixels[512u + x]);
        const u32 sourceClass = StructuredVulkan2DSourceClass(structuredPixels[x]);
        if (sourceClass <= 16u)
            ++sourceCounts[sourceClass];
    }

    u32 captureBacked3DSourceClass = 0u;
    if (!lineHasExplicit3DSlot)
    {
        constexpr std::array<u32, 4> sourceClasses{1u, 2u, 4u, 8u};
        u32 bestSourceCount = 0u;
        for (u32 sourceClass : sourceClasses)
        {
            if (sourceCounts[sourceClass] > bestSourceCount)
            {
                bestSourceCount = sourceCounts[sourceClass];
                captureBacked3DSourceClass = sourceClass;
            }
        }
        if (bestSourceCount < 128u)
            captureBacked3DSourceClass = 0u;
    }

    ++StructuredCaptureBackedBestClassLines[captureBacked3DSourceClass];
    return captureBacked3DSourceClass;
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
    u32 legacyControl,
    u32 captureBacked3DSourceClass)
{
    if (!UseStructuredVulkan2D() || engine >= 2u || line >= 192u || x >= 256u)
        return;

    const u32 flags0 = originalVal1 >> 24u;
    const u32 flags1 = originalVal2 >> 24u;
    const u32 flags2 = originalVal3 >> 24u;
    const bool slotInPlane0 = (flags0 & 0xC0u) == 0x40u;
    const bool slotInPlane1 = (flags1 & 0xC0u) == 0x40u;
    const bool slotInPlane2 = (flags2 & 0xC0u) == 0x40u;
    const bool has3DSlot = slotInPlane0 || slotInPlane1 || slotInPlane2;
    const u32 legacyAlpha = (legacyControl >> 24u) & 0x0Fu;
    const bool legacyCaptureBackedComp4 =
        legacyAlpha == 4u
        && legacyVal1 == 0x20000000u
        && legacyVal2 == 0x20000000u;
    const std::size_t pixelIndex = static_cast<std::size_t>(line) * 256u + x;
    const std::size_t engineBase =
        static_cast<std::size_t>(engine) * 3u * StructuredPixelCount;

    if (!has3DSlot
        && captureBacked3DSourceClass == 0u
        && !legacyCaptureBackedComp4
        && !StructuredVulkan2DIsOpaqueBlack(legacyVal1))
    {
        StructuredEnginePlanes[engineBase + pixelIndex] = legacyVal1;
        StructuredEnginePlanes[engineBase + StructuredPixelCount + pixelIndex] = 0u;
        StructuredEnginePlanes[engineBase + (2u * StructuredPixelCount) + pixelIndex] =
            (legacyControl & 0x00FFFFFFu) | ((legacyAlpha | 0x80u) << 24u);
        return;
    }

    const u32 sourceClass0 = StructuredVulkan2DSourceClass(originalVal1);
    const u32 sourceClass1 = StructuredVulkan2DSourceClass(originalVal2);
    const u32 sourceClass2 = StructuredVulkan2DSourceClass(originalVal3);
    const bool captureBackedSlotInPlane0 =
        captureBacked3DSourceClass != 0u && sourceClass0 == captureBacked3DSourceClass;
    const bool captureBackedSlotInPlane1 =
        captureBacked3DSourceClass != 0u && sourceClass1 == captureBacked3DSourceClass;
    const bool captureBackedSlotInPlane2 =
        captureBacked3DSourceClass != 0u && sourceClass2 == captureBacked3DSourceClass;
    const bool hasCaptureBacked3DSlot =
        !has3DSlot
        && (captureBackedSlotInPlane0 || captureBackedSlotInPlane1 || captureBackedSlotInPlane2);

    u32 plane0 = legacyVal1;
    u32 plane1 = 0u;
    u32 control = legacyControl;
    bool protectedBlack2D = false;
    if (has3DSlot || hasCaptureBacked3DSlot || legacyCaptureBackedComp4)
    {
        bool hasAbovePlane = false;
        if (legacyCaptureBackedComp4)
        {
            plane0 = 0u;
        }
        else if (slotInPlane0 || captureBackedSlotInPlane0)
        {
            plane0 = legacyVal2;
        }
        else if (slotInPlane1 || captureBackedSlotInPlane1)
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

        const u32 structuredAlpha =
            legacyAlpha | 0x40u | (hasAbovePlane ? 0x80u : 0u);
        control = (legacyControl & 0x00FFFFFFu)
            | ((structuredAlpha | (protectedBlack2D ? 0x20u : 0u)) << 24u);
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

void SoftRenderer::StoreStructuredCapturePixel(
    u32 vramBank,
    u32 vramAddress,
    u32 originalVal1,
    u32 originalVal2,
    u32 originalVal3,
    u32 legacyVal1,
    u32 legacyVal2,
    u32 legacyControl,
    u32 external3DSourceClass,
    bool external3DSlot,
    bool external3DCoverage,
    bool allowUnclassifiedExternal3DSlot)
{
    if (!UseStructuredVulkan2D() || vramBank >= 4u || vramAddress >= StructuredPixelCount)
        return;

    constexpr u32 kSlot3DFlag = 0x40u;
    constexpr u32 kAbove3DFlag = 0x80u;
    constexpr u32 kOnly2DFlag = 0x80u;
    constexpr u32 kProtectedBlackFlag = 0x20u;
    constexpr u32 kNo3DCoverageFlag = 0x10u;
    constexpr u32 k3DPlaceholder = 0x20000000u;
    const std::size_t captureBase =
        static_cast<std::size_t>(vramBank) * 3u * StructuredPixelCount;
    const u32 line = vramAddress / 256u;
    const std::size_t captureIndex = static_cast<std::size_t>(vramAddress);
    const std::size_t lineValidIndex = static_cast<std::size_t>(vramBank) * 192u + line;

    const u32 sourceClass0 = StructuredVulkan2DSourceClass(originalVal1);
    const u32 sourceClass1 = StructuredVulkan2DSourceClass(originalVal2);
    const u32 sourceClass2 = StructuredVulkan2DSourceClass(originalVal3);
    const bool slotInPlane0 = StructuredVulkan2DHas3DSlot(originalVal1);
    const bool slotInPlane1 = StructuredVulkan2DHas3DSlot(originalVal2);
    const bool slotInPlane2 = StructuredVulkan2DHas3DSlot(originalVal3);
    const bool has3DSlot = slotInPlane0 || slotInPlane1 || slotInPlane2;
    const bool hasExternal3DSlot =
        !has3DSlot
        && external3DSlot
        && (external3DSourceClass != 0u || allowUnclassifiedExternal3DSlot);

    u32 captureBacked3DSourceClass = 0u;
    if (!has3DSlot && !hasExternal3DSlot)
    {
        if (sourceClass0 != 0x10u && sourceClass0 != 0u)
            captureBacked3DSourceClass = sourceClass0;
        else if (sourceClass1 != 0x10u && sourceClass1 != 0u)
            captureBacked3DSourceClass = sourceClass1;
        else if (sourceClass2 != 0x10u && sourceClass2 != 0u)
            captureBacked3DSourceClass = sourceClass2;
    }

    const bool captureBackedSlotInPlane0 =
        captureBacked3DSourceClass != 0u && sourceClass0 == captureBacked3DSourceClass;
    const bool captureBackedSlotInPlane1 =
        captureBacked3DSourceClass != 0u && sourceClass1 == captureBacked3DSourceClass;
    const bool captureBackedSlotInPlane2 =
        captureBacked3DSourceClass != 0u && sourceClass2 == captureBacked3DSourceClass;
    const bool hasCaptureBacked3DSlot =
        !has3DSlot
        && !hasExternal3DSlot
        && (captureBackedSlotInPlane0
            || captureBackedSlotInPlane1
            || captureBackedSlotInPlane2);

    u32 belowPlane = legacyVal1;
    u32 abovePlane = 0u;
    u32 control = legacyControl;
    const u32 existingAbovePlane =
        StructuredCapturePlanes[captureBase + StructuredPixelCount + captureIndex];
    const u32 existingControl =
        StructuredCapturePlanes[captureBase + (2u * StructuredPixelCount) + captureIndex];
    const u32 existingControlAlpha = existingControl >> 24u;
    const bool existingHasStructuredAbove =
        (existingControlAlpha & kSlot3DFlag) != 0u
        && (existingControlAlpha & kAbove3DFlag) != 0u
        && existingAbovePlane != 0u;
    const u32 legacyAlpha = (legacyControl >> 24u) & 0x0Fu;
    const bool legacyCaptureBackedComp4 =
        legacyAlpha == 4u
        && legacyVal1 == k3DPlaceholder
        && legacyVal2 == k3DPlaceholder;
    bool protectedBlack2D = false;
    if (has3DSlot || hasExternal3DSlot || hasCaptureBacked3DSlot || legacyCaptureBackedComp4)
    {
        bool hasAbovePlane = false;
        if (legacyCaptureBackedComp4)
        {
            belowPlane = 0u;
        }
        else if (hasExternal3DSlot)
        {
            belowPlane = legacyVal2;
            if (legacyAlpha == 1u && StructuredVulkan2DSourceIsReal2D(sourceClass0))
            {
                abovePlane = originalVal1;
                hasAbovePlane = true;
                protectedBlack2D = StructuredVulkan2DIsOpaqueBlack(abovePlane);
            }
            else if (legacyAlpha == 7u
                && existingHasStructuredAbove
                && existingAbovePlane == legacyVal1)
            {
                abovePlane = existingAbovePlane;
                hasAbovePlane = true;
                protectedBlack2D = (existingControlAlpha & kProtectedBlackFlag) != 0u;
            }
            else if (legacyAlpha == 7u
                && external3DSourceClass != 0u
                && sourceClass0 != external3DSourceClass
                && StructuredVulkan2DSourceIsReal2D(sourceClass0))
            {
                abovePlane = originalVal1;
                hasAbovePlane = true;
                protectedBlack2D = StructuredVulkan2DIsOpaqueBlack(abovePlane);
            }
        }
        else if (external3DSlot && slotInPlane0)
        {
            belowPlane = legacyVal2;
            if (StructuredVulkan2DSourceIsReal2D(sourceClass1))
            {
                abovePlane = legacyVal2;
                hasAbovePlane = true;
                protectedBlack2D = StructuredVulkan2DIsOpaqueBlack(abovePlane);
            }
        }
        else if (slotInPlane0 || captureBackedSlotInPlane0)
        {
            belowPlane = legacyVal2;
        }
        else if (slotInPlane1 || captureBackedSlotInPlane1)
        {
            belowPlane = legacyVal2;
            if (StructuredVulkan2DSourceIsReal2D(sourceClass0))
            {
                abovePlane = originalVal1;
                hasAbovePlane = true;
                protectedBlack2D = StructuredVulkan2DIsOpaqueBlack(abovePlane);
            }
        }
        else
        {
            belowPlane = legacyVal1;
            if (StructuredVulkan2DSourceIsReal2D(sourceClass0)
                || StructuredVulkan2DSourceIsReal2D(sourceClass1))
            {
                abovePlane = legacyVal1;
                hasAbovePlane = true;
                protectedBlack2D = StructuredVulkan2DIsOpaqueBlack(abovePlane);
            }
        }

        const u32 structuredAlpha = legacyAlpha
            | kSlot3DFlag
            | (hasAbovePlane ? kAbove3DFlag : 0u)
            | (external3DSlot && !external3DCoverage ? kNo3DCoverageFlag : 0u);
        control = (legacyControl & 0x00FFFFFFu)
            | ((structuredAlpha | (protectedBlack2D ? kProtectedBlackFlag : 0u)) << 24u);
    }
    else
    {
        protectedBlack2D =
            (StructuredVulkan2DSourceIsReal2D(sourceClass0)
                || StructuredVulkan2DSourceIsReal2D(sourceClass1)
                || StructuredVulkan2DSourceIsReal2D(sourceClass2))
            && StructuredVulkan2DIsOpaqueBlack(legacyVal1);
        control = (legacyControl & 0x00FFFFFFu)
            | ((legacyAlpha
                | kOnly2DFlag
                | (protectedBlack2D ? kProtectedBlackFlag : 0u)) << 24u);
    }

    if (StructuredCaptureLineValid[lineValidIndex] == 0u)
        ++StructuredCopyLines;
    StructuredCapturePlanes[captureBase + captureIndex] = belowPlane;
    StructuredCapturePlanes[captureBase + StructuredPixelCount + captureIndex] = abovePlane;
    StructuredCapturePlanes[captureBase + (2u * StructuredPixelCount) + captureIndex] = control;
    StructuredCaptureLineValid[lineValidIndex] = 1u;
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

    const u32* const rawPacked =
        static_cast<SoftRenderer2D*>(Rend2D_A.get())->GetStructuredPackedLine();
    if (rawPacked == nullptr)
        return;
    for (std::size_t x = 0; x < 256u; ++x)
    {
        u32 val1 = rawPacked[x];
        const u32 val2 = rawPacked[256u + x];
        const u32 val3 = rawPacked[512u + x];
        const u32 exact3D = exact3DLine[x];
        const u32 compositionMode = (val3 >> 24u) & 0xFu;
        if (compositionMode == 4u)
        {
            val1 = (exact3D >> 24u) != 0u
                ? ColorBlend5(exact3D, val1)
                : val2;
        }
        else if (compositionMode == 1u)
        {
            val1 = (exact3D >> 24u) != 0u
                ? ColorBlend4(
                    val1,
                    exact3D,
                    (val3 >> 8u) & 0x1Fu,
                    (val3 >> 16u) & 0x1Fu)
                : val2;
        }
        else if (compositionMode <= 3u)
        {
            if ((exact3D >> 24u) != 0u)
            {
                val1 = exact3D;
                const u32 evy = (val3 >> 8u) & 0x1Fu;
                if (compositionMode == 2u)
                    val1 = ColorBrightnessUp(val1, evy, 0x8u);
                else if (compositionMode == 3u)
                    val1 = ColorBrightnessDown(val1, evy, 0x7u);
            }
            else
            {
                val1 = val2;
            }
        }
        StructuredCaptureCompositeLine[x] = val1;
    }
    StructuredCaptureCompositeLineValid = true;
}

void SoftRenderer::BuildStructuredScreenLine(
    u32 engine,
    u32 screen,
    u32 screenLine,
    u32 engineLine,
    const u32* rawPacked,
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

    // Sapphire's accelerated framebuffer stores the unmerged three-plane
    // BGOBJ line.  Keep that raw producer result intact here; SwapBuffers()
    // performs the frontend's mergeStructuredDisplayLine/copyStructuredLine
    // step once the frame-wide capture classification is known.
    if (!forcePlain && displayMode == 1u && rawPacked != nullptr)
    {
        std::memcpy(packedPlane0, rawPacked, 256u * sizeof(u32));
        std::memcpy(packedPlane1, rawPacked + 256u, 256u * sizeof(u32));
        std::memcpy(packedControl, rawPacked + 512u, 256u * sizeof(u32));
    }
    else
    {
        for (std::size_t x = 0; x < 256u; ++x)
        {
            packedPlane0[x] = forcePlain
                ? output[x]
                : (output[x] & 0x00FFFFFFu);
            packedPlane1[x] = 0u;
            packedControl[x] = 0u;
        }
    }

    u32 lineMeta = 0u;
    if (!forcePlain)
    {
        constexpr u32 kMetaFlagExactRegularCaptureUses3d = 1u << 19u;
        constexpr u32 kMetaFlagRegularCaptureUses3d = 1u << 21u;
        constexpr u32 kMetaFlagVramCaptureUses3d = 1u << 22u;
        const u16 brightness = engine == 0u ? GPU.MasterBrightnessA : GPU.MasterBrightnessB;
        const u32 dispCnt = engine == 0u ? GPU.GPU2D_A.DispCnt : GPU.GPU2D_B.DispCnt;
        const u32 xPos = GPU.GPU3D.GetRenderXPos();
        u32 rendererMetaFlags = 0u;
        if (displayMode == 2u && StructuredFrameCaptureLineUses3D[engineLine] != 0u)
        {
            rendererMetaFlags |= kMetaFlagVramCaptureUses3d;
        }
        else if (displayMode == 1u)
        {
            const bool exactCaptureLineUses3d =
                StructuredEngineLineUsesCapture3D[
                    (static_cast<std::size_t>(engine) * 192u) + engineLine] != 0u;
            const u32 engineACaptureCnt = GPU.CaptureCnt;
            const bool broadCaptureLineUses3d =
                engine == 1u
                && (engineACaptureCnt & (1u << 31u)) != 0u
                && ((engineACaptureCnt >> 20u) & 0x3u) == 3u
                && StructuredFrameCaptureLineUses3D[engineLine] != 0u;
            if (exactCaptureLineUses3d || broadCaptureLineUses3d)
            {
                rendererMetaFlags |= kMetaFlagRegularCaptureUses3d;
                if (exactCaptureLineUses3d)
                    rendererMetaFlags |= kMetaFlagExactRegularCaptureUses3d;
            }
        }
        lineMeta = static_cast<u32>(brightness)
            | (dispCnt & 0x30000u)
            | rendererMetaFlags
            | (xPos << 24u)
            | ((xPos & 0x100u) << 15u);
    }
    packedScreen[packedRowBase + 768u] = lineMeta;

    u32* const structuredPlane0 =
        StructuredScreenPlanes.data() + destinationBase + screenRowBase;
    u32* const structuredPlane1 =
        StructuredScreenPlanes.data() + destinationBase + StructuredPixelCount + screenRowBase;
    u32* const structuredControl =
        StructuredScreenPlanes.data()
        + destinationBase
        + (2u * StructuredPixelCount)
        + screenRowBase;
    bool copiedStructured = false;
    if (!forcePlain && displayMode == 1u)
    {
        std::memcpy(
            structuredPlane0,
            StructuredEnginePlanes.data() + sourceBase + engineRowBase,
            256u * sizeof(u32));
        std::memcpy(
            structuredPlane1,
            StructuredEnginePlanes.data() + sourceBase + StructuredPixelCount + engineRowBase,
            256u * sizeof(u32));
        std::memcpy(
            structuredControl,
            StructuredEnginePlanes.data()
                + sourceBase
                + (2u * StructuredPixelCount)
                + engineRowBase,
            256u * sizeof(u32));
        copiedStructured = true;
    }
    else if (!forcePlain && engine == 0u && displayMode == 2u)
    {
        const u32 bank = (GPU.GPU2D_A.DispCnt >> 18u) & 0x3u;
        const std::size_t validIndex = static_cast<std::size_t>(bank) * 192u + engineLine;
        if ((GPU.VRAMMap_LCDC & (1u << bank)) != 0u
            && StructuredCaptureLineValid[validIndex] != 0u)
        {
            const std::size_t captureBase =
                static_cast<std::size_t>(bank) * 3u * StructuredPixelCount;
            std::memcpy(
                structuredPlane0,
                StructuredCapturePlanes.data() + captureBase + engineRowBase,
                256u * sizeof(u32));
            std::memcpy(
                structuredPlane1,
                StructuredCapturePlanes.data()
                    + captureBase
                    + StructuredPixelCount
                    + engineRowBase,
                256u * sizeof(u32));
            std::memcpy(
                structuredControl,
                StructuredCapturePlanes.data()
                    + captureBase
                    + (2u * StructuredPixelCount)
                    + engineRowBase,
                256u * sizeof(u32));
            copiedStructured = true;
        }
    }
    if (!copiedStructured)
    {
        std::fill_n(structuredPlane0, 256u, 0u);
        std::fill_n(structuredPlane1, 256u, 0u);
        std::fill_n(structuredControl, 256u, 0u);
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

void SoftRenderer::RequestStructuredVulkanResync() noexcept
{
    StructuredVulkanResyncRequested.store(true, std::memory_order_release);
}

void SoftRenderer::SwapBuffers()
{
    if (UseStructuredVulkan2D() && StructuredFrameValid)
    {
        const std::lock_guard<std::mutex> completedFrameLock(CompletedStructuredVulkanFrameMutex);
        constexpr u32 kPacked3dPlaceholder = 0x20000000u;
        constexpr u32 kMetaFlagRegularCaptureUses3d = 1u << 21u;
        constexpr u32 kMetaFlagVramCaptureUses3d = 1u << 22u;
        constexpr u32 kMetaFlagForceLive3dCompMode7 = 1u << 18u;
        const bool currentScreenSwapLatched = GPU.GPU3D.GetRenderScreenSwapAt3D();
        const StructuredVulkanFrameSnapshot* previousCompletedFrame = nullptr;
        for (const auto& candidate : CompletedStructuredVulkanFrames)
        {
            if (candidate.Valid
                && (previousCompletedFrame == nullptr
                    || candidate.Generation > previousCompletedFrame->Generation))
            {
                previousCompletedFrame = &candidate;
            }
        }
        const bool screenSwapToggledThisFrame =
            previousCompletedFrame != nullptr
            && previousCompletedFrame->ScreenSwapLatched != currentScreenSwapLatched;

        const auto& rawTop =
            VulkanPackedFramebuffer[static_cast<std::size_t>(BackBuffer & 1)][0];
        const auto& rawBottom =
            VulkanPackedFramebuffer[static_cast<std::size_t>(BackBuffer & 1)][1];
        const auto countCaptureUses3dLines =
            [](const std::array<u32, VulkanPackedPixelCount>& packed,
                u32 flag,
                u32 requiredDisplayMode) {
                int count = 0;
                for (std::size_t y = 0; y < 192u; ++y)
                {
                    const u32 meta = packed[(y * VulkanPackedStride) + 768u];
                    if (((meta >> 16u) & 0x3u) == requiredDisplayMode
                        && (meta & flag) != 0u)
                    {
                        ++count;
                    }
                }
                return count;
            };
        const auto countDisplayModeLines =
            [](const std::array<u32, VulkanPackedPixelCount>& packed,
                u32 requiredDisplayMode) {
                int count = 0;
                for (std::size_t y = 0; y < 192u; ++y)
                {
                    const u32 meta = packed[(y * VulkanPackedStride) + 768u];
                    if (((meta >> 16u) & 0x3u) == requiredDisplayMode)
                        ++count;
                }
                return count;
            };
        const int topRegularCaptureLineCount =
            countCaptureUses3dLines(rawTop, kMetaFlagRegularCaptureUses3d, 1u);
        const int bottomRegularCaptureLineCount =
            countCaptureUses3dLines(rawBottom, kMetaFlagRegularCaptureUses3d, 1u);
        const int topVramDisplayLineCount = countDisplayModeLines(rawTop, 2u);
        const int bottomVramDisplayLineCount = countDisplayModeLines(rawBottom, 2u);
        const bool topHasPartialRegularCapture =
            topRegularCaptureLineCount > 0 && topRegularCaptureLineCount < 192;
        const bool bottomHasPartialRegularCapture =
            bottomRegularCaptureLineCount > 0 && bottomRegularCaptureLineCount < 192;
        const bool captureBackedClass4Only =
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
        const bool captureBackedHasStructured2DSource =
            StructuredCaptureBacked3DLines > 0u
            && captureBackedDominantStructured2DLines > (StructuredCaptureBacked3DLines / 2u)
            && captureBackedDominantStructured2DLines > StructuredCaptureBackedBestClassLines[0];
        int captureLineUses3dCount = 0;
        for (const u8 uses3d : StructuredFrameCaptureLineUses3D)
            captureLineUses3dCount += uses3d != 0u ? 1 : 0;
        const bool captureBackedFullClass0Only =
            StructuredCaptureBacked3DLines == 192u
            && StructuredCaptureBackedBestClassLines[0] == StructuredCaptureBacked3DLines
            && StructuredCaptureBackedBestClassLines[1] == 0u
            && StructuredCaptureBackedBestClassLines[2] == 0u
            && StructuredCaptureBackedBestClassLines[4] == 0u
            && StructuredCaptureBackedBestClassLines[8] == 0u
            && StructuredCaptureBackedBestClassLines[16] == 0u;
        const bool captureBackedFullClass0AlternatingCapture =
            captureBackedFullClass0Only
            && StructuredCaptureMode >= 2u
            && captureLineUses3dCount == 192
            && ((topVramDisplayLineCount > 96 && bottomVramDisplayLineCount == 0)
                || (bottomVramDisplayLineCount > 96 && topVramDisplayLineCount == 0));

        StructuredVulkanFrameSnapshot& completedFrame =
            CompletedStructuredVulkanFrames[static_cast<std::size_t>(BackBuffer & 1)];
        if (completedFrame.Completed3DReference.Valid)
            Rend3D->ReleaseCompletedFrameReference(completedFrame.Completed3DReference);
        completedFrame.Completed3DReference = {};
        completedFrame.PackedTop = rawTop;
        completedFrame.PackedBottom = rawBottom;

        const auto structuredLineHasPayload =
            [](const u32* plane0, const u32* plane1, const u32* control, std::size_t rowBase) {
                for (std::size_t x = 0; x < 256u; ++x)
                {
                    const std::size_t index = rowBase + x;
                    if (control[index] != 0u || plane1[index] != 0u || plane0[index] != 0u)
                        return true;
                }
                return false;
            };
        const auto packedRawLineHas3dSlot =
            [](const std::array<u32, VulkanPackedPixelCount>& packed, std::size_t y) {
                const std::size_t packedRowBase = y * VulkanPackedStride;
                for (std::size_t x = 0; x < 256u; ++x)
                {
                    const u32 plane0Alpha = packed[packedRowBase + x] >> 24u;
                    const u32 plane1Alpha = packed[packedRowBase + 256u + x] >> 24u;
                    const u32 controlAlpha = packed[packedRowBase + 512u + x] >> 24u;
                    if ((plane0Alpha & 0xC0u) == 0x40u
                        || (plane1Alpha & 0xC0u) == 0x40u
                        || (controlAlpha & 0x40u) != 0u)
                    {
                        return true;
                    }
                }
                return false;
            };
        const auto copyStructuredLine =
            [](std::array<u32, VulkanPackedPixelCount>& packed,
                const u32* structuredPlane0,
                const u32* structuredPlane1,
                const u32* structuredControl,
                std::size_t y,
                std::size_t rowBase) {
                const std::size_t packedRowBase = y * VulkanPackedStride;
                std::memcpy(
                    packed.data() + packedRowBase,
                    structuredPlane0 + rowBase,
                    256u * sizeof(u32));
                std::memcpy(
                    packed.data() + packedRowBase + 256u,
                    structuredPlane1 + rowBase,
                    256u * sizeof(u32));
                std::memcpy(
                    packed.data() + packedRowBase + 512u,
                    structuredControl + rowBase,
                    256u * sizeof(u32));
            };
        const auto mergeStructuredDisplayLine =
            [&](std::array<u32, VulkanPackedPixelCount>& packed,
                const u32* structuredPlane0,
                const u32* structuredPlane1,
                const u32* structuredControl,
                std::size_t y,
                std::size_t rowBase) {
                const std::size_t packedRowBase = y * VulkanPackedStride;
                for (std::size_t x = 0; x < 256u; ++x)
                {
                    const std::size_t index = rowBase + x;
                    const std::size_t packedIndex = packedRowBase + x;
                    const u32 packedPlane0 = packed[packedIndex];
                    const u32 packedPlane1 = packed[packedRowBase + 256u + x];
                    const u32 packedControl = packed[packedRowBase + 512u + x];
                    const u32 packedPlane0Alpha = packedPlane0 >> 24u;
                    const u32 packedPlane1Alpha = packedPlane1 >> 24u;
                    const u32 packedControlAlpha = packedControl >> 24u;
                    const bool packedNeeds3DSlot =
                        (packedPlane0Alpha & 0xC0u) == 0x40u
                        || (packedPlane1Alpha & 0xC0u) == 0x40u
                        || (packedControlAlpha & 0x40u) != 0u;
                    const u32 structuredP0 = structuredPlane0[index];
                    const u32 structuredP1 = structuredPlane1[index];
                    const u32 structuredC = structuredControl[index];
                    const bool structuredHasRenderablePayload =
                        (structuredP0 != 0u && structuredP0 != kPacked3dPlaceholder)
                        || (structuredP1 != 0u && structuredP1 != kPacked3dPlaceholder);
                    const u32 structuredControlAlpha = structuredC >> 24u;
                    const bool structuredHas3DSlot =
                        ((structuredP0 >> 24u) & 0xC0u) == 0x40u
                        || ((structuredP1 >> 24u) & 0xC0u) == 0x40u
                        || (structuredControlAlpha & 0x40u) != 0u;
                    const bool structuredHasAbove =
                        (structuredControlAlpha & 0x40u) != 0u
                        && (structuredControlAlpha & 0x80u) != 0u
                        && structuredP1 != 0u;
                    const bool packedHasCurrent2D =
                        (packedPlane0 != 0u && packedPlane0 != kPacked3dPlaceholder)
                        || (packedPlane1 != 0u && packedPlane1 != kPacked3dPlaceholder);
                    const bool packedCurrent2DOnly = packedHasCurrent2D && !packedNeeds3DSlot;

                    if (!structuredHasRenderablePayload && !(packedNeeds3DSlot && structuredHas3DSlot))
                    {
                        if (structuredHas3DSlot && packedCurrent2DOnly)
                        {
                            packed[packedRowBase + 512u + x] =
                                (packedControl & 0x00FFFFFFu)
                                | ((packedControlAlpha | 0x80u) << 24u);
                        }
                        continue;
                    }

                    packed[packedIndex] = structuredP0;
                    packed[packedRowBase + 256u + x] = structuredP1;
                    packed[packedRowBase + 512u + x] = structuredC;
                    if (structuredHas3DSlot && !structuredHasAbove && packedCurrent2DOnly)
                    {
                        packed[packedRowBase + 256u + x] = packedPlane0;
                        const u32 overlayControlRgb =
                            captureBackedClass4Only
                                && screenSwapToggledThisFrame
                                && (packedControl & 0x00FFFFFFu) != 0u
                            ? (packedControl & 0x00FFFFFFu)
                            : (structuredC & 0x00FFFFFFu);
                        const bool protectedBlack =
                            packedPlane0 != 0u
                            && packedPlane0 != kPacked3dPlaceholder
                            && (packedPlane0 & 0x00FFFFFFu) == 0u;
                        packed[packedRowBase + 512u + x] =
                            overlayControlRgb
                            | ((structuredControlAlpha
                                | 0x40u
                                | 0x80u
                                | (protectedBlack ? 0x20u : 0u)) << 24u);
                    }
                }
            };

        const u32* const structuredTopPlane0 = StructuredScreenPlanes.data();
        const u32* const structuredTopPlane1 =
            StructuredScreenPlanes.data() + StructuredPixelCount;
        const u32* const structuredTopControl =
            StructuredScreenPlanes.data() + (2u * StructuredPixelCount);
        const u32* const structuredBottomPlane0 =
            StructuredScreenPlanes.data() + (3u * StructuredPixelCount);
        const u32* const structuredBottomPlane1 =
            StructuredScreenPlanes.data() + (4u * StructuredPixelCount);
        const u32* const structuredBottomControl =
            StructuredScreenPlanes.data() + (5u * StructuredPixelCount);
        for (std::size_t y = 0; y < 192u; ++y)
        {
            const std::size_t packedRowBase = y * VulkanPackedStride;
            const std::size_t rowBase = y * 256u;
            const auto mergeScreen =
                [&](std::array<u32, VulkanPackedPixelCount>& packed,
                    const u32* plane0,
                    const u32* plane1,
                    const u32* control,
                    bool hasPartialRegularCapture) {
                    const u32 lineMeta = packed[packedRowBase + 768u];
                    const u32 displayMode = (lineMeta >> 16u) & 0x3u;
                    const bool partialRegularCaptureLine =
                        hasPartialRegularCapture
                        && (lineMeta & kMetaFlagRegularCaptureUses3d) != 0u;
                    const bool hasPayload = structuredLineHasPayload(
                        plane0,
                        plane1,
                        control,
                        rowBase);
                    const bool lineNeedsStructured3d =
                        (!captureBackedHasStructured2DSource
                            && !captureBackedFullClass0AlternatingCapture)
                        || (lineMeta & (kMetaFlagRegularCaptureUses3d
                            | kMetaFlagVramCaptureUses3d
                            | kMetaFlagForceLive3dCompMode7)) != 0u
                        || packedRawLineHas3dSlot(packed, y);
                    const bool structuredDisplayLine =
                        displayMode == 1u
                        && lineNeedsStructured3d
                        && (!partialRegularCaptureLine || hasPayload);
                    const bool structuredVramCapture =
                        displayMode == 2u
                        && (lineMeta & kMetaFlagVramCaptureUses3d) != 0u
                        && hasPayload;
                    if (structuredDisplayLine
                        && (captureBackedHasStructured2DSource
                            || captureBackedFullClass0AlternatingCapture))
                    {
                        mergeStructuredDisplayLine(packed, plane0, plane1, control, y, rowBase);
                    }
                    else if (structuredDisplayLine || structuredVramCapture)
                    {
                        copyStructuredLine(packed, plane0, plane1, control, y, rowBase);
                    }
                };
            mergeScreen(
                completedFrame.PackedTop,
                structuredTopPlane0,
                structuredTopPlane1,
                structuredTopControl,
                topHasPartialRegularCapture);
            mergeScreen(
                completedFrame.PackedBottom,
                structuredBottomPlane0,
                structuredBottomPlane1,
                structuredBottomControl,
                bottomHasPartialRegularCapture);
        }

        // Preserve Sapphire's pre-merge physical structured planes alongside
        // the post-merge packed result. Both belong to this same immutable
        // generation and the latch tail intentionally consults both.
        completedFrame.ScreenPlanes = StructuredScreenPlanes;
        for (std::size_t y = 0; y < 192u; ++y)
        {
            const std::size_t packedRow = y * VulkanPackedStride;
            completedFrame.ScreenLineMeta[y] =
                completedFrame.PackedTop[packedRow + 768u];

            completedFrame.ScreenLineMeta[192u + y] =
                completedFrame.PackedBottom[packedRow + 768u];
        }
        completedFrame.Capture3DSource = StructuredCapture3DSource;
        completedFrame.CaptureLineUses3D = StructuredFrameCaptureLineUses3D;
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
        completedFrame.ScreenSwapLatched = currentScreenSwapLatched;
        Renderer3DCompletedFrameReference completed3DReference{};
        if (Rend3D->AcquireCompletedFrameForStructured(completed3DReference))
            completedFrame.Completed3DReference = completed3DReference;
        completedFrame.Renderer3DOwnerIsTop = completedFrame.Completed3DReference.Valid
            ? completedFrame.Completed3DReference.OwnerIsTop()
            : currentScreenSwapLatched;
#ifndef NDEBUG
        if (completedFrame.Completed3DReference.Valid)
        {
            // Sapphire obtains both values from the same post-RunFrame
            // VCount-215 ownership latch.  A mismatch here would reintroduce
            // a whole-screen Top/Bottom reversal at the compositor boundary.
            assert(completedFrame.Renderer3DOwnerIsTop == currentScreenSwapLatched);
        }
#endif
        completedFrame.CaptureBackedClass4Only = captureBackedClass4Only;
        completedFrame.CaptureBackedPartialClass0Only =
            StructuredCaptureBacked3DLines > 0u
            && StructuredCaptureBacked3DLines < 192u
            && StructuredCaptureBackedBestClassLines[0] == StructuredCaptureBacked3DLines
            && StructuredCaptureBackedBestClassLines[1] == 0u
            && StructuredCaptureBackedBestClassLines[2] == 0u
            && StructuredCaptureBackedBestClassLines[4] == 0u
            && StructuredCaptureBackedBestClassLines[8] == 0u
            && StructuredCaptureBackedBestClassLines[16] == 0u;
        completedFrame.CaptureBackedFullClass0AlternatingCapture =
            captureBackedFullClass0AlternatingCapture;
        completedFrame.CaptureBackedHasStructured2DSource = captureBackedHasStructured2DSource;
        completedFrame.StructuredCopyLines = StructuredCopyLines;
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
