#include "SapphireVulkanFrameLatch.h"

#include "Platform.h"

using namespace melonDS::Platform;

namespace MelonDSAndroid
{

void SapphireVulkanFrameLatch::clearLatchedSoftPackedFrameSnapshot()
{
    core_.clearLatchedSoftPackedFrameSnapshot();
    lastSidecar_.clear();
}

bool SapphireVulkanFrameLatch::updateVulkanTemporal3dHistoryGate()
{
    if (!temporalEnabled_ || !sapphireTemporalEnabled())
    {
        core_.forceTemporalHistoryGateOff();
        return false;
    }
    return core_.updateVulkanTemporal3dHistoryGate();
}

bool SapphireVulkanFrameLatch::isVulkanTemporal3dHistoryGateActive() const
{
    if (!temporalEnabled_ || !sapphireTemporalEnabled())
        return false;
    return core_.isVulkanTemporal3dHistoryGateActive();
}

bool SapphireVulkanFrameLatch::latchSoftPackedFrameSnapshot(
    const SapphireFrameInput& input,
    const DesktopSapphireFrameSidecar& sidecar,
    bool useStructuredVulkan2D)
{
    if (!input.valid || input.frame == nullptr)
        return false;

#ifndef NDEBUG
    if (sidecar.liveFrontBuffer >= 0 && input.frontBuffer != sidecar.liveFrontBuffer)
    {
        Log(LogLevel::Error,
            "[SapphireFrameIdentity] frontBuffer mismatch published=%d live=%d\n",
            input.frontBuffer,
            sidecar.liveFrontBuffer);
        return false;
    }
    if (input.preparedFrameScreenSwap != sidecar.liveScreenSwap)
    {
        Log(LogLevel::Error,
            "[SapphireFrameIdentity] screenSwap mismatch published=%d live=%d\n",
            input.preparedFrameScreenSwap ? 1 : 0,
            sidecar.liveScreenSwap ? 1 : 0);
        return false;
    }
#endif

    lastSidecar_ = sidecar;
    return core_.latchSoftPackedFrameSnapshot(
        input.frame,
        input.frontBuffer,
        input.preparedFrameScreenSwap,
        useStructuredVulkan2D);
}

} // namespace MelonDSAndroid
