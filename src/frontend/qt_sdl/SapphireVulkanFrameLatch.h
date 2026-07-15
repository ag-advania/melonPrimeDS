#pragma once

#include <array>

#include "VulkanReference/FrameQueue.h"
#include "VulkanReference/VulkanOutput.h"
#include "MelonPrimeDesktopSapphireFrameSidecar.h"
#include "SapphirePublished2DFrame.h"

namespace melonDS
{
class NDS;
}

namespace MelonDSAndroid
{

// Sapphire 0.7.0.rc4 MelonInstance latch dependency closure.
class SapphireVulkanFrameLatch
{
public:
    void bindNds(melonDS::NDS* newNds) { nds_ = newNds; }

    void clearLatchedSoftPackedFrameSnapshot();
    bool updateVulkanTemporal3dHistoryGate();
    [[nodiscard]] bool isVulkanTemporal3dHistoryGateActive() const;
    bool latchSoftPackedFrameSnapshot(
        const Frame* frame,
        const SapphirePublished2DFrame& published,
        bool useStructuredVulkan2D);

    [[nodiscard]] const SoftPackedFrameSnapshot& lastSnapshot() const
    {
        return lastSoftPackedFrameSnapshot;
    }

    SoftPackedFrameSnapshot& mutableLastSnapshot()
    {
        return lastSoftPackedFrameSnapshot;
    }

    [[nodiscard]] const SoftPackedFrameSnapshot& previousSnapshot() const
    {
        return previousSoftPackedFrameSnapshot;
    }

    [[nodiscard]] int structuredCaptureGateFrames() const
    {
        return vulkanStructuredCaptureGateFrames;
    }

    [[nodiscard]] bool regularCaptureTransitionResyncPending() const
    {
        return vulkanRegularCaptureTransitionResyncPending;
    }

    void clearRegularCaptureTransitionResyncPending()
    {
        vulkanRegularCaptureTransitionResyncPending = false;
    }

    void invalidateAll2DTemporalSources();

private:
    melonDS::NDS* nds_ = nullptr;

    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> lastValidTopScreenCapture3dDsFrame{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> lastValidBottomScreenCapture3dDsFrame{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> lastValidTopScreenResolvedPrimary{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> lastValidBottomScreenResolvedPrimary{};
    std::array<u8, SoftPackedFrameSnapshot::kLineCount> lastValidTopScreenResolvedPrimaryLines{};
    std::array<u8, SoftPackedFrameSnapshot::kLineCount> lastValidBottomScreenResolvedPrimaryLines{};
    bool hasLastValidTopScreenCapture3dDsFrame = false;
    bool hasLastValidBottomScreenCapture3dDsFrame = false;
    bool vulkanRegularCaptureTransitionResyncPending = false;
    int vulkanStructuredCaptureGateFrames = 0;
    int vulkanTemporal3dHistoryGateFrames = 0;
    int vulkanTemporal3dNotReadyFrames = 0;
    int vulkanTemporal3dHistoryDebugLogsRemaining = 0;
    SoftPackedFrameSnapshot lastSoftPackedFrameSnapshot;
    SoftPackedFrameSnapshot previousSoftPackedFrameSnapshot;
    DesktopSapphireFrameSidecar lastFrameSidecar_;
    DesktopSapphireFrameSidecar previousFrameSidecar_;
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedEngineATopPlane0{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedEngineATopPlane1{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedEngineATopControl{};
    std::array<u32, SoftPackedFrameSnapshot::kLineCount> cachedEngineATopLineMeta{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedEngineABottomPlane0{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedEngineABottomPlane1{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedEngineABottomControl{};
    std::array<u32, SoftPackedFrameSnapshot::kLineCount> cachedEngineABottomLineMeta{};
    bool cachedEngineATopValid = false;
    bool cachedEngineABottomValid = false;
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedAtypicalDisplayTopPrimary{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedAtypicalDisplayBottomPrimary{};
    std::array<u8, SoftPackedFrameSnapshot::kLineCount> cachedAtypicalDisplayTopPrimaryLines{};
    std::array<u8, SoftPackedFrameSnapshot::kLineCount> cachedAtypicalDisplayBottomPrimaryLines{};
    int framesSinceLastScreenSwapToggle = 1024;
    bool wasInAlternatingMode = false;
};

} // namespace MelonDSAndroid
