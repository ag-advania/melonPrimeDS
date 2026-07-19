#pragma once

// Runtime reference oracle for the Vulkan software-2D packed-frame snapshot
// pipeline. This entire translation unit compiles to nothing unless
// MELONPRIME_VULKAN_REFERENCE_ORACLE is defined (opt-in via the build system),
// so normal builds pay zero cost and carry zero risk.
//
// Purpose: an independent, from-reference transcription of
// SapphireRhodonite/melonDS-android 0.7.0.rc4 MelonInstance.cpp
// latchSoftPackedFrameSnapshot(), runnable on the same live
// StructuredVulkanSnapshotSource as MelonPrimeVulkanSnapshotBuilder::build(),
// so the two outputs can be diffed frame-by-frame at runtime.

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_VULKAN_REFERENCE_ORACLE)

#include <array>

#include "MelonPrimeVulkanSnapshotBuilder.h"
#include "MelonPrimeVulkanOutput.h"

namespace MelonPrime
{

// Independent reference implementation. Mirrors the public shape of
// MelonPrimeVulkanSnapshotBuilder (reset()/build()) so a caller can run both on
// identical input and compare the results. Owns its own persistent state,
// separate from the production builder's.
class MelonPrimeVulkanSnapshotReferenceOracle
{
public:
    void reset() noexcept;
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

// Compares a production snapshot against the oracle snapshot pixel-by-pixel
// (Top then Bottom, row-major, plane0 then plane1 then control per pixel) and
// line-meta-by-line-meta. On the FIRST divergence found it logs a single
// ReferenceParityMismatch line at LogLevel::Error and returns; if the two
// snapshots match, it does nothing (no per-frame OK spam). Intended to be
// called only when the oracle is active, so it needs no separate perf guard.
void compareReferenceOracleSnapshot(
    const SoftPackedFrameSnapshot& current,
    const SoftPackedFrameSnapshot& reference,
    u64 generation);

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN && MELONPRIME_VULKAN_REFERENCE_ORACLE
