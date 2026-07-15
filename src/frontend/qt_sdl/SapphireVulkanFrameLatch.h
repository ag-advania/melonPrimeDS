#pragma once

#include "SapphireGenerated/SapphireFrameLatchCore.h"
#include "MelonPrimeSapphireFrameInput.h"
#include "MelonPrimeSapphirePipelineMode.h"
#include "MelonPrimeDesktopSapphireFrameSidecar.h"

namespace melonDS
{
class NDS;
}

namespace MelonDSAndroid
{

class SapphireVulkanFrameLatch
{
public:
    void bindNds(melonDS::NDS* newNds)
    {
        nds_ = newNds;
        core_.bindNds(newNds);
    }

    void setTemporalEnabled(bool enabled) { temporalEnabled_ = enabled; }
    [[nodiscard]] bool temporalEnabled() const { return temporalEnabled_; }

    void clearLatchedSoftPackedFrameSnapshot();
    bool updateVulkanTemporal3dHistoryGate();
    [[nodiscard]] bool isVulkanTemporal3dHistoryGateActive() const;
    bool latchSoftPackedFrameSnapshot(
        const SapphireFrameInput& input,
        const DesktopSapphireFrameSidecar& sidecar,
        bool useStructuredVulkan2D);

    [[nodiscard]] const SoftPackedFrameSnapshot& lastSnapshot() const
    {
        return core_.lastSnapshot();
    }

    SoftPackedFrameSnapshot& mutableLastSnapshot()
    {
        return core_.mutableLastSnapshot();
    }

    [[nodiscard]] const SoftPackedFrameSnapshot& previousSnapshot() const
    {
        return core_.previousSnapshot();
    }

    [[nodiscard]] int structuredCaptureGateFrames() const
    {
        return core_.structuredCaptureGateFrames();
    }

    [[nodiscard]] bool regularCaptureTransitionResyncPending() const
    {
        return core_.regularCaptureTransitionResyncPending();
    }

    void clearRegularCaptureTransitionResyncPending()
    {
        core_.clearRegularCaptureTransitionResyncPending();
    }

    [[nodiscard]] const DesktopSapphireFrameSidecar& lastSidecar() const
    {
        return lastSidecar_;
    }

private:
    melonDS::NDS* nds_ = nullptr;
    bool temporalEnabled_ = true;
    SapphireFrameLatchCore core_;
    DesktopSapphireFrameSidecar lastSidecar_;
};

} // namespace MelonDSAndroid
