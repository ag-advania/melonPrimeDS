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
    const u32* enginePlane[2][3]{};
    const u8* engineLineUsesCapture3d[2]{};
    const u32* capture3dSource{};
    const u8* capture3dSourceLineValid{};
    const u8* screenNeedsCapture3d[2]{};
    bool hasCapture3dSource{};
    bool captureScreenSwap{};
    bool captureScreenSwapValid{};
    bool physicalScreenSwap{};
    bool captureBackedClass4Only{};
    bool captureBackedHasStructured2DSource{};
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
    bool build(
        const StructuredVulkanSnapshotSource& source,
        u64 frameId,
        SoftPackedFrameSnapshot& destination);

private:
    struct PhaseHistory
    {
        SoftPackedFrameSnapshot snapshot{};
        u64 generation{};
        bool valid{};
    };

    struct EnginePhaseCache
    {
        std::array<u32, SoftPackedFrameSnapshot::kPixelCount> plane0{};
        std::array<u32, SoftPackedFrameSnapshot::kPixelCount> plane1{};
        std::array<u32, SoftPackedFrameSnapshot::kPixelCount> control{};
        std::array<u32, SoftPackedFrameSnapshot::kLineCount> lineMeta{};
        bool valid{};
    };

    // Two physical-only phases plus four exact
    // (physical ScreenSwap, capture owner) phases for packed recovery.
    // Capture history is four phases of (physicalScreenSwap, captureScreenSwap)
    // so opposite PhysicalScreenSwap frames never share one capture bucket.
    //
    // A ScreenSwap-toggling title alternates ownership every frame. Packed
    // line recovery must use the last snapshot from the SAME ownership phase;
    // recovering from the immediately previous opposite phase fills Top with
    // the other engine's content and looks like a complete Top/Bottom swap.
    std::array<PhaseHistory, 6> phaseHistory{};
    std::array<PhaseHistory, 4> capturePhaseHistory{};

    // Sapphire-compatible Engine A Top/Bottom phase caches. Identity is
    // "Engine A when routed to Top/Bottom", not "physical Top/Bottom history".
    SoftPackedFrameSnapshot previousSnapshot{};
    EnginePhaseCache engineATop{};
    EnginePhaseCache engineABottom{};
    int framesSinceLastScreenSwapToggle = 1024;
    bool wasInAlternatingMode = false;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
