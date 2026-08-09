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

#ifndef VULKAN_AMD_ANTI_LAG_H
#define VULKAN_AMD_ANTI_LAG_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <string>

#include "VulkanCommon.h"
#include "VulkanDevice.h"

namespace melonDS
{

// AMD Radeon Anti-Lag 2 on top of VK_AMD_anti_lag.
//
// Deliberately a bool, not a mode enum. VK_AMD_anti_lag has
// VK_ANTI_LAG_MODE_DRIVER_CONTROL_AMD / ON / OFF, but DRIVER_CONTROL means
// "whatever the Radeon control panel says", which is not a thing a per-emulator
// setting should be able to select on the user's behalf -- and there is no
// boost concept anywhere in the extension. The DX12 backend's
// DX12AmdAntiLag2 has no mode enum either, so Off/On is the complete and
// matching surface. The setting is Config's AmdAntiLag2Enabled.
//
// Scope, threading and failure policy match VulkanNvidiaReflex: the presenter
// owns one instance, every call is on the presenting thread, and any failure
// permanently disables the feature without ever affecting rendering.
//
// Unlike Reflex this is device-scoped, not swapchain-scoped -- vkAntiLagUpdateAMD
// takes only a VkDevice -- so a swapchain rebuild does not disturb it.
class VulkanAmdAntiLag
{
public:
    VulkanAmdAntiLag() = default;
    ~VulkanAmdAntiLag();

    VulkanAmdAntiLag(const VulkanAmdAntiLag&) = delete;
    VulkanAmdAntiLag& operator=(const VulkanAmdAntiLag&) = delete;

    // `device` must outlive this object. False means Anti-Lag 2 is unusable,
    // which is a supported outcome, not an error.
    bool Initialize(VulkanDevice& device);
    void Shutdown() noexcept;

    [[nodiscard]] bool IsAvailable() const noexcept { return Available; }
    [[nodiscard]] const std::string& GetUnavailableReason() const noexcept
    {
        return UnavailableReason;
    }

    // Available AND switched on by the user AND accepted by the driver: the
    // "Actual" column of the low-latency log line.
    [[nodiscard]] bool IsActive() const noexcept { return Available && Enabled; }

    // Cheap to call every frame with an unchanged value. A real transition is
    // pushed to the driver on the next BeginFrame(), including the transition
    // to off, which sends one explicit VK_ANTI_LAG_MODE_OFF_AMD update so the
    // driver stops pacing instead of being left mid-frame.
    void SetEnabled(bool enabled) noexcept;

    // --- frame path --------------------------------------------------------
    //
    // vkAntiLagUpdateAMD is called twice per frame with a matching frameIndex:
    // stage INPUT immediately before the frame's input is read, and stage
    // PRESENT immediately before vkQueuePresentKHR.
    void BeginFrame(u64 frameIndex);
    void EndFrame(u64 frameIndex);

private:
    void Update(VkAntiLagModeAMD mode, VkAntiLagStageAMD stage, u64 frameIndex);
    void Disable(std::string reason) noexcept;

    VulkanDevice* Device = nullptr;
    std::string UnavailableReason;

    bool Available = false;
    bool Enabled = false;
    // Last value pushed to the driver, so an unchanged setting costs nothing
    // and a change is logged exactly once.
    bool AppliedEnabled = false;
    bool StateApplied = false;
    // True between a BeginFrame() that issued an INPUT update and its matching
    // EndFrame(); a PRESENT update without its INPUT partner is meaningless.
    bool FrameOpen = false;
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_AMD_ANTI_LAG_H
