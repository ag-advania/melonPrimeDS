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

#include <atomic>

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
    // The renderer callback fires for both CPU reads and writes to a VRAM
    // block a hardware capture had touched (GPU::SyncVRAMCaptureBlock does
    // not tell them apart), so it cannot be used to invalidate structured
    // capture metadata -- a plain CPU read of just-captured VRAM would wipe
    // metadata that is still fresh. Sapphire has no such hook either;
    // structured metadata validity is driven entirely by exact-range
    // invalidate-before-write inside the capture path itself.
    void SyncVRAMCapture(u32 bank, u32 start, u32 len, bool complete) override {};

    bool GetFramebuffers(void** top, void** bottom) override;

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    void SwapBuffers() override;

    static constexpr std::size_t StructuredPixelCount = 256u * 192u;
    // Sapphire accelerated layout: Plane0|Plane1|Control|Meta per physical line.
    static constexpr std::size_t VulkanPackedStride = 256u * 3u + 1u;
    static constexpr std::size_t VulkanPackedPixelCount = VulkanPackedStride * 192u;
    struct StructuredVulkanFrameSnapshot
    {
        // Physical Top/Bottom structured planes before the frontend merge.
        // PackedTop/Bottom contain the corresponding post-merge result.
        std::array<u32, 2u * 3u * StructuredPixelCount> ScreenPlanes{};
        std::array<u32, 2u * 192u> ScreenLineMeta{};
        // Authoritative physical Top/Bottom packed buffers for this generation.
        std::array<u32, VulkanPackedPixelCount> PackedTop{};
        std::array<u32, VulkanPackedPixelCount> PackedBottom{};
        // Exact CPU 3D source used by hardware display capture.
        std::array<u32, StructuredPixelCount> Capture3DSource{};
        std::array<u8, 192u> CaptureLineUses3D{};
        std::array<u8, 192u> Capture3DSourceLineValid{};
        std::array<u8, 192u> TopScreenNeedsCapture3D{};
        std::array<u8, 192u> BottomScreenNeedsCapture3D{};
        bool HasCapture3DSource = false;
        bool CaptureScreenSwap = false;
        bool CaptureScreenSwapValid = false;
        // Sapphire screenSwapLatched, adapted to the desktop core's publication
        // timing: the physical Engine-A destination for this completed packed
        // generation (the VCount-215 renderer latch already names the next one).
        bool ScreenSwapLatched = false;
        bool Renderer3DOwnerIsTop = false;
        bool CaptureBackedClass4Only = false;
        bool CaptureBackedPartialClass0Only = false;
        bool CaptureBackedFullClass0AlternatingCapture = false;
        bool CaptureBackedHasStructured2DSource = false;
        u32 StructuredCopyLines = 0;
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
    void RequestStructuredVulkanResync() noexcept;
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
    // [backBuffer][physicalScreen 0=Top / 1=Bottom]. Authoritative Vulkan 2D
    // producer (Sapphire packed stride). StructuredScreenPlanes retain the
    // pre-merge source needed by Sapphire's post-merge temporal repairs.
    std::array<u32, VulkanPackedPixelCount> VulkanPackedFramebuffer[2][2]{};
    // Structured display-capture metadata (plane0/plane1/control per VRAM
    // bank+line). Shared by both SoftRenderer2D instances (Rend2D_A /
    // Rend2D_B) via Parent -- Sapphire drives both engines through a single
    // GPU2D::SoftRenderer object, so its equivalent arrays are implicitly
    // shared. melonPrimeDS has two separate engine instances; owning this
    // here (keyed by VRAM bank+address, not by engine) is what lets Engine B
    // read capture metadata that Engine-A hardware wrote. Validity is driven
    // entirely by exact-range invalidate-before-write inside the capture
    // path (StoreStructuredCaptureLine) -- never by a visible-frame
    // generation counter, and never by a generic VRAM read/write hook.
    std::array<u32, 4u * 3u * StructuredPixelCount> StructuredCapturePlanes{};
    std::array<u8, 4u * 192u> StructuredCaptureLineValid{};
    // Sapphire CaptureLineUses3d: current hardware-capture source line, reset
    // once per frame. This is distinct from the persistent, VRAM-bank-indexed
    // structured capture plane store above.
    std::array<u8, 192u> StructuredFrameCaptureLineUses3D{};
    std::array<u8, 2u * 192u> StructuredEngineLineUsesCapture3D{};
    std::array<u32, 17u> StructuredCaptureBackedBestClassLines{};
    u32 StructuredCaptureBacked3DLines = 0;
    u32 StructuredCopyLines = 0;
    u32 StructuredCaptureMode = 0;
    std::array<u32, StructuredPixelCount> StructuredCapture3DSource{};
    std::array<u8, 192u> StructuredCapture3DSourceLineValid{};
    alignas(8) u32 Structured3DPlaceholderLine[256]{};
    alignas(8) u32 StructuredCaptureCompositeLine[256]{};
    bool StructuredFrameValid = false;
    bool StructuredCapture3DSourceValid = false;
    bool StructuredCaptureScreenSwap = false;
    bool StructuredCaptureScreenSwapValid = false;
    // Desktop core adaptation: the packed 2D generation is completed after
    // VCount 215 has already latched ownership for the next 3D target.  Track
    // where Engine A actually wrote the 192 visible lines so packed temporal
    // repair is keyed to this generation, not to the next 3D generation.
    bool StructuredPackedScreenSwapAtLine0 = false;
    bool StructuredPackedScreenSwapChangedMidFrame = false;
    u32 StructuredEngineAOnTopLines = 0;
    u32 StructuredEngineAOnBottomLines = 0;
    bool StructuredCaptureCompositeLineValid = false;
    bool StructuredCapturePreparedThisFrame = false;
    std::array<StructuredVulkanFrameSnapshot, 2> CompletedStructuredVulkanFrames{};
    std::atomic_bool StructuredVulkanResyncRequested{false};
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
        u32 legacyControl,
        u32 captureBacked3DSourceClass);
    void StoreStructuredCapturePixel(
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
    [[nodiscard]] u32 ClassifyStructuredCaptureBackedLine(
        u32 engine,
        u32 line,
        const u32* structuredPixels);
    void PrepareStructuredCaptureLine(u32 line, const u32* exact3DLine);
    void BuildStructuredScreenLine(
        u32 engine,
        u32 screen,
        u32 screenLine,
        u32 engineLine,
        const u32* rawPacked,
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
