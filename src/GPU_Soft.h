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
#include "GPU2D_Soft.h"
#include "GPU3D_Soft.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
#include <mutex>
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

    void AllocCapture(u32 bank, u32 start, u32 len) override {};
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // Structured capture metadata is scoped per 2D engine (SoftRenderer2D),
    // matching Sapphire's contract -- only Engine A ever writes it, so this
    // just routes the sync signal there. See SoftRenderer2D for why no
    // visible-frame generation gate is needed once metadata is isolated
    // per engine instead of shared cross-engine.
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override;
#else
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override {};
#endif

    bool GetFramebuffers(void** top, void** bottom) override;

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    void SwapBuffers() override;

    static constexpr std::size_t StructuredPixelCount = 256u * 192u;
    // Sapphire accelerated layout: Plane0|Plane1|Control|Meta per physical line.
    static constexpr std::size_t VulkanPackedStride = 256u * 3u + 1u;
    static constexpr std::size_t VulkanPackedPixelCount = VulkanPackedStride * 192u;
    struct StructuredVulkanFrameSnapshot
    {
        std::array<u32, 2u * 3u * StructuredPixelCount> ScreenPlanes{};
        std::array<u32, 2u * 192u> ScreenLineMeta{};
        // Authoritative physical Top/Bottom packed buffers for this generation.
        // Plane/meta arrays above are derived views of the same write.
        std::array<u32, VulkanPackedPixelCount> PackedTop{};
        std::array<u32, VulkanPackedPixelCount> PackedBottom{};
        // Engine A/B provenance before physical Top/Bottom routing. Diagnostics /
        // capture only — not a presentation re-routing source.
        std::array<u32, 2u * 3u * StructuredPixelCount> EnginePlanes{};
        std::array<u8, 2u * 192u> EngineLineUsesCapture3D{};
        std::array<u32, StructuredPixelCount> Capture3DSource{};
        std::array<u8, 192u> Capture3DSourceLineValid{};
        std::array<u8, 192u> TopScreenNeedsCapture3D{};
        std::array<u8, 192u> BottomScreenNeedsCapture3D{};
        bool HasCapture3DSource = false;
        bool CaptureScreenSwap = false;
        bool CaptureScreenSwapValid = false;
        // Phase key derived at publish from where Engine A actually wrote this
        // generation — never an independent line-0 ScreenSwap side channel.
        bool PhysicalScreenSwap = false;
        bool PhysicalScreenSwapStable = true;
        bool Renderer3DOwnerIsTop = false;
        bool CaptureBackedClass4Only = false;
        bool CaptureBackedHasStructured2DSource = false;
        int FrontBuffer = -1;
        u64 Generation = 0;
        u64 Renderer3DRenderSerial = 0;
        Renderer3DCompletedFrameReference Completed3DReference{};
        bool Valid = false;
    };

    // Copies a single completed generation while the producer lock is held.
    // The Qt presentation thread never observes a ring slot while the
    // emulation thread is recycling it for a newer frame.
    [[nodiscard]] bool CopyStructuredVulkanFrame(StructuredVulkanFrameSnapshot& snapshot) const;
#endif

private:
    friend class SoftRenderer2D;
    friend class SoftRenderer3D;

    u32* Framebuffer[2][2];

    u32* Output3D;
    alignas(8) u32 Output2D[2][256];

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    std::array<u32, 2u * 3u * StructuredPixelCount> StructuredEnginePlanes{};
    std::array<u32, 2u * 3u * StructuredPixelCount> StructuredScreenPlanes{};
    std::array<u32, 2u * 192u> StructuredScreenLineMeta{};
    // [backBuffer][physicalScreen 0=Top / 1=Bottom]. Authoritative Vulkan 2D
    // producer (Sapphire packed stride). ScreenPlanes are derived from this.
    std::array<u32, VulkanPackedPixelCount> VulkanPackedFramebuffer[2][2]{};
    // Structured display-capture metadata (plane0/plane1/control per VRAM
    // bank+line). Shared by both SoftRenderer2D instances (Rend2D_A /
    // Rend2D_B) via Parent -- Sapphire drives both engines through a single
    // GPU2D::SoftRenderer object, so its equivalent arrays are implicitly
    // shared. melonPrimeDS has two separate engine instances; owning this
    // here (keyed by VRAM bank+address, not by engine) is what lets Engine B
    // read capture metadata that Engine-A hardware wrote. Validity is driven
    // by per-line invalidate-before-write (StoreStructuredCaptureLine) and by
    // InvalidateStructuredCaptureRange on genuine CPU/DMA VRAM writes --
    // never by a visible-frame generation counter.
    std::array<u32, 4u * 3u * StructuredPixelCount> StructuredCapturePlanes{};
    std::array<u8, 4u * 192u> StructuredCaptureLineValid{};
    std::array<u8, 4u * 192u> StructuredCaptureLineUses3D{};
    std::array<u8, 2u * 192u> StructuredEngineLineUsesCapture3D{};
    std::array<u32, 192u * 17u> StructuredCaptureBackedSourceClassPixels{};
    std::array<u8, 192u> StructuredCaptureBackedExplicitSlot{};
    std::array<u32, 17u> StructuredCaptureBackedBestClassLines{};
    u32 StructuredCaptureBacked3DLines = 0;
    std::array<u32, StructuredPixelCount> StructuredCapture3DSource{};
    std::array<u8, 192u> StructuredCapture3DSourceLineValid{};
    alignas(8) u32 Structured3DPlaceholderLine[256]{};
    alignas(8) u32 StructuredCaptureCompositeLine[256]{};
    bool StructuredFrameValid = false;
    bool StructuredCapture3DSourceValid = false;
    bool StructuredCaptureScreenSwap = false;
    bool StructuredCaptureScreenSwapValid = false;
    bool StructuredPhysicalScreenSwap = false;
    bool StructuredPhysicalScreenSwapStable = true;
    bool StructuredScreenSwapAtLine0 = false;
    bool StructuredScreenSwapChangedMidFrame = false;
    u32 StructuredEngineAOnTopLines = 0;
    u32 StructuredEngineAOnBottomLines = 0;
    bool StructuredCaptureCompositeLineValid = false;
    bool StructuredCapturePreparedThisFrame = false;
    std::array<StructuredVulkanFrameSnapshot, 2> CompletedStructuredVulkanFrames{};
    mutable std::mutex CompletedStructuredVulkanFrameMutex;
    u64 StructuredVulkanGeneration = 0;

    [[nodiscard]] bool UseStructuredVulkan2D() const noexcept;
    void StoreStructuredEnginePixel(
        u32 engine,
        u32 line,
        u32 x,
        u32 originalVal1,
        u32 originalVal2,
        u32 originalVal3,
        u32 legacyVal1,
        u32 legacyVal2,
        u32 legacyControl);
    void PrepareStructuredCaptureLine(u32 line, const u32* exact3DLine);
    void InvalidateStructuredCaptureRange(u32 bank, u32 start, u32 len);
    void BuildStructuredScreenLine(
        u32 engine,
        u32 screen,
        u32 screenLine,
        u32 engineLine,
        const u32* output,
        bool forcePlain = false);
#endif

    void DrawScanlineA(u32 line, u32* dst);
    void DrawScanlineB(u32 line, u32* dst);

    void DoCapture(u32 line);

    void ApplyMasterBrightness(u16 regval, u32* dst);
    void ExpandColor(u32* dst);
};

}

#endif // GPU_SOFT_H
