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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#endif

namespace melonDS
{
class SoftRenderer;

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
// MELONPRIME-PC-ADAPT: Sapphire has one CurUnit-switching 2D renderer, so its structured
// output/capture arrays are inherently shared by engines A and B. This fork has one renderer
// object per engine. These small wrappers retain the original field-style access while allowing
// the two PC renderer objects to point at the same storage.
template<typename T, size_t N>
class SharedStructuredVulkanArray
{
public:
    SharedStructuredVulkanArray()
        : Storage(std::make_shared<std::array<T, N>>())
    {
    }

    T* data() noexcept { return Storage->data(); }
    const T* data() const noexcept { return Storage->data(); }
    void fill(const T& value) { Storage->fill(value); }
    T& operator[](size_t index) noexcept { return (*Storage)[index]; }
    const T& operator[](size_t index) const noexcept { return (*Storage)[index]; }

private:
    std::shared_ptr<std::array<T, N>> Storage;
};

template<typename T>
class SharedStructuredVulkanValue
{
public:
    SharedStructuredVulkanValue()
        : Storage(std::make_shared<T>())
    {
    }

    operator T() const noexcept { return *Storage; }
    SharedStructuredVulkanValue& operator=(const T& value) noexcept
    {
        *Storage = value;
        return *this;
    }

private:
    std::shared_ptr<T> Storage;
};
#endif

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
    // MELONPRIME-PORT: reference-generation (Sapphire) GPU2D::SoftRenderer public structured-
    // Vulkan-2D accessor surface (GPU2D_Soft.h ~74-75). The per-pixel producer is wired below and
    // the PC frontend consumes these planes through StructuredVulkan2DRendererBridge.
    [[nodiscard]] const u32* GetStructuredVulkan2DPlane(bool topScreen, u32 plane) const noexcept;
    void ClearStructuredVulkan2DState() noexcept;
#endif

private:
    SoftRenderer& Parent;

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // MELONPRIME-PORT-ADAPT: SoftRenderer::DoCapture (GPU_Soft.cpp) needs to reach the engine-A
    // instance's structured-Vulkan-2D private surface (CopyStructuredVulkan2DCaptureLineToCurrentScreen
    // for VRAM display mode, and the Store/Copy/Read/Merge capture family for the structured
    // DoCapture branch) the same way reference's single CurUnit-switching SoftRenderer reaches
    // its own members directly. Reference has no equivalent friend declaration because producer
    // and capture logic live on one class there; our split requires one.
    friend class SoftRenderer;

    // MELONPRIME-PORT: reference-generation structured-Vulkan-2D constants (GPU2D_Soft.h
    // ~77-81). MELONPRIME-PORT-ADAPT: reference's single SoftRenderer instance switches
    // CurUnit between both GPU2D engines and stores both screens' data in one shared object;
    // our fork already splits SoftRenderer2D one-per-engine (see ctor), so each instance keeps
    // its own copy of both screen slots (kStructuredScreenCount unchanged at 2) to preserve the
    // reference method bodies byte-for-byte instead of collapsing the screen dimension. The
    // storage wrappers below are shared between both instances after construction, recreating
    // Sapphire's single-renderer ownership rather than leaving capture history engine-local.
    static constexpr size_t kStructuredScreenWidth = 256;
    static constexpr size_t kStructuredScreenHeight = 192;
    static constexpr size_t kStructuredPixelCount = kStructuredScreenWidth * kStructuredScreenHeight;
    static constexpr size_t kStructuredPlaneCount = 3;
    static constexpr size_t kStructuredScreenCount = 2;

    // MELONPRIME-PORT: reference-generation structured-Vulkan-2D private method surface
    // (GPU2D_Soft.h ~164-215), ported verbatim (bodies in GPU2D_Soft.cpp) with CurUnit->
    // adapted to GPU2D./GPU. (this SoftRenderer2D is permanently bound to one engine, unlike
    // reference's CurUnit-switching single instance). DrawPixel_Normal/Accel, PushRawPixel_Accel,
    // and TryDrawStructuredVulkan2DCapturePixel are wired into the BG/OBJ and capture paths below.
    [[nodiscard]] bool UseStructuredVulkan2D() const noexcept;
    void ClearStructuredVulkan2DLine(u32 line);
    void ClearStructuredVulkan2DCapture(u32 vramBank);
    void ClearStructuredVulkan2DCaptureRange(u32 vramBank, u32 dstAddress, u32 width);
    void SaveStructuredVulkan2DCaptureSourceLine(u32 line);
    void CopyStructuredVulkan2DCaptureSourceLineToCapture(u32 line, u32 vramBank, u32 dstAddress, u32 width);
    void CopyStructuredVulkan2DCurrentLineToCapture(u32 line, u32 vramBank, u32 dstAddress, u32 width);
    void CopyStructuredVulkan2DCaptureLineToCurrentScreen(u32 line, u32 vramBank);
    bool ReadStructuredVulkan2DCapture2DOverlayPixel(
        u32 vramBank,
        u32 vramAddress,
        u32& overlayPixel,
        u32& overlayControlAlpha) const noexcept;
    void MergeStructuredVulkan2DCapture2DOverlayPixel(
        u32 vramBank,
        u32 vramAddress,
        u32 overlayPixel,
        u32 overlayControlAlpha);
    // MELONPRIME-PORT-ADAPT: reference derives top/bottom placement from a Framebuffer-pointer
    // comparison specific to its accelerated-stride DrawScanline, which has no equivalent here;
    // adapted to read GPU.ScreenSwap, the same signal SoftRenderer::DrawScanline already uses
    // (see GPU_Soft.cpp DrawScanlineA/B) to map engines to physical screen position.
    [[nodiscard]] bool CurrentUnitTargetsTopScreen() const noexcept;
    void StoreStructuredVulkan2DPixel(
        u32 line,
        u32 x,
        u32 originalVal1,
        u32 originalVal2,
        u32 originalVal3,
        u32 legacyVal1,
        u32 legacyVal2,
        u32 legacyControl,
        u32 captureBacked3DSourceClass);
    void StoreStructuredVulkan2DCapturePixel(
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
        bool allowUnclassifiedExternal3DSlot);
    static void DrawPixel_Normal(u32* dst, u16 color, u32 flag);
    static void DrawPixel_Accel(u32* dst, u16 color, u32 flag);
    static void PushRawPixel_Accel(u32* dst, u32 value);
    bool TryDrawStructuredVulkan2DCapturePixel(u32* dst, u32 flatByteAddress);
    void ShareStructuredVulkan2DStateFrom(const SoftRenderer2D& source) noexcept;

    // MELONPRIME-PORT: reference-generation structured-Vulkan-2D per-pixel producer wiring.
    // Reference threads a `DrawPixel drawPixel`
    // function-pointer template parameter through DrawBG_Text/Affine/Extended/Large and
    // InterleaveSprites so BGOBJLine can hold either 2 planes (legacy DrawPixel/DrawPixel_Normal)
    // or 3 planes (DrawPixel_Accel, deferring 3D blending to the consumer) depending on
    // GPU.GPU3D.IsRendererAccelerated(). MELONPRIME-PORT-ADAPT: our fork has no
    // IsRendererAccelerated() concept (Renderer3D::GetLine always returns a fully composited
    // line -- see GPU3D.h) and the existing DrawBG_*/InterleaveSprites methods above are already
    // a from-upstream-diverged, non-templated 2-plane implementation (different mosaic-line
    // field names, different bit-test style) that must stay byte-identical for the Vulkan-OFF
    // build. Rather than retrofitting a second template parameter onto those (which would touch
    // unguarded declaration text), each gets a dedicated "_Structured" sibling below: a
    // byte-for-byte duplicate of the corresponding method above (same field accesses, same
    // control flow) with only the leaf DrawPixel(...) call sites parameterized via drawPixel.
    // These are called only when UseStructuredVulkan2D() is true (see DoDrawBG*/DoInterleaveSprites
    // macro changes and DrawBG_3D dispatch in GPU2D_Soft.cpp); the plain methods above remain the
    // Vulkan-off / non-structured path and are untouched.
    using StructuredDrawPixelFn = void (*)(u32* dst, u16 color, u32 flag);
    template<bool mosaic, StructuredDrawPixelFn drawPixel> void DrawBG_Text_Structured(u32 line, u32 bgnum);
    template<bool mosaic, StructuredDrawPixelFn drawPixel> void DrawBG_Affine_Structured(u32 line, u32 bgnum);
    template<bool mosaic, StructuredDrawPixelFn drawPixel> void DrawBG_Extended_Structured(u32 line, u32 bgnum);
    template<bool mosaic, StructuredDrawPixelFn drawPixel> void DrawBG_Large_Structured(u32 line);
    // reference GPU2D_Soft.cpp ~2784-2815 (IsRendererAccelerated() branch only; the non-
    // accelerated branch is exactly today's DrawBG_3D() above). MELONPRIME-PORT-ADAPT: drops the
    // Renderer2DDebugShouldDraw3DBg(...) debug-tool gate (no equivalent debug-tools
    // infrastructure in this fork; see other omissions in this file).
    void DrawBG_3D_Structured();
    template<StructuredDrawPixelFn drawPixel> void InterleaveSprites_Structured(u32 prio);

    // reference GPU2D_Soft.cpp ~1004/2770 (CurrentLineRegularCaptureUses3d). MELONPRIME-PORT-ADAPT:
    // reference's field lives on the single CurUnit-switching SoftRenderer and is read back
    // within the SAME DrawScanline call (dst[256*3] renderer-metadata word); our fork has no
    // equivalent per-line metadata channel on SoftRenderer::Framebuffer (see GPU_Soft.h --
    // Framebuffer is a plain 256x192 word buffer, not reference's accelerated 256*3+1 stride
    // with a trailing metadata word), so that consumer is out of this port's reach without a
    // Framebuffer/GPU-level restructuring outside this file's scope. The field itself is kept
    // (reset per-line, set by TryDrawStructuredVulkan2DCapturePixel below) since it costs nothing
    // and preserves the reference control-flow shape for any future metadata-channel port.
    bool CurrentLineRegularCaptureUses3d = false;
#endif

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

    // MELONPRIME-PORT-ADAPT: reference (GPU2D_Soft.h ~84) always sizes BGOBJLine at 256*3 (a
    // legacy/below plane, an above/3D-adjacent plane, and a compmode/control-flags plane) to
    // support its accelerated-GL 2D+3D compositing scheme; our fork's non-Vulkan path only ever
    // used the first 2 planes (see DrawPixel above). Kept at 256*2 for the Vulkan-off build
    // (byte-identical to baseline) and widened to 256*3 whenever MELONPRIME_ENABLE_VULKAN is
    // compiled in, matching reference's fixed layout, so the *_Structured producer methods below
    // have a third plane to write into even when UseStructuredVulkan2D() is false at runtime.
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // MELONPRIME-PORT: reference-generation structured-Vulkan-2D state fields (GPU2D_Soft.h
    // ~238-244), ported verbatim (same element counts/layout; see kStructuredScreenCount note
    // above for why the screen dimension is preserved rather than collapsed).
    SharedStructuredVulkanArray<
        u32, kStructuredScreenCount * kStructuredPlaneCount * kStructuredPixelCount> StructuredVulkan2DPlanes;
    SharedStructuredVulkanArray<
        u32, kStructuredPlaneCount * kStructuredScreenWidth> StructuredVulkan2DCaptureSourceLine;
    SharedStructuredVulkanValue<bool> StructuredVulkan2DCaptureSourceLineValid;
    SharedStructuredVulkanValue<u32> StructuredVulkan2DCaptureSourceLineY;
    bool StructuredVulkan2DCurrentLineTargetsTop = false;
    SharedStructuredVulkanArray<
        u32, 4 * kStructuredPlaneCount * kStructuredPixelCount> StructuredVulkan2DCapturePlanes;
    SharedStructuredVulkanArray<u8, 4 * kStructuredScreenHeight> StructuredVulkan2DCaptureLineValid;
#endif

    u32 ColorComposite(int i, u32 val1, u32 val2) const;

    template<u32 bgmode> void DrawScanlineBGMode(u32 line);
    void DrawScanlineBGMode6(u32 line);
    void DrawScanlineBGMode7(u32 line);
    void DrawScanline_BGOBJ(u32 line, u32* dst);

    static void DrawPixel(u32* dst, u16 color, u32 flag);

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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
// MELONPRIME-PORT-ADAPT: reference `GPU::GetRenderer2D()` (melonDS-android-lib src/GPU.h:76-77)
// returns one shared `GPU2D::Renderer2D&` object (its single CurUnit-switching SoftRenderer
// instance) whose `GetStructuredVulkan2DPlane(topScreen, plane)`/`ClearStructuredVulkan2DState()`/
// `GetDebugCaptureStats()` the Vulkan latch pipeline (MelonInstanceVulkan.cpp) calls for BOTH
// topScreen=true and topScreen=false from that SAME object. Our fork splits 2D rendering into two
// independent per-engine renderers (SoftRenderer2D Rend2D_A/Rend2D_B on the owning GPU-level
// SoftRenderer, see GPU_Soft.h) -- neither one alone has valid data for both screens (each only
// ever writes the screen slot it currently targets, see SoftRenderer2D::CurrentUnitTargetsTopScreen()
// above). This bridge is the call-site substitute for reference's `GPU2D::SoftRenderer` type
// (impossible to name identically here: GPU2D is a concrete per-engine-unit class in this fork, not
// a namespace). It holds both per-engine renderers and, as each scanline is produced, copies the
// owning engine's row into a physical-top/physical-bottom plane set paired with this fork's
// existing front/back accelerated framebuffer. That reproduces the reference framebuffer
// semantics and avoids re-inferring ownership after the frame has completed. The two producer
// renderers continue to use the live GPU.ScreenSwap while drawing.
// Constructed/owned by SoftRenderer (see GetRenderer2D() there); method bodies in GPU2D_Soft.cpp
// (needs melonDS::GPU complete for the screen-ownership state, not available in this header -- see
// the class-level comment in GPU2D.h for why GPU.h isn't included here).
class StructuredVulkan2DRendererBridge
{
public:
    // MELONPRIME-PC-ADAPT: reference `GPU2D::SoftRenderer::DebugCaptureStats`
    // (GPU2D_Soft.h ~35-63), field-for-field. The owning GPU-level SoftRenderer stores and updates
    // the shared state because this fork splits the two 2D engines across separate renderer objects.
    struct DebugCaptureStats
    {
        u32 CaptureLines = 0;
        u32 CaptureWidth = 0;
        u32 CaptureMode = 0;
        u32 CaptureBit24 = 0;
        u32 Direct3DLines = 0;
        u32 SourceACompositeLines = 0;
        u32 CaptureLineUses3dLines = 0;
        u32 CaptureLineUsefulAlphaLines = 0;
        u32 CaptureDestinationBlankLines = 0;
        u32 Opaque3DSourcePixels = 0;
        u32 Opaque3DBackdropPixels = 0;
        u32 SourceAOutputUsefulPixels = 0;
        u32 SourceAOutputVisiblePixels = 0;
        u32 SourceAOutputOpaqueBlackPixels = 0;
        u32 StructuredCopyLines = 0;
        u32 StructuredCopyPlane0UsefulPixels = 0;
        u32 StructuredCopyPlane1UsefulPixels = 0;
        u32 StructuredCopySlotPixels = 0;
        u32 StructuredCopyAbovePixels = 0;
        u32 StructuredCopy2DOnlyPixels = 0;
        u32 StructuredCopySourceBOverlayPixels = 0;
        u32 CaptureBacked3DLines = 0;
        u32 CaptureBacked3DNoBestClassLines = 0;
        u32 CaptureBacked3DExplicitSlotLines = 0;
        u32 CaptureBacked3DBestClassCounts[17] {};
        u32 CompModeCounts[8] {};
    };

    StructuredVulkan2DRendererBridge(
        SoftRenderer2D& rend2dA, SoftRenderer2D& rend2dB,
        const std::array<u8, 192>& captureLineUses3dMask,
        const DebugCaptureStats& debugCaptureStats,
        const std::array<u32, 256 * 192>& debugCapture3dSource,
        const bool& hasDebugCapture3dSource) noexcept
        : Rend2D_A(rend2dA)
        , Rend2D_B(rend2dB)
        , CaptureLineUses3dMask(captureLineUses3dMask)
        , LastDebugCaptureStats(debugCaptureStats)
        , LastDebugCapture3dSource(debugCapture3dSource)
        , HasLastDebugCapture3dSource(hasDebugCapture3dSource)
    {}

    [[nodiscard]] const u32* GetStructuredVulkan2DPlane(
        int buffer, bool topScreen, u32 plane) const noexcept;
    void LatchStructuredVulkan2DLine(
        int buffer, u32 outputLine, u32 sourceLine, bool engineATargetsTop) noexcept;
    void ClearStructuredVulkan2DState() noexcept;
    [[nodiscard]] const DebugCaptureStats& GetDebugCaptureStats() const noexcept { return LastDebugCaptureStats; }

    [[nodiscard]] const u32* GetDebugCapture3dSource() const noexcept
    {
        return HasLastDebugCapture3dSource ? LastDebugCapture3dSource.data() : nullptr;
    }

    // MELONPRIME-PC-ADAPT: reference `GetDebugCaptureLineUses3dMask()` (GPU2D_Soft.h ~73) returns
    // its own per-line "did this capture use 3D" bitmask (`CaptureLineUses3d`) -- NOT debug-only in
    // our fork: it's the same real field SoftRenderer::CaptureLineUses3d already populates during
    // DoCapture's structured branch (see GPU_Soft.cpp) and this port's own accel-Framebuffer
    // packing (PackAccelFramebufferSlot) already reads. Bound by reference at construction (see
    // ctor) since this bridge has no direct access to the owning SoftRenderer's private members.
    [[nodiscard]] const std::array<u8, 192>& GetDebugCaptureLineUses3dMask() const noexcept
    {
        return CaptureLineUses3dMask;
    }

private:
    static constexpr size_t kStructuredScreenWidth = 256;
    static constexpr size_t kStructuredScreenHeight = 192;
    static constexpr size_t kStructuredPixelCount =
        kStructuredScreenWidth * kStructuredScreenHeight;
    static constexpr size_t kStructuredPlaneCount = 3;
    static constexpr size_t kStructuredScreenCount = 2;
    static constexpr size_t kStructuredBufferCount = 2;

    SoftRenderer2D& Rend2D_A;
    SoftRenderer2D& Rend2D_B;
    alignas(8) std::array<
        u32,
        kStructuredBufferCount * kStructuredScreenCount
            * kStructuredPlaneCount * kStructuredPixelCount>
        PhysicalStructuredVulkan2DPlanes {};
    const std::array<u8, 192>& CaptureLineUses3dMask;
    const DebugCaptureStats& LastDebugCaptureStats;
    const std::array<u32, 256 * 192>& LastDebugCapture3dSource;
    const bool& HasLastDebugCapture3dSource;
};
#endif

}
