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
#include <cassert>

#include "MelonPrimeSapphireGpu2DState.h"
#include "MelonPrimeFirstVulkanFrameTrace.h"
#endif

namespace melonDS
{

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
namespace
{
SapphireGpu2DState* GetSapphireState(GPU& gpu) noexcept
{
    return gpu.TryGetSapphireGpu2DState();
}

u32* FramebufferPlane(GPU& gpu, int buffer, int plane) noexcept
{
    return gpu.FramebufferPlane(buffer, plane);
}

int BackBufferIndex(const GPU& gpu) noexcept
{
    return gpu.BackBufferIndex();
}

size_t PackedFramebufferClearBytes(const GPU& gpu) noexcept
{
    return gpu.FramebufferPixelCount() * sizeof(u32);
}
} // namespace
#else
namespace
{
size_t PackedFramebufferClearBytes() noexcept
{
    return 256 * 192 * sizeof(u32);
}
} // namespace
#endif

SoftRenderer::SoftRenderer(melonDS::NDS& nds)
    : Renderer(nds.GPU)
{
#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
    const size_t len = 256 * 192;
    Framebuffer[0][0] = new u32[len];
    Framebuffer[0][1] = new u32[len];
    Framebuffer[1][0] = new u32[len];
    Framebuffer[1][1] = new u32[len];
    BackBuffer = 0;
#endif

    Rend3D = std::make_unique<SoftRenderer3D>(GPU.GPU3D, *this);
#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
    Rend2D_A = std::make_unique<SoftRenderer2D>(GPU.GPU2D_A, *this);
    Rend2D_B = std::make_unique<SoftRenderer2D>(GPU.GPU2D_B, *this);
#endif
}

SoftRenderer::~SoftRenderer()
{
#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
    delete[] Framebuffer[0][0];
    delete[] Framebuffer[0][1];
    delete[] Framebuffer[1][0];
    delete[] Framebuffer[1][1];
#endif
}

void SoftRenderer::Reset()
{
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    const size_t len = PackedFramebufferClearBytes(GPU);
    for (int buffer = 0; buffer < 2; ++buffer)
    {
        for (int plane = 0; plane < 2; ++plane)
        {
            if (u32* planePtr = FramebufferPlane(GPU, buffer, plane))
                memset(planePtr, 0, len);
        }
    }
#else
    const size_t len = 256 * 192 * sizeof(u32);
    memset(Framebuffer[0][0], 0, len);
    memset(Framebuffer[0][1], 0, len);
    memset(Framebuffer[1][0], 0, len);
    memset(Framebuffer[1][1], 0, len);
    Rend2D_A->Reset();
    Rend2D_B->Reset();
#endif
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    if (auto* renderer = GPU.TryGetGpu2DSoftRenderer())
        renderer->ClearStructuredVulkan2DState();
    (void)GPU.AssignFramebuffers();
#endif
    GetRenderer3D().Reset();
}

void SoftRenderer::Stop()
{
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    GetRenderer3D().StopRenderer();
    const size_t len = PackedFramebufferClearBytes(GPU);
    for (int buffer = 0; buffer < 2; ++buffer)
    {
        for (int plane = 0; plane < 2; ++plane)
        {
            if (u32* planePtr = FramebufferPlane(GPU, buffer, plane))
                memset(planePtr, 0, len);
        }
    }
#else
    const size_t len = 256 * 192 * sizeof(u32);
    memset(Framebuffer[0][0], 0, len);
    memset(Framebuffer[0][1], 0, len);
    memset(Framebuffer[1][0], 0, len);
    memset(Framebuffer[1][1], 0, len);
#endif
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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    if (auto* state = GetSapphireState(GPU);
        state != nullptr && state->IsActiveForRendering(GPU))
    {
        using MelonPrime::FirstVulkanFrameTrace::log;
        using MelonPrime::FirstVulkanFrameTrace::consumeBudget;

        log(
            "[FirstGpu2D] DrawScanline enter VCount=%u line=%u SapphireActive=1 generation=%llu GPU3DGeneration=%llu UnitA=%p UnitB=%p\n",
            GPU.VCount,
            line,
            static_cast<unsigned long long>(state->ActiveRendererGeneration()),
            static_cast<unsigned long long>(GPU.GPU3D.GetCurrentRendererGeneration()),
            static_cast<void*>(&GPU.GPU2D_A),
            static_cast<void*>(&GPU.GPU2D_B));

        log(
            "[FirstGpu2D] canonical units UnitA.Num=%u UnitB.Num=%u UnitA.DispCnt=0x%08X UnitB.DispCnt=0x%08X\n",
            GPU.GPU2D_A.Num,
            GPU.GPU2D_B.Num,
            GPU.GPU2D_A.DispCnt,
            GPU.GPU2D_B.DispCnt);

        if (!GPU.AssignFramebuffers())
        {
            log("[FirstGpu2D] bind failed Framebuffer null\n");
            consumeBudget();
            return;
        }

        const int backBuffer = BackBufferIndex(GPU);
        log(
            "[FirstGpu2D] after Bind Framebuffer0=%p Framebuffer1=%p BackBuffer=%d stride=%d\n",
            static_cast<void*>(FramebufferPlane(GPU, backBuffer, 0)),
            static_cast<void*>(FramebufferPlane(GPU, backBuffer, 1)),
            backBuffer,
            GPU.GPU3D.IsRendererAccelerated() ? 769 : 256);

        line = GPU.VCount;
        if (line < 192)
        {
            auto& renderer2D = GetSapphire2DRenderer();
            log("[FirstGpu2D] before UnitA line=%u\n", line);
            renderer2D.DrawScanline(line, &GPU.GPU2D_A);
            log("[FirstGpu2D] after UnitA line=%u\n", line);

            log("[FirstGpu2D] before UnitB line=%u\n", line);
            renderer2D.DrawScanline(line, &GPU.GPU2D_B);
            log("[FirstGpu2D] after UnitB line=%u\n", line);
        }

        consumeBudget();
        return;
    }
#endif

    u32 *dstA, *dstB;
    const u32 stride = 256u;
    u32 dstoffset = stride * line;
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    const int backBuffer = BackBufferIndex(GPU);
    if (GPU.ScreenSwap)
    {
        dstA = &FramebufferPlane(GPU, backBuffer, 0)[dstoffset];
        dstB = &FramebufferPlane(GPU, backBuffer, 1)[dstoffset];
    }
    else
    {
        dstA = &FramebufferPlane(GPU, backBuffer, 1)[dstoffset];
        dstB = &FramebufferPlane(GPU, backBuffer, 0)[dstoffset];
    }
#else
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
#endif

    // the position used for drawing operations is based on VCOUNT
    line = GPU.VCount;
    if (line < 192)
    {
        // retrieve 3D output
        Output3D = GetRenderer3D().GetLine(line);

        // draw BG/OBJ layers
#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
        Rend2D_A->DrawScanline(line);
        Rend2D_B->DrawScanline(line);
#endif

        // draw the final screen output
        DrawScanlineA(line, dstA);
        DrawScanlineB(line, dstB);

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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
SapphirePhysical2DScreenView SoftRenderer::BuildPhysicalScreenView(
    int frontBuffer,
    SapphirePhysicalScreen screen) const noexcept
{
    SapphirePhysical2DScreenView view{};
    const bool top = screen == SapphirePhysicalScreen::Top;

    // Physical top/bottom LCD content always lands in framebuffer slot 1/0
    // regardless of GPU.ScreenSwap (swap only retargets engines to slots).
    view.packed = FramebufferPlane(GPU, frontBuffer, top ? 1u : 0u);
    view.plane0 = GetSapphire2DRenderer().GetStructuredVulkan2DPlane(top, 0);
    view.plane1 = GetSapphire2DRenderer().GetStructuredVulkan2DPlane(top, 1);
    view.control = GetSapphire2DRenderer().GetStructuredVulkan2DPlane(top, 2);
    view.engine = top
        ? (GPU.ScreenSwap ? 1u : 0u)
        : (GPU.ScreenSwap ? 0u : 1u);

    return view;
}

void SoftRenderer::SwapBuffers()
{
    GPU.FrontBuffer = GPU.FrontBuffer ? 0 : 1;
    (void)GPU.AssignFramebuffers();
}

bool SoftRenderer::PublishSapphire2DFrame() noexcept
{
    auto* state = GetSapphireState(GPU);
    if (state == nullptr || !state->IsActiveForRendering(GPU))
        return false;
    if (GPU.VulkanFrameSerial == 0)
        return false;

    SapphirePublished2DFrame published{};
    published.frontBuffer = GPU.FrontBuffer;
    published.hardwareScreenSwap = GPU.ScreenSwap;
    published.renderScreenSwapAt3D = GPU.GPU3D.RenderScreenSwapAt3D;
    published.emulatedFrameSerial = GPU.VulkanFrameSerial;
    published.publicationGeneration = GPU.Published2DFrame.publicationGeneration + 1;

    if (published.frontBuffer < 0 || published.frontBuffer > 1)
        return false;

    const SapphirePhysical2DScreenView topView =
        BuildPhysicalScreenView(published.frontBuffer, SapphirePhysicalScreen::Top);
    const SapphirePhysical2DScreenView bottomView =
        BuildPhysicalScreenView(published.frontBuffer, SapphirePhysicalScreen::Bottom);

    published.top.packed = topView.packed;
    published.bottom.packed = bottomView.packed;
    if (published.top.packed == nullptr || published.bottom.packed == nullptr)
        return false;

    published.top.physicalScreen = SapphirePhysicalScreen::Top;
    published.bottom.physicalScreen = SapphirePhysicalScreen::Bottom;
    published.top.engine = topView.engine;
    published.bottom.engine = bottomView.engine;

    const bool structuredReady =
        GPU.GPU3D.HasCurrentRenderer()
        && GPU.GPU3D.GetCurrentRenderer().UsesStructured2DMetadata();

    if (structuredReady)
    {
        published.top.structuredPlane0 = topView.plane0;
        published.top.structuredPlane1 = topView.plane1;
        published.top.structuredControl = topView.control;
        published.bottom.structuredPlane0 = bottomView.plane0;
        published.bottom.structuredPlane1 = bottomView.plane1;
        published.bottom.structuredControl = bottomView.control;

        if (published.top.structuredPlane0 == nullptr
            || published.top.structuredPlane1 == nullptr
            || published.top.structuredControl == nullptr
            || published.bottom.structuredPlane0 == nullptr
            || published.bottom.structuredPlane1 == nullptr
            || published.bottom.structuredControl == nullptr)
        {
            return false;
        }
    }

#ifndef NDEBUG
    assert(GPU.GPU3D.HasCurrentRenderer()
        || published.top.structuredControl == nullptr);
    assert(GPU.GPU3D.HasCurrentRenderer()
        || published.bottom.structuredControl == nullptr);
    if (published.top.structuredControl != nullptr)
        assert(published.top.engine == (GPU.ScreenSwap ? 1u : 0u));
    if (published.bottom.structuredControl != nullptr)
        assert(published.bottom.engine == (GPU.ScreenSwap ? 0u : 1u));
#endif

    GPU.Published2DFrame = published;
    GPU.Published2DFrame.valid = true;
    GPU.Published2DFrame.frameSerial = GPU.VulkanFrameSerial;
    if (GPU.GPU3D.HasCurrentRenderer())
        GPU.Published2DFrame.rendererGeneration = GPU.GPU3D.GetCurrentRendererGeneration();

#ifndef NDEBUG
    static int publishLogBudget = 120;
    if (publishLogBudget > 0)
    {
        --publishLogBudget;
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Debug,
            "[Sapphire2DPublish] reason=finishFrame frontBuffer=%d hardwareSwap=%d "
            "render3DSwap=%d frameSerial=%llu rendererPresent=%d structured=%d generation=%llu\n",
            published.frontBuffer,
            published.hardwareScreenSwap ? 1 : 0,
            published.renderScreenSwapAt3D ? 1 : 0,
            static_cast<unsigned long long>(GPU.Published2DFrame.frameSerial),
            GPU.GPU3D.HasCurrentRenderer() ? 1 : 0,
            structuredReady ? 1 : 0,
            static_cast<unsigned long long>(GPU.Published2DFrame.publicationGeneration));
    }
#endif

    return true;
}

SapphireGPU2DCore::GPU2D::SoftRenderer& SoftRenderer::GetSapphire2DRenderer() noexcept
{
    auto* renderer = GPU.TryGetGpu2DSoftRenderer();
    assert(renderer != nullptr);
    return *renderer;
}

const SapphireGPU2DCore::GPU2D::SoftRenderer& SoftRenderer::GetSapphire2DRenderer() const noexcept
{
    const auto* renderer = GPU.TryGetGpu2DSoftRenderer();
    assert(renderer != nullptr);
    return *renderer;
}
#endif

void SoftRenderer::DrawSprites(u32 line)
{
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    if (auto* state = GetSapphireState(GPU);
        state != nullptr && state->IsActiveForRendering(GPU))
    {
        (void)GPU.AssignFramebuffers();
        auto& renderer2D = GetSapphire2DRenderer();
        renderer2D.DrawSprites(line, &GPU.GPU2D_A);
        renderer2D.DrawSprites(line, &GPU.GPU2D_B);
        return;
    }
#endif

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
    Rend2D_A->DrawSprites(line);
    Rend2D_B->DrawSprites(line);
#endif
}

void SoftRenderer::VBlank()
{
}

void SoftRenderer::VBlankEnd()
{
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
    const u32 captureCnt = GPU.CaptureCnt;

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

    u16* srcB = nullptr;
    if (captureCnt & (1<<25))
        srcB = GPU.DispFIFOBuffer;
    else
    {
        const u32 dispcnt = GPU.GPU2D_A.DispCnt;
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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    const int frontbuf = GPU.FrontBuffer;
    *top = FramebufferPlane(GPU, frontbuf, 0);
    *bottom = FramebufferPlane(GPU, frontbuf, 1);
#else
    int frontbuf = BackBuffer ^ 1;
    *top = Framebuffer[frontbuf][0];
    *bottom = Framebuffer[frontbuf][1];
#endif
    return true;
}

}
