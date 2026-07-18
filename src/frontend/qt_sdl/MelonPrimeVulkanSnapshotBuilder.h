#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <array>

#include "MelonPrimeVulkanOutput.h"

namespace MelonPrime
{

// Immutable, completed-frame input supplied by the guarded software 2D
// renderer. Every pointer belongs to one StructuredVulkanFrame generation.
struct StructuredVulkanSnapshotSource
{
    const u32* plane[2][3]{};
    const u32* lineMeta[2]{};
    // Sapphire packed physical buffers (stride 256*3+1), already merged by
    // the desktop producer and authoritative for the latch.
    const u32* packedTop{};
    const u32* packedBottom{};
    const u32* capture3dSource{};
    const u8* captureLineUses3dMask{};
    const u8* capture3dSourceLineValid{};
    const u8* screenNeedsCapture3d[2]{};
    bool hasCapture3dSource{};
    bool captureScreenSwap{};
    bool captureScreenSwapValid{};
    bool screenSwapLatched{};
    bool captureBackedClass4Only{};
    bool captureBackedPartialClass0Only{};
    bool captureBackedFullClass0AlternatingCapture{};
    bool captureBackedHasStructured2DSource{};
    u32 structuredCopyLines{};
    int frontBuffer{-1};
    bool renderer3dOwnerIsTop{};
    u64 generation{};
    u64 renderer3dRenderSerial{};
    u64 renderer3dCompletionValue{};
    u32 renderer3dImageSlot{};
    bool renderer3dReferenceValid{};
};

class MelonPrimeVulkanSnapshotBuilder
{
public:
    void reset() noexcept;
    bool takeRegularCaptureTransitionResyncRequest() noexcept;
    bool build(
        const StructuredVulkanSnapshotSource& source,
        u64 frameId,
        SoftPackedFrameSnapshot& destination);

private:
    struct EnginePhaseCache
    {
        std::array<u32, SoftPackedFrameSnapshot::kPixelCount> plane0{};
        std::array<u32, SoftPackedFrameSnapshot::kPixelCount> plane1{};
        std::array<u32, SoftPackedFrameSnapshot::kPixelCount> control{};
        std::array<u32, SoftPackedFrameSnapshot::kLineCount> lineMeta{};
        bool valid{};
    };

    // Sapphire-compatible Engine A Top/Bottom phase caches. Identity is
    // "Engine A when routed to Top/Bottom", not "physical Top/Bottom history".
    SoftPackedFrameSnapshot previousSnapshot{};
    EnginePhaseCache engineATop{};
    EnginePhaseCache engineABottom{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> lastValidTopScreenCapture3dDsFrame{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> lastValidBottomScreenCapture3dDsFrame{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> lastValidTopScreenResolvedPrimary{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> lastValidBottomScreenResolvedPrimary{};
    std::array<u8, SoftPackedFrameSnapshot::kLineCount> lastValidTopScreenResolvedPrimaryLines{};
    std::array<u8, SoftPackedFrameSnapshot::kLineCount> lastValidBottomScreenResolvedPrimaryLines{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedAtypicalDisplayTopPrimary{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedAtypicalDisplayBottomPrimary{};
    std::array<u8, SoftPackedFrameSnapshot::kLineCount> cachedAtypicalDisplayTopPrimaryLines{};
    std::array<u8, SoftPackedFrameSnapshot::kLineCount> cachedAtypicalDisplayBottomPrimaryLines{};
    bool hasLastValidTopScreenCapture3dDsFrame = false;
    bool hasLastValidBottomScreenCapture3dDsFrame = false;
    bool vulkanRegularCaptureTransitionResyncPending = false;
    int vulkanStructuredCaptureGateFrames = 0;
    int framesSinceLastScreenSwapToggle = 1024;
    bool wasInAlternatingMode = false;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
