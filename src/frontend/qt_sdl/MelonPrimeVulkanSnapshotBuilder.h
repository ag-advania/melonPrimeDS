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
    const u32* capture3dSource{};
    const u8* capture3dSourceLineValid{};
    const u8* screenNeedsCapture3d[2]{};
    bool hasCapture3dSource{};
    bool captureScreenSwap{};
    bool captureScreenSwapValid{};
    bool captureBackedClass4Only{};
    int frontBuffer{-1};
    bool screenSwap{};
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

    // Two renderer-owner-only phases plus four exact
    // (renderer owner, capture owner) phases.
    std::array<PhaseHistory, 6> phaseHistory{};
    std::array<PhaseHistory, 2> capturePhaseHistory{};
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
