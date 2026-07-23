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

namespace MelonDSAndroid
{
bool areRendererDebugToolsEnabled();
}
#endif

namespace melonDS
{

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
namespace
{
// MELONPRIME-PORT-ADAPT: duplicate of the same-named helper in GPU2D_Soft.cpp's anonymous
// namespace (reference GPU2D_Soft.cpp ~47-57, ported verbatim there). Needed here too because
// SoftRenderer::DoCapture's structured branch (this file) does its own capture-specific 3D-source
// classification; anonymous-namespace helpers are file-local in C++, so they can't be shared
// across translation units without moving them to a header (out of scope for this minimal-diff
// producer port -- see class-level comments in GPU2D_Soft.h/.cpp for the rest of this port's
// scope boundary).
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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // Sapphire's one CurUnit-switching SoftRenderer owns one structured output/capture state for
    // both engines. Recreate that ownership before either PC engine can render a scanline.
    static_cast<SoftRenderer2D&>(*Rend2D_B).ShareStructuredVulkan2DStateFrom(
        static_cast<const SoftRenderer2D&>(*Rend2D_A));

    // MELONPRIME-PORT: see GPU.h GPU::GetRenderer2D() / GPU2D_Soft.h StructuredVulkan2DRendererBridge
    // class-level comments. Constructed here (ctor body, not the initializer list) because it needs
    // Rend2D_A/Rend2D_B already constructed as SoftRenderer2D above.
    Renderer2DBridge.emplace(
        static_cast<SoftRenderer2D&>(*Rend2D_A),
        static_cast<SoftRenderer2D&>(*Rend2D_B),
        CaptureLineUses3d,
        LastDebugCaptureStats,
        LastDebugCapture3dSource,
        HasLastDebugCapture3dSource);
#endif
}

SoftRenderer::~SoftRenderer()
{
    delete[] Framebuffer[0][0];
    delete[] Framebuffer[0][1];
    delete[] Framebuffer[1][0];
    delete[] Framebuffer[1][1];

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    FreeAccelFramebuffers();
#endif
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
    if (Rend3D->UsesStructured2DMetadata())
        EnsureAccelFramebuffers();
    if (FramebufferAccelAllocated)
    {
        const size_t accelLen = static_cast<size_t>(kAccelFramebufferStride) * kAccelFramebufferLineCount;
        for (auto& buffer : FramebufferAccel)
        {
            for (u32* screen : buffer)
                std::fill_n(screen, accelLen, 0xFFFFFFFFu);
        }
    }
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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    if (FramebufferAccelAllocated)
    {
        const size_t accelLen = static_cast<size_t>(kAccelFramebufferStride) * kAccelFramebufferLineCount;
        for (auto& buffer : FramebufferAccel)
        {
            for (u32* screen : buffer)
                std::fill_n(screen, accelLen, 0u);
        }
    }
#endif
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
    u32 *dstA, *dstB;
    u32 dstoffset = 256 * line;
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // MELONPRIME-PORT-ADAPT: reference's accelerated Framebuffer row index is this call's ORIGINAL
    // `line` parameter (target scanline), captured before the `line = GPU.VCount` reassignment
    // below -- see reference GPU2D_Soft.cpp SoftRenderer::DrawScanline(u32 line, Unit* unit): its
    // `dst` pointer is computed from the original `line` param too, before that same reassignment.
    // Mirrors this file's own dstA/dstB pointers just below, which use the same pre-reassignment
    // value for the same reason.
    const u32 accelRow = line;
    if (!FramebufferAccelAllocated && Rend3D->UsesStructured2DMetadata())
        EnsureAccelFramebuffers();
#endif
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
        if (Rend3D->UsesStructured2DMetadata())
        {
            // Sapphire's accelerated path does not fetch the CPU 3D line
            // before drawing 2D.  It installs the capture screen-owner hint
            // first and fetches the line on demand from DoCapture().
            Output3D = nullptr;

            // Sapphire renders and captures engine A before engine B. Engine B's structured
            // producer consumes CaptureLineUses3d written by that same-line capture.
            Rend2D_A->DrawScanline(line);
            const auto& rend2dA = static_cast<const SoftRenderer2D&>(*Rend2D_A);
            std::memcpy(AccelBGOBJLine[0], rend2dA.BGOBJLine, sizeof(AccelBGOBJLine[0]));
            DrawScanlineA(line, dstA);

            if (GPU.CaptureEnable)
                DoCapture(line, accelRow);

            Rend2D_B->DrawScanline(line);
            const auto& rend2dB = static_cast<const SoftRenderer2D&>(*Rend2D_B);
            std::memcpy(AccelBGOBJLine[1], rend2dB.BGOBJLine, sizeof(AccelBGOBJLine[1]));
            DrawScanlineB(line, dstB);
        }
        else
#endif
        {
            // retrieve 3D output
            Output3D = Rend3D->GetLine(line);

        // draw BG/OBJ layers
            Rend2D_A->DrawScanline(line);
            Rend2D_B->DrawScanline(line);

        // draw the final screen output
            DrawScanlineA(line, dstA);
            DrawScanlineB(line, dstB);

        // perform display capture if enabled
            if (GPU.CaptureEnable)
                DoCapture(line, line);
        }

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        if (FramebufferAccelAllocated)
            PackAccelFramebufferLine(accelRow, line);
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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        if (FramebufferAccelAllocated)
            ClearAccelFramebufferLine(accelRow);
#endif
    }

    if (GPU.ScreensEnabled)
    {
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
    }
}

void SoftRenderer::DrawSprites(u32 line)
{
    Rend2D_A->DrawSprites(line);
    Rend2D_B->DrawSprites(line);
}

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
// MELONPRIME-PORT: reference GPU2D_Soft.cpp SoftRenderer::VBlankEnd(Unit* unitA, Unit* unitB)
// (~1244-1273). MELONPRIME-PORT-ADAPT: our Renderer::VBlankEnd() override takes no parameters
// (see GPU.h); unitA/unitB are replaced with GPU.GPU2D_A directly (only unitA/engine-A fields
// are read by this hunk). renderer3d is replaced with *Rend3D (this SoftRenderer already owns
// the active Renderer3D; GetCurrentRenderer() has no equivalent -- Rend3D IS the current
// renderer). Reference's #ifdef OGLRENDERER_ENABLED has no equivalent in our build and is
// dropped in favor of the surrounding MELONPRIME_ENABLE_VULKAN guard.
void SoftRenderer::VBlankEnd()
{
    if (Rend3D->Accelerated)
    {
        const u32 captureCnt = GPU.GPU2D_A.CaptureCnt;
        const u32 captureMode = (captureCnt >> 29u) & 0x3u;
        const bool captureEnabled = (captureCnt & (1u << 31u)) != 0u;
        if (!Rend3D->UsesStructured2DMetadata())
        {
            if (captureEnabled && captureMode != 1u)
                Rend3D->PrepareCaptureFrame();
            return;
        }

        const bool captureUsesDirect3D = (captureCnt & (1u << 24u)) != 0u;
        const bool sourceAContributes = captureMode == 0u
            || ((captureMode >= 2u) && ((captureCnt & 0x1Fu) != 0u));
        const bool bg0Uses3D = (GPU.GPU2D_A.DispCnt & 0x0108u) == 0x0108u;
        if (captureEnabled
            && captureMode != 1u
            && (captureUsesDirect3D || (bg0Uses3D && sourceAContributes)))
        {
            // Sapphire reads POWCNT1 bit 15 at this exact hook. GPU.ScreenSwap
            // is this fork's directly-updated equivalent of that register bit.
            Rend3D->SetCaptureScreenSwapHint(GPU.ScreenSwap);
            Rend3D->BeginCaptureFrame();
            Rend3D->PrepareCaptureFrame();
        }
    }
}

// MELONPRIME-PORT: reference-generation (Sapphire) accelerated Framebuffer channel storage/
// producer. See GetAccelFramebuffer()/GetRenderer2D() class-level comments in GPU_Soft.h for the
// design rationale (parallel buffer, physical-screen-indexed, lazily allocated).
void SoftRenderer::EnsureAccelFramebuffers()
{
    if (FramebufferAccelAllocated)
        return;

    const size_t len = static_cast<size_t>(kAccelFramebufferStride) * kAccelFramebufferLineCount;
    FramebufferAccel[0][0] = new u32[len]();
    FramebufferAccel[0][1] = new u32[len]();
    FramebufferAccel[1][0] = new u32[len]();
    FramebufferAccel[1][1] = new u32[len]();
    FramebufferAccelAllocated = true;
}

void SoftRenderer::FreeAccelFramebuffers()
{
    if (!FramebufferAccelAllocated)
        return;

    delete[] FramebufferAccel[0][0];
    delete[] FramebufferAccel[0][1];
    delete[] FramebufferAccel[1][0];
    delete[] FramebufferAccel[1][1];
    FramebufferAccel[0][0] = nullptr;
    FramebufferAccel[0][1] = nullptr;
    FramebufferAccel[1][0] = nullptr;
    FramebufferAccel[1][1] = nullptr;
    FramebufferAccelAllocated = false;
}

// MELONPRIME-PC-ADAPT: reference GPU2D_Soft.cpp SoftRenderer::DrawScanline(u32 line, Unit* unit)
// writes directly into the accelerated GPU framebuffer. This fork keeps that framebuffer beside
// its legacy flat framebuffer, so reproduce the reference's four display-mode branches here after
// the per-engine SoftRenderer2D has finished the same BG/OBJ work. In particular, regular display
// must copy the raw three-plane BGOBJLine. StructuredVulkan2DPlanes is a separate supplemental
// snapshot consumed independently by MelonInstanceVulkan and is not a substitute for those raw
// packed planes.
void SoftRenderer::PackAccelFramebufferSlot(
    u32* slotBase, u32 row, u32 contentLine, bool /*topScreen*/,
    SoftRenderer2D& unitRenderer, melonDS::GPU2D& unit)
{
    u32* dst = slotBase + static_cast<size_t>(kAccelFramebufferStride) * row;

    // Sapphire force-blanks engine B when POWCNT1 disables it, but not engine A. Its accelerated
    // renderer writes an opaque-white plane and clears the per-line metadata before returning.
    // Do that before consulting BGOBJLine so a disabled/forced-blank unit cannot republish the
    // preceding scanline's packed layers.
    if (unit.ForcedBlank || (unit.Num != 0 && !unit.Enabled))
    {
        for (u32 i = 0; i < 256u; i++)
            dst[i] = 0xFFFFFFFFu;
        dst[kAccelFramebufferStride - 1u] = 0u;
        return;
    }

    const u32 dispmodeMask = unit.Num ? 0x1u : 0x3u;
    const u32 dispmode = (unit.DispCnt >> 16) & dispmodeMask;

    switch (dispmode)
    {
    case 0: // screen off
        for (u32 i = 0; i < 256u; i++)
            dst[i] = 0x003F3F3Fu;
        break;

    case 1: // regular display
        for (u32 i = 0; i < 256u * 3u; i += 2u)
            *reinterpret_cast<u64*>(&dst[i]) =
                *reinterpret_cast<const u64*>(&AccelBGOBJLine[unit.Num][i]);
        break;

    case 2: // VRAM display (engine A only)
        {
            const u32 vramBank = (unit.DispCnt >> 18) & 0x3u;
            if (GPU.VRAMMap_LCDC & (1u << vramBank))
            {
                const u16* vram = reinterpret_cast<const u16*>(GPU.VRAM[vramBank]);
                vram += contentLine * 256u;
                for (u32 i = 0; i < 256u; i++)
                {
                    const u16 color = vram[i];
                    const u8 r = (color & 0x001Fu) << 1u;
                    const u8 g = (color & 0x03E0u) >> 4u;
                    const u8 b = (color & 0x7C00u) >> 9u;
                    dst[i] = r | (static_cast<u32>(g) << 8u) | (static_cast<u32>(b) << 16u);
                }
            }
            else
            {
                for (u32 i = 0; i < 256u; i++)
                    dst[i] = 0u;
            }
        }
        break;

    case 3: // FIFO display (engine A only)
        for (u32 i = 0; i < 256u; i++)
        {
            const u16 color = GPU.DispFIFOBuffer[i];
            const u8 r = (color & 0x001Fu) << 1u;
            const u8 g = (color & 0x03E0u) >> 4u;
            const u8 b = (color & 0x7C00u) >> 9u;
            dst[i] = r | (static_cast<u32>(g) << 8u) | (static_cast<u32>(b) << 16u);
        }
        break;
    }

    // reference GPU2D_Soft.cpp ~1158-1197 (trailing per-line renderer-metadata word).
    // kSoftPackedMetaFlagForceLive3dCompMode7 is not a core producer flag: Sapphire's
    // MelonInstance::latchSoftPackedFrameSnapshot derives it while repairing the latched copy.
    constexpr u32 kMetaFlagRegularCaptureUses3d = 1u << 21u;
    constexpr u32 kMetaFlagVramCaptureUses3d = 1u << 22u;
    constexpr u32 kMetaFlagExactRegularCaptureUses3d = 1u << 19u;
    const u32 xpos = GPU.GPU3D.GetRenderXPos();
    u32 rendererMetaFlags = 0u;
    const u32 engineACaptureCnt = GPU.GPU2D_A.CaptureCnt;
    const bool captureConfiguredFullScreen =
        (engineACaptureCnt & (1u << 31u)) != 0u
        && ((engineACaptureCnt >> 20u) & 0x3u) == 3u;

    if (dispmode == 2u)
    {
        if (contentLine < CaptureLineUses3d.size() && CaptureLineUses3d[contentLine] != 0)
            rendererMetaFlags |= kMetaFlagVramCaptureUses3d;
    }
    else if (dispmode == 1u)
    {
        const bool broadCaptureLineUses3d =
            unit.Num == 1
            && captureConfiguredFullScreen
            && contentLine < CaptureLineUses3d.size()
            && CaptureLineUses3d[contentLine] != 0;
        const bool exactLineUses3d = unitRenderer.CurrentLineRegularCaptureUses3d;
        if (exactLineUses3d || broadCaptureLineUses3d)
        {
            rendererMetaFlags |= kMetaFlagRegularCaptureUses3d;
            if (exactLineUses3d)
                rendererMetaFlags |= kMetaFlagExactRegularCaptureUses3d;
        }
    }

    const u16 masterBrightness = unit.Num ? GPU.MasterBrightnessB : GPU.MasterBrightnessA;
    dst[kAccelFramebufferStride - 1u] =
        static_cast<u32>(masterBrightness) |
        (unit.DispCnt & 0x30000u) |
        rendererMetaFlags |
        (static_cast<u32>(xpos) << 24) | ((static_cast<u32>(xpos) & 0x100u) << 15);
}

// MELONPRIME-PORT-ADAPT: packs BOTH physical screens for this scanline. Sapphire reassigns its
// accelerated framebuffer pointers immediately when POWCNT1 changes; GPU.ScreenSwap is this
// fork's equivalent live assignment.
void SoftRenderer::PackAccelFramebufferLine(u32 row, u32 contentLine)
{
    auto& rend2dA = static_cast<SoftRenderer2D&>(*Rend2D_A);
    auto& rend2dB = static_cast<SoftRenderer2D&>(*Rend2D_B);
    const bool aTargetsTop = GPU.ScreenSwap;

    PackAccelFramebufferSlot(
        FramebufferAccel[BackBuffer][0], row, contentLine, /* topScreen */ true,
        aTargetsTop ? rend2dA : rend2dB, aTargetsTop ? GPU.GPU2D_A : GPU.GPU2D_B);
    PackAccelFramebufferSlot(
        FramebufferAccel[BackBuffer][1], row, contentLine, /* topScreen */ false,
        aTargetsTop ? rend2dB : rend2dA, aTargetsTop ? GPU.GPU2D_B : GPU.GPU2D_A);
    Renderer2DBridge->LatchStructuredVulkan2DLine(
        BackBuffer, row, contentLine, aTargetsTop);
}

// MELONPRIME-PORT-ADAPT: reference forceblank branch (GPU2D_Soft.cpp ~1059-1069): fills plane0
// white and zeroes the metadata word for out-of-range (VCOUNT-write edge case) scanlines. Our
// per-line call packs both screens in one go (see PackAccelFramebufferLine above), unlike
// reference's per-unit dispatch, since both screens need identical treatment here.
void SoftRenderer::ClearAccelFramebufferLine(u32 row)
{
    for (int screen = 0; screen < 2; screen++)
    {
        u32* dst = FramebufferAccel[BackBuffer][screen] + static_cast<size_t>(kAccelFramebufferStride) * row;
        for (u32 i = 0; i < 256u; i++)
            dst[i] = 0xFFFFFFFFu;
        dst[kAccelFramebufferStride - 1u] = 0u;
    }
}
#endif

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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
                // MELONPRIME-PORT: reference GPU2D_Soft.cpp ~1097-1116 (VRAM display mode branch
                // of DrawScanline calls CopyStructuredVulkan2DCaptureLineToCurrentScreen when
                // useStructuredVulkan2D, so structured planes for this line mirror whatever was
                // captured into this VRAM bank). MELONPRIME-PORT-ADAPT: our fork composites VRAM
                // display mode here in SoftRenderer::DrawScanlineA rather than reference's
                // monolithic per-unit SoftRenderer::DrawScanline; VRAM display mode only exists
                // for engine A (2-bit DispCnt display-mode field; engine B is 1-bit), so this
                // reaches into Rend2D_A specifically via the SoftRenderer2D friend grant (see
                // GPU2D_Soft.h). SoftRenderer::SoftRenderer always constructs Rend2D_A as
                // SoftRenderer2D (see ctor above), so a static_cast is safe here.
                auto* rend2dA = static_cast<SoftRenderer2D*>(Rend2D_A.get());
                if (rend2dA->UseStructuredVulkan2D())
                    rend2dA->CopyStructuredVulkan2DCaptureLineToCurrentScreen(line, vrambank);
#endif
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

void SoftRenderer::DoCapture(u32 line, u32 sourceLine)
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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    const u32 captureMode = (captureCnt >> 29u) & 0x3u;
    const bool captureUsesDirect3D = (captureCnt & (1u << 24u)) != 0u;
    const bool captureDebugEnabled = MelonDSAndroid::areRendererDebugToolsEnabled();
    if (line == 0)
    {
        HasLastDebugCapture3dSource = false;
        LastDebugCapture3dSource.fill(0u);
        LastDebugCaptureStats = {};
        LastDebugCaptureStats.CaptureWidth = width;
        LastDebugCaptureStats.CaptureMode = captureMode;
        LastDebugCaptureStats.CaptureBit24 = captureUsesDirect3D ? 1u : 0u;
    }
    LastDebugCaptureStats.CaptureLines++;
#endif

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

    u16* srcB = nullptr;
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // MELONPRIME-PORT-ADAPT: reference's structured branch additionally records which VRAM bank
    // (if any) backs source B, to sample previously-captured structured overlay pixels from it
    // (GPU2D_Soft.cpp ~1516-1524, structuredSourceBVram/structuredSourceBFromVram). Captured here
    // alongside the existing srcB setup rather than recomputed separately.
    u32 structuredSourceBVram = 4u;
    bool structuredSourceBFromVram = false;
    u32 structuredSourceBBaseAddr = 0u;
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
            structuredSourceBVram = srcvram;
            structuredSourceBFromVram = true;
#endif

            u32 offset = line * 256;
            if (((dispcnt >> 16) & 0x3) != 2)
                offset += (((captureCnt >> 26) & 0x3) << 14);

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
            // MELONPRIME-PORT-ADAPT: reference GPU2D_Soft.cpp ~1531 (structuredSourceBBaseAddr =
            // srcBaddr, captured before the &0xFFFF wraparound applied to the srcB pointer below).
            structuredSourceBBaseAddr = offset & 0xFFFF;
#endif
            srcB += (offset & 0xFFFF);
        }
    }

    static_assert(VRAMDirtyGranularity == 512);
    GPU.VRAMDirty[dstvram][(dstaddr * 2) / VRAMDirtyGranularity] = true;

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // MELONPRIME-PORT: reference GPU2D_Soft.cpp ~1275-1997 (SoftRenderer::DoCapture's structured
    // branch). MELONPRIME-PORT-ADAPT: reference is a method of the single CurUnit-switching
    // SoftRenderer/SoftRenderer2D-equivalent class and reads/writes BGOBJLine and the structured
    // Store/Copy/Read/Merge method family directly; our fork splits those onto SoftRenderer2D
    // (Rend2D_A specifically -- capture always sources from engine A), reached here via the
    // SoftRenderer2D friend grant (see GPU2D_Soft.h). CaptureLineUses3d itself is a direct member
    // of this class (see GPU_Soft.h), matching reference's own field placement on the class that
    // owns DoCapture -- no Parent indirection needed for it here (SoftRenderer2D reaches it via
    // Parent since IT is the one split off). `line`/`_3DLine` in reference correspond to our `line`/`Output3D`
    // (this fork's SoftRenderer::DrawScanline always eagerly resolves Output3D via
    // Rend3D->GetLine(line) before DoCapture runs -- see class-level comment on
    // Renderer3D::UsesStructured2DMetadata()/GetLine in GPU3D.h -- so there is no separate
    // "n3dline vs VCount line" distinction or on-demand 3D-line fetch to replicate here; both use
    // `line`, matching this fork's pre-existing convention already visible in the non-structured
    // srcA selection above). Sapphire's capture metadata/statistics that feed the latch heuristics
    // are retained; Android-only sample-point logging is not part of the desktop presentation path.
    auto* rend2dA = static_cast<SoftRenderer2D*>(Rend2D_A.get());
    const bool useStructuredVulkan2D = rend2dA->UseStructuredVulkan2D();
    bool structuredCaptureStoredFromSourceA = false;
    bool captureLineUses3d = false;
    bool captureBlendsStructuredSourceB = false;
    std::array<u16, 256> structuredCaptureOutputPixels {};
    std::array<u32, 256> structuredSourceBOverlayPixels {};
    std::array<u32, 256> structuredSourceBOverlayControlAlpha {};
    const u32 structuredCaptureDstBase = dstaddr & 0xFFFFu;

    if (useStructuredVulkan2D)
    {
        // Sapphire reads POWCNT1 bit 15 at capture time. GPU.ScreenSwap is the
        // directly-updated equivalent in this fork; RenderScreenSwapAt3D is an
        // older VCount-215 snapshot and is not interchangeable here.
        const bool captureScreenSwap = GPU.ScreenSwap;

        // MELONPRIME-PORT: reference GPU2D_Soft.cpp ~1285-1286 (per-line reset before
        // reclassification; the final value is written back further below, mirroring ~1794-1795).
        if (line < CaptureLineUses3d.size())
            CaptureLineUses3d[line] = 0;

        const u32 sourceBEvb = (captureCnt >> 8) & 0x1Fu;
        captureBlendsStructuredSourceB =
            captureMode >= 2u
            && sourceBEvb != 0u
            && structuredSourceBFromVram;
        if (captureBlendsStructuredSourceB)
        {
            const u32 sampleWidth = std::min<u32>(width, 256u);
            for (u32 i = 0; i < sampleWidth; i++)
            {
                rend2dA->ReadStructuredVulkan2DCapture2DOverlayPixel(
                    structuredSourceBVram,
                    (structuredSourceBBaseAddr + i) & 0xFFFFu,
                    structuredSourceBOverlayPixels[static_cast<size_t>(i)],
                    structuredSourceBOverlayControlAlpha[static_cast<size_t>(i)]);
            }
        }

        rend2dA->ClearStructuredVulkan2DCaptureRange(dstvram, structuredCaptureDstBase, width);

        if (captureUsesDirect3D)
        {
            if (captureDebugEnabled)
                LastDebugCaptureStats.Direct3DLines++;
            Rend3D->SetCaptureScreenSwapHint(captureScreenSwap);
            Output3D = Rend3D->GetLine(static_cast<int>(sourceLine));
            srcA = Output3D;
            captureLineUses3d = Output3D != nullptr;
        }
        else
        {
            srcA = rend2dA->BGOBJLine;
            bool needs3dComposite = false;
            const bool sourceAContributes = captureMode == 0u
                || ((captureMode >= 2u) && ((captureCnt & 0x1Fu) != 0u));
            if (sourceAContributes)
            {
                for (int i = 0; i < 256; i++)
                {
                    const u32 compmode = (rend2dA->BGOBJLine[512 + i] >> 24) & 0xF;
                    if (captureDebugEnabled && compmode < 8u)
                        LastDebugCaptureStats.CompModeCounts[compmode]++;
                    if (compmode <= 4u)
                    {
                        needs3dComposite = true;
                        break;
                    }
                }
            }

            if (needs3dComposite)
            {
                if (captureDebugEnabled)
                    LastDebugCaptureStats.SourceACompositeLines++;
                Rend3D->SetCaptureScreenSwapHint(captureScreenSwap);
                Output3D = Rend3D->GetLine(static_cast<int>(sourceLine));
            }

            if (needs3dComposite && Output3D != nullptr)
            {
                rend2dA->CopyStructuredVulkan2DCaptureSourceLineToCapture(
                    line, dstvram, structuredCaptureDstBase, width);

                u32 external3DSourceClass = 0u;
                u32 external3DSourceCounts[17] {};
                const bool allowUnclassifiedExternal3DSlot =
                    captureMode >= 2u && width == 256u && srcB != nullptr;

                for (int i = 0; i < 256; i++)
                {
                    const u32 sourceClass = StructuredVulkan2DSourceClass(rend2dA->BGOBJLine[i]);
                    if (sourceClass <= 16u)
                        external3DSourceCounts[sourceClass]++;
                }
                constexpr u32 sourceClasses[] = {1u, 2u, 4u, 8u};
                u32 bestSourceCount = 0u;
                for (u32 sourceClass : sourceClasses)
                {
                    if (external3DSourceCounts[sourceClass] > bestSourceCount)
                    {
                        bestSourceCount = external3DSourceCounts[sourceClass];
                        external3DSourceClass = sourceClass;
                    }
                }
                if (bestSourceCount < 128u)
                    external3DSourceClass = 0u;

                captureLineUses3d = true;

                for (int i = 0; i < 256; i++)
                {
                    const u32 originalVal1 = rend2dA->BGOBJLine[i];
                    const u32 originalVal2 = rend2dA->BGOBJLine[256 + i];
                    const u32 originalVal3 = rend2dA->BGOBJLine[512 + i];
                    u32 val1 = originalVal1;
                    u32 val2 = originalVal2;

                    u32 compmode = (originalVal3 >> 24) & 0xF;
                    const u32 _3dval = Output3D[i];

                    if (captureDebugEnabled && (_3dval >> 24) > 0)
                    {
                        LastDebugCaptureStats.Opaque3DSourcePixels++;
                        if ((val1 & 0xFF000000u) == 0x20000000u)
                            LastDebugCaptureStats.Opaque3DBackdropPixels++;
                    }

                    if (compmode == 4)
                    {
                        if ((_3dval >> 24) > 0)
                            val1 = ColorBlend5(_3dval, val1);
                        else
                            val1 = val2;
                    }
                    else if (compmode == 1)
                    {
                        if ((_3dval >> 24) > 0)
                        {
                            u32 eva = (originalVal3 >> 8) & 0x1F;
                            u32 evb = (originalVal3 >> 16) & 0x1F;

                            val1 = ColorBlend4(val1, _3dval, eva, evb);
                        }
                        else
                            val1 = val2;
                    }
                    else if (compmode <= 3)
                    {
                        if ((_3dval >> 24) > 0)
                        {
                            u32 evy = (originalVal3 >> 8) & 0x1F;

                            val1 = _3dval;
                            if      (compmode == 2) val1 = ColorBrightnessUp(val1, evy, 0x8);
                            else if (compmode == 3) val1 = ColorBrightnessDown(val1, evy, 0x7);
                        }
                        else
                            val1 = val2;
                    }

                    rend2dA->BGOBJLine[i] = val1;
                    rend2dA->StoreStructuredVulkan2DCapturePixel(
                        dstvram,
                        (structuredCaptureDstBase + static_cast<u32>(i)) & 0xFFFFu,
                        originalVal1,
                        originalVal2,
                        originalVal3,
                        val1,
                        val2,
                        originalVal3,
                        external3DSourceClass,
                        true,
                        (_3dval >> 24u) != 0u,
                        allowUnclassifiedExternal3DSlot);
                    structuredCaptureStoredFromSourceA = true;
                }
            }
        }

        if (captureLineUses3d && !structuredCaptureStoredFromSourceA)
            rend2dA->CopyStructuredVulkan2DCurrentLineToCapture(line, dstvram, structuredCaptureDstBase, width);

        // MELONPRIME-PORT: reference GPU2D_Soft.cpp ~1794-1795.
        if (line < CaptureLineUses3d.size())
            CaptureLineUses3d[line] = captureLineUses3d ? 1 : 0;

        if (captureLineUses3d && srcA != nullptr)
        {
            std::memcpy(
                LastDebugCapture3dSource.data() + static_cast<size_t>(line) * 256u,
                srcA,
                256u * sizeof(u32));
            HasLastDebugCapture3dSource = true;
            LastDebugCaptureStats.CaptureLineUses3dLines++;
        }
    }
#endif

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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
                if (useStructuredVulkan2D && dst[i] != 0u)
                {
                    LastDebugCaptureStats.SourceAOutputUsefulPixels++;
                    if ((dst[i] & 0x7FFFu) != 0u)
                        LastDebugCaptureStats.SourceAOutputVisiblePixels++;
                    else
                        LastDebugCaptureStats.SourceAOutputOpaqueBlackPixels++;
                }
#endif
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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
                    // MELONPRIME-PORT: reference GPU2D_Soft.cpp ~1927-1928 (structuredCaptureOutputPixels
                    // tracking, consumed by the source-B overlay merge step below). Only reachable
                    // when captureBlendsStructuredSourceB is true, which already implies srcB != nullptr
                    // (this branch), so this is a no-op write when structured mode is off/inactive.
                    if (useStructuredVulkan2D && i < 256u)
                        structuredCaptureOutputPixels[i] = static_cast<u16>(dst[i]);
#endif
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
    // MELONPRIME-PORT: reference GPU2D_Soft.cpp ~1966-1985 (source-B structured-overlay merge
    // step, protecting previously-captured 3D-composited capture-plane content from being
    // clobbered by a source-B color that's actually just a stale/faded copy of it).
    if (useStructuredVulkan2D && captureBlendsStructuredSourceB)
    {
        const u32 mergeWidth = std::min<u32>(width, 256u);
        for (u32 i = 0; i < mergeWidth; i++)
        {
            const u32 overlayPixel = structuredSourceBOverlayPixels[static_cast<size_t>(i)];
            if (overlayPixel == 0u)
                continue;

            const u32 val = overlayPixel;
            const u32 r = (val >> 1) & 0x1F;
            const u32 g = (val >> 9) & 0x1F;
            const u32 b = (val >> 17) & 0x1F;
            const u32 a = ((val >> 24) != 0) ? 0x8000u : 0u;
            const u16 overlayPacked = static_cast<u16>(r | (g << 5) | (b << 10) | a);
            const u16 outputPacked = structuredCaptureOutputPixels[static_cast<size_t>(i)];

            if (((overlayPacked ^ outputPacked) & 0x8000u) != 0u)
                continue;
            const int lhsR = overlayPacked & 0x1F, lhsG = (overlayPacked >> 5) & 0x1F, lhsB = (overlayPacked >> 10) & 0x1F;
            const int rhsR = outputPacked & 0x1F, rhsG = (outputPacked >> 5) & 0x1F, rhsB = (outputPacked >> 10) & 0x1F;
            if (std::abs(lhsR - rhsR) > 2 || std::abs(lhsG - rhsG) > 2 || std::abs(lhsB - rhsB) > 2)
                continue;

            rend2dA->MergeStructuredVulkan2DCapture2DOverlayPixel(
                dstvram,
                (structuredCaptureDstBase + i) & 0xFFFFu,
                overlayPixel,
                structuredSourceBOverlayControlAlpha[static_cast<size_t>(i)]);
        }
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


bool SoftRenderer::GetFramebuffers(void** top, void** bottom)
{
    int frontbuf = BackBuffer ^ 1;
    *top = Framebuffer[frontbuf][0];
    *bottom = Framebuffer[frontbuf][1];
    return true;
}

}
