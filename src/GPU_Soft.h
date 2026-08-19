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
#include "GPU2DNative.h"
#include "GPU2D_Soft.h"
#include "GPU3D_Soft.h"

#ifdef MELONPRIME_DS
// Defines MELONPRIME_HAS_STRUCTURED_SOFT_2D, shared with the GPU2D_Soft
// producer side.
#include "MelonPrimeStructuredComposition.h"
#include "MelonPrimeStructuredPerf.h"
#endif

namespace melonDS
{

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

    void AllocCapture(u32 bank, u32 start, u32 len) override;
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override;
    void InvalidateVRAMCapture(u32 bank, u32 start, u32 len) override;

    bool GetFramebuffers(void** top, void** bottom) override;

    // Native GPU2D validation consumes the same native logical words that the
    // software engine produced before display-mode/master-brightness handling.
    // This is intentionally not a Qt/presenter image and is never used as a
    // hidden fallback for Vulkan or DX12 output.
    [[nodiscard]] const u32* GetSoftwareLogicalFrame(u32 engine) const noexcept
    {
        if (engine >= 2u)
            return nullptr;
        return SoftwareLogicalFrame.data()
            + static_cast<std::size_t>(engine)
                * GPU2DNative::ScreenPixelCount;
    }

    // Final LCD pixels after display mode, routing, master brightness, and
    // the screens-enabled gate. Values are canonical r6g6b6 words and are
    // used only as the exact differential oracle for a native backend.
    [[nodiscard]] const u32* GetSoftwareScreenFrame(u32 screen) const noexcept
    {
        if (screen >= 2u)
            return nullptr;
        return SoftwareScreenFrame.data()
            + static_cast<std::size_t>(screen)
                * GPU2DNative::ScreenPixelCount;
    }

    [[nodiscard]] const GPU2DNative::FrameInput& GetNativeGPU2DFrame() const noexcept
    {
        return NativeGPU2DFrame.GetFrame();
    }
    [[nodiscard]] bool HasNativeGPU2DFrame() const noexcept
    {
        return NativeGPU2DFrame.IsValid();
    }

protected:
    [[nodiscard]] const u32* GetSoftwareCaptureSourceLine(bool source3D) const noexcept
    {
        return source3D ? Output3D : Output2D[0];
    }

#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
public:
    [[nodiscard]] u32 GetCaptureTextureReference(
        u32 bank, u32 address) const noexcept override;

    struct StructuredVulkanFrameView
    {
        const u32* Plane[2][StructuredComposition::kPlaneCount]{};
        const u32* CaptureSourcePlane[StructuredComposition::kPlaneCount]{};
        const u32* CaptureSourceBNative = nullptr;
        const u32* CaptureSourceBReference = nullptr;
        const u32* CaptureCommands = nullptr;
        const u32* LineMeta[2]{};
        StructuredComposition::ScreenRoutingView ScreenRouting{};
        bool NativeMenuHeld = false;
        bool Valid = false;
        u64 ScreenRouteCopyBytes = 0;
        u64 ScreenRouteCopyNanoseconds = 0;
        u32 StructuredRegularLines = 0;
        u32 StructuredFallbackLines = 0;
        u64 Generation = 0;
        StructuredComposition::GenerationState ContentGeneration{};
    };

    [[nodiscard]] bool GetStructuredVulkanFrame(StructuredVulkanFrameView& view) const noexcept;
    [[nodiscard]] StructuredPerfBackend GetStructured2DPerfBackendForFrame() const noexcept
    {
        return StructuredPerfBackendForFrame;
    }
    // Published to the frontend so the Custom HUD can tell when MPH's native
    // START menu is held. It never selects composition behaviour.
    void SetNativeMenuHeldForFrame(bool held) noexcept
    {
        NativeMenuHeldForFrame = held;
    }
#endif

private:
    friend class SoftRenderer2D;
    friend class SoftRenderer3D;

    u32* Framebuffer[2][2];

    u32* Output3D;
    alignas(8) u32 Output2D[2][256];
    std::array<u32, 2u * GPU2DNative::ScreenPixelCount> SoftwareLogicalFrame{};
    std::array<u32, 2u * GPU2DNative::ScreenPixelCount> SoftwareScreenFrame{};
    GPU2DNative::FrameRecorder NativeGPU2DFrame;

#if defined(MELONPRIME_HAS_STRUCTURED_SOFT_2D)
    static constexpr std::size_t StructuredPixelCount = 256u * 192u;
    static constexpr std::size_t StructuredCapturePixelCount = 256u * 256u;
    static constexpr std::size_t StructuredCaptureLineCount = 256u;
    std::array<u32, 2u * StructuredComposition::kPlaneCount * StructuredPixelCount> StructuredEnginePlanes{};
    std::array<u32, 2u * StructuredComposition::kPlaneCount * StructuredPixelCount> StructuredScreenPlanes{};
    std::array<u8, 2u * StructuredComposition::kScreenHeight> StructuredScreenSource{};
    std::array<u32, 2u * 192u> StructuredScreenLineMeta{};
    std::array<u32, 4u * 3u * StructuredCapturePixelCount> StructuredCapturePlanes{};
    std::array<u8, 4u * StructuredCaptureLineCount> StructuredCaptureLineValid{};
    std::array<u8, 4u * StructuredCapturePixelCount> StructuredCapturePixelValid{};
    std::array<u8, 4u * StructuredCapturePixelCount> StructuredCapturePixelVersion{};
    std::array<u8, 4u> StructuredCaptureBankVersion{};
    std::array<u8, 4u> StructuredCaptureBankWrittenThisFrame{};
    std::array<u8, 192u> StructuredCaptureCommandWrittenThisFrame{};
    std::array<u16, 192u> StructuredCaptureSourceBWidthThisFrame{};
    std::array<u32, StructuredPixelCount> StructuredCaptureSourceBNative{};
    std::array<u32, StructuredPixelCount> StructuredCaptureSourceBReference{};
    std::array<u32, 192u * StructuredComposition::kCaptureCommandWords> StructuredCaptureCommands{};
    alignas(8) u32 Structured3DPlaceholderLine[256]{};
    alignas(8) u32 StructuredCaptureCompositeLine[256]{};
    bool StructuredFrameValid = false;
    bool StructuredCaptureCompositeLineValid = false;
    bool StructuredCapturePreparedThisFrame = false;
    bool StructuredCapture3DValid = false;
    bool StructuredFrameNativeMenuHeld = false;
    bool NativeMenuHeldForFrame = false;
    u64 StructuredScreenRouteCopyBytes = 0;
    u64 StructuredScreenRouteCopyNanoseconds = 0;
    u32 StructuredRegularLines = 0;
    u32 StructuredFallbackLines = 0;
    u64 StructuredFrameGeneration = 0;
    StructuredComposition::GenerationState StructuredContentGeneration{};
    u16 StructuredPendingPlaneDirtyMask = 0;
    u8 StructuredPendingLineMetaDirtyMask = 0;
    bool StructuredPendingCaptureCommandsDirty = false;
    u8 StructuredEngineChangedMask[2] = { 0, 0 };
    StructuredPerfBackend StructuredPerfBackendForFrame = StructuredPerfBackend::None;

    [[nodiscard]] bool UseStructuredVulkan2D() const noexcept;
    inline void MarkStructuredPlaneDirty(u32 plane) noexcept
    {
        if (plane < StructuredComposition::kStructuredInputPlaneCount)
        {
            // Generation publication is deferred until the frame boundary.
            // The producer can touch a plane thousands of times per frame;
            // only the logical plane unit needs one generation commit.
            StructuredPendingPlaneDirtyMask |= static_cast<u16>(1u << plane);
        }
    }
    inline void MarkStructuredScreenPlanesDirty() noexcept
    {
        StructuredPendingPlaneDirtyMask |= 0x00FFu;
    }
    inline void MarkStructuredLineMetaDirty(u32 screen) noexcept
    {
        if (screen < StructuredComposition::kStructuredInputLineMetaCount)
            StructuredPendingLineMetaDirtyMask |= static_cast<u8>(1u << screen);
    }
    inline void MarkStructuredCaptureCommandsDirty() noexcept
    {
        StructuredPendingCaptureCommandsDirty = true;
    }
    inline void StoreStructuredScreenSource(u32 screen, u32 line, u8 value) noexcept
    {
        if (screen >= 2u || line >= StructuredComposition::kScreenHeight)
            return;
        u8& destination = StructuredScreenSource[
            static_cast<std::size_t>(screen) * StructuredComposition::kScreenHeight + line];
        if (destination == value)
            return;
        destination = value;
        MarkStructuredScreenPlanesDirty();
    }
    inline void StoreStructuredScreenPlaneWord(
        u32 screen, u32 plane, std::size_t pixelIndex, u32 value,
        u8* changedMask = nullptr) noexcept
    {
        if (screen >= 2u || plane >= StructuredComposition::kPlaneCount
            || pixelIndex >= StructuredPixelCount)
        {
            return;
        }
        u32& destination = StructuredScreenPlanes[
            static_cast<std::size_t>(screen) * StructuredComposition::kPlaneCount
                * StructuredPixelCount
            + static_cast<std::size_t>(plane) * StructuredPixelCount
            + pixelIndex];
        if (destination == value)
            return;
        destination = value;
        if (changedMask)
            *changedMask |= static_cast<u8>(1u << plane);
        else
            MarkStructuredPlaneDirty(screen * StructuredComposition::kPlaneCount + plane);
    }
    inline void StoreStructuredCaptureSourceWord(
        u32 plane, u32& destination, u32 value) noexcept
    {
        if (destination == value)
            return;
        destination = value;
        MarkStructuredPlaneDirty(plane);
    }
    inline void StoreStructuredCaptureCommandWord(u32& destination, u32 value) noexcept
    {
        if (destination == value)
            return;
        destination = value;
        MarkStructuredCaptureCommandsDirty();
    }
    inline void StoreStructuredEnginePixel(
        u32 engine,
        u32 line,
        u32 x,
        u32 val1,
        u32 val2,
        u32 composed,
        u32 compositionMode,
        u32 eva,
        u32 evb,
        u32 reference1 = 0,
        u32 reference2 = 0)
    {
        namespace Contract = StructuredComposition;
        if (engine >= 2u || line >= 192u || x >= 256u)
            return;

        const std::size_t pixelIndex = static_cast<std::size_t>(line) * 256u + x;
        const std::size_t engineBase = static_cast<std::size_t>(engine)
            * Contract::kPlaneCount * StructuredPixelCount;
        u32 plane0 = composed;
        u32 plane1 = 0;
        u32 captureReference = 0;
        u32 controlAlpha = Contract::kControlPlain2D;
        // These are the software 2D engine's BG/OBJ flags, not structured
        // control bits. 0x40 identifies the 3D layer and 0x80 distinguishes a
        // blend-flagged sprite that only resembles it.
        const u32 alpha1 = val1 >> 24u;
        const u32 alpha2 = val2 >> 24u;
        const bool val1Is3D = (alpha1 & 0x40u) != 0u && (alpha1 & 0x80u) == 0u;
        const bool val2Is3D = (alpha2 & 0x40u) != 0u && (alpha2 & 0x80u) == 0u;

        const bool val1IsCapture = reference1 != 0u;
        const bool val2IsCapture = reference2 != 0u;

        if (val1Is3D || val1IsCapture)
        {
            plane0 = val2;
            captureReference = reference1;
            controlAlpha = Contract::kControlHas3DSlot
                | (compositionMode & Contract::kControlCompositionModeMask);
            if ((plane0 & 0x00FFFFFFu) == 0 && (plane0 >> 24u) != 0)
                controlAlpha |= Contract::kControlOpaqueBlackBelow;
        }
        else if ((val2Is3D || val2IsCapture) && compositionMode == Contract::kCompositionModeBlend4)
        {
            plane0 = 0;
            plane1 = val1;
            controlAlpha = Contract::kControlHas3DSlot
                | Contract::kControlAbovePlane
                | Contract::kCompositionModeBlend4;
            if ((plane1 & 0x00FFFFFFu) == 0 && (plane1 >> 24u) != 0)
                controlAlpha |= Contract::kControlOpaqueBlackBelow;
            captureReference = reference2;
        }

        const u32 control =
            ((controlAlpha & Contract::kControlFlagMask) << Contract::kControlFlagShift)
            | ((evb & 0xFFu) << Contract::kControlEvbShift)
            | ((eva & 0xFFu) << Contract::kControlEvaShift);
        u8 changedPlaneMask = 0;
        const auto store = [&](u32 plane, u32 value) {
            u32& destination = StructuredEnginePlanes[
                engineBase + (static_cast<std::size_t>(plane) * StructuredPixelCount)
                + pixelIndex];
            if (destination == value)
                return;
            destination = value;
            changedPlaneMask |= static_cast<u8>(1u << plane);
        };
        store(0u, plane0);
        store(1u, plane1);
        store(2u, control);
        store(3u, captureReference);
        StructuredEngineChangedMask[engine] |= changedPlaneMask;
    }
    inline void FlushStructuredEngineLine(u32 engine, u32 line) noexcept
    {
        if (engine >= 2u)
            return;
        const u8 changedPlaneMask = StructuredEngineChangedMask[engine];
        StructuredEngineChangedMask[engine] = 0;
        if (changedPlaneMask == 0 || line >= StructuredComposition::kScreenHeight)
            return;

        if (engine == 0u)
        {
            for (u32 plane = 0; plane < StructuredComposition::kPlaneCount; ++plane)
            {
                if (changedPlaneMask & static_cast<u8>(1u << plane))
                    MarkStructuredPlaneDirty(8u + plane);
            }
        }

        for (u32 screen = 0; screen < 2u; ++screen)
        {
            const u8 source = StructuredScreenSource[
                static_cast<std::size_t>(screen) * StructuredComposition::kScreenHeight
                + line];
            if (source != engine)
                continue;
            for (u32 plane = 0; plane < StructuredComposition::kPlaneCount; ++plane)
            {
                if (changedPlaneMask & static_cast<u8>(1u << plane))
                    MarkStructuredPlaneDirty(screen * StructuredComposition::kPlaneCount + plane);
            }
        }
    }
    inline void FlushStructuredGeneration() noexcept
    {
        for (u32 plane = 0; plane < StructuredComposition::kStructuredInputPlaneCount; ++plane)
        {
            if (StructuredPendingPlaneDirtyMask & static_cast<u16>(1u << plane))
                StructuredContentGeneration.Plane[plane] = StructuredFrameGeneration;
        }
        for (u32 screen = 0; screen < StructuredComposition::kStructuredInputLineMetaCount; ++screen)
        {
            if (StructuredPendingLineMetaDirtyMask & static_cast<u8>(1u << screen))
                StructuredContentGeneration.LineMeta[screen] = StructuredFrameGeneration;
        }
        if (StructuredPendingCaptureCommandsDirty)
            StructuredContentGeneration.CaptureCommands = StructuredFrameGeneration;
        StructuredPendingPlaneDirtyMask = 0;
        StructuredPendingLineMetaDirtyMask = 0;
        StructuredPendingCaptureCommandsDirty = false;
    }
    void PrepareStructuredCaptureLine(u32 line, const u32* exact3DLine);
    void StoreStructuredCaptureLine(
        u32 line,
        u32 width,
        u32 destinationBank,
        u32 destinationAddress,
        const u16* captureOutput);
    [[nodiscard]] u32 GetStructuredCaptureReference(
        u32 engine,
        u32 flatByteAddress,
        u16 nativeColor,
        bool object = false) const;
    void RecordStructuredCaptureLine(
        u32 line,
        u32 width,
        u32 destinationBank,
        u32 destinationAddress,
        u32 captureCnt,
        const u16* sourceB,
        u32 sourceBBank,
        u32 sourceBAddress);
    void FinalizeStructuredCaptureFrame();
    [[nodiscard]] bool SnapshotStructuredVramDisplayLine(
        u32 screen, u32 outputLine, u32 sourceLine);
    void BuildStructuredScreenLine(
        u32 engine,
        u32 screen,
        u32 line,
        const u32* output,
        bool forcePlain = false,
        bool preserveVramSnapshot = false);
    void InvalidateStructuredCaptureBlocks(u32 bank, u32 start, u32 len);
#endif

    void DrawScanlineA(u32 line, u32* dst);
    void DrawScanlineB(u32 line, u32* dst);

    void DoCapture(u32 line);

    void ApplyMasterBrightness(u16 regval, u32* dst);
    void ExpandColor(u32* dst);
};

}

#endif // GPU_SOFT_H
