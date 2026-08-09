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

#include "VulkanAmdAntiLag.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <utility>

#include "Platform.h"

namespace melonDS
{

using Platform::Log;
using Platform::LogLevel;


VulkanAmdAntiLag::~VulkanAmdAntiLag()
{
    Shutdown();
}


bool VulkanAmdAntiLag::Initialize(VulkanDevice& device)
{
    Shutdown();

    Device = &device;
    Enabled = false;
    AppliedEnabled = false;
    StateApplied = false;
    FrameOpen = false;

    const VulkanLowLatencyStatus& status = device.GetAmdAntiLagStatus();
    if (!status.Enabled)
    {
        Disable(status.Reason.empty()
            ? std::string("VK_AMD_anti_lag was not enabled on this device")
            : status.Reason);
        return false;
    }

    if (!device.Fns().AntiLagUpdateAMD)
    {
        Disable("the driver enabled VK_AMD_anti_lag but did not provide vkAntiLagUpdateAMD");
        return false;
    }

    Available = true;
    UnavailableReason.clear();
    return true;
}


void VulkanAmdAntiLag::Shutdown() noexcept
{
    // Leave the driver in a defined state rather than mid-pacing. Only worth
    // doing when it was actually turned on.
    if (Available && Enabled && Device && Device->IsValid())
        Update(VK_ANTI_LAG_MODE_OFF_AMD, VK_ANTI_LAG_STAGE_INPUT_AMD, 0);

    Device = nullptr;
    Available = false;
    Enabled = false;
    AppliedEnabled = false;
    StateApplied = false;
    FrameOpen = false;
}


void VulkanAmdAntiLag::Disable(std::string reason) noexcept
{
    Available = false;
    FrameOpen = false;
    UnavailableReason = std::move(reason);
}


void VulkanAmdAntiLag::SetEnabled(bool enabled) noexcept
{
    Enabled = enabled;
}


void VulkanAmdAntiLag::Update(VkAntiLagModeAMD mode, VkAntiLagStageAMD stage, u64 frameIndex)
{
    VkAntiLagPresentationInfoAMD presentation{};
    presentation.sType = VK_STRUCTURE_TYPE_ANTI_LAG_PRESENTATION_INFO_AMD;
    presentation.stage = stage;
    presentation.frameIndex = frameIndex;

    VkAntiLagDataAMD data{};
    data.sType = VK_STRUCTURE_TYPE_ANTI_LAG_DATA_AMD;
    data.mode = mode;
    // 0 = no application frame cap, the same reasoning as Reflex's
    // minimumIntervalUs: the DS's own 60Hz timing and the swapchain present
    // mode already pace this emulator, and a second limiter would fight both.
    data.maxFPS = 0;
    data.pPresentationInfo = &presentation;

    // vkAntiLagUpdateAMD returns void, so there is no runtime result to check
    // and no failure path to disable on.
    Device->Fns().AntiLagUpdateAMD(Device->GetHandle(), &data);
}


void VulkanAmdAntiLag::BeginFrame(u64 frameIndex)
{
    FrameOpen = false;
    if (!Available || !Device || !Device->IsValid())
        return;

    const bool changed = (AppliedEnabled != Enabled) || !StateApplied;

    if (!Enabled)
    {
        // One explicit OFF on the transition, then silence. Calling every frame
        // with mode OFF would be pointless driver traffic in the hot path.
        if (changed)
        {
            Update(VK_ANTI_LAG_MODE_OFF_AMD, VK_ANTI_LAG_STAGE_INPUT_AMD, frameIndex);
            AppliedEnabled = false;
            StateApplied = true;
            Log(LogLevel::Info, "[Vulkan] AMD Radeon Anti-Lag 2 disabled\n");
        }
        return;
    }

    // Stage INPUT belongs immediately before the frame's input is read, which
    // is exactly where the presenter's BeginFrame hook runs.
    Update(VK_ANTI_LAG_MODE_ON_AMD, VK_ANTI_LAG_STAGE_INPUT_AMD, frameIndex);
    FrameOpen = true;

    if (changed)
    {
        AppliedEnabled = true;
        StateApplied = true;
        Log(LogLevel::Info, "[Vulkan] AMD Radeon Anti-Lag 2 enabled (mode=on, maxFPS=0)\n");
    }
}


void VulkanAmdAntiLag::EndFrame(u64 frameIndex)
{
    if (!FrameOpen)
        return;
    FrameOpen = false;

    if (!Available || !Enabled || !Device || !Device->IsValid())
        return;

    // Stage PRESENT, with the frameIndex its INPUT partner used, immediately
    // before vkQueuePresentKHR.
    Update(VK_ANTI_LAG_MODE_ON_AMD, VK_ANTI_LAG_STAGE_PRESENT_AMD, frameIndex);
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
