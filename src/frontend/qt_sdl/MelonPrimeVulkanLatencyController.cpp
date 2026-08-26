/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "MelonPrimeVulkanLatencyController.h"

#include <string>

#include "Platform.h"
#include "VulkanDevice.h"

namespace MelonPrime
{

using melonDS::Platform::Log;
using melonDS::Platform::LogLevel;

bool VulkanLatencyController::Initialize(melonDS::VulkanDevice& device)
{
    // Neither is required. A device without the extension, or a driver that
    // refuses it, leaves the presenter running with no vendor authority.
    Reflex.Initialize(device);
    AntiLag.Initialize(device);
    return true;
}

void VulkanLatencyController::Shutdown() noexcept
{
    AntiLag.Shutdown();
    Reflex.Shutdown();
    LogicalFrameIndex = 0;
    LastAcceptedLogicalFrameId = 0;
    LoggedReflexMode = -1;
    LoggedReflexActive = false;
    LoggedAntiLagActive = false;
    StateLogged = false;
}

void VulkanLatencyController::ApplyPreferences(int reflexMode, bool antiLag2Enabled)
{
    Reflex.SetMode(melonDS::VulkanNvidiaReflexModeFromConfig(reflexMode));
    AntiLag.SetEnabled(antiLag2Enabled);
}

void VulkanLatencyController::BeginReflexSleep(melonDS::u64 logicalFrameId)
{
    // On/OnBoost perform vkLatencySleepNV synchronously here, before input.
    // Off adopts the operation already issued after the previous present and
    // lets it overlap this frame until the present boundary. SIMULATION_START
    // is deliberately not emitted here: it belongs after input sampling.
    Reflex.BeginFrame(logicalFrameId);

    // Anti-Lag and Reflex describe the same logical emulated frame. The id is
    // owned by EmuThread, so turning either feature off does not create a
    // presenter-local counter with different semantics.
    LogicalFrameIndex = logicalFrameId;
    if (LastAcceptedLogicalFrameId == 0 && logicalFrameId > 0)
        LastAcceptedLogicalFrameId = logicalFrameId - 1;
}

void VulkanLatencyController::BeginAntiLagInput(melonDS::u64 logicalFrameId)
{
    // Anti-Lag's INPUT stage, specified to be issued immediately before the
    // application reads input -- the same point the Reflex sleep just returned
    // from. Its PRESENT partner is issued just before vkQueuePresentKHR with
    // this same index.
    LogicalFrameIndex = logicalFrameId;
    AntiLag.BeginFrame(LogicalFrameIndex);
}

void VulkanLatencyController::PrepareForTeardown()
{
    Reflex.FinishFrame();
    Reflex.SetMode(melonDS::VulkanNvidiaReflexMode::Off);
    AntiLag.SetEnabled(false);
    AntiLag.EndFrame(LogicalFrameIndex);
    AntiLag.BeginFrame(LogicalFrameIndex);
}

void VulkanLatencyController::LogVendorState(
    const melonDS::VulkanDevice& device, const char* context) const
{
    const melonDS::VulkanLowLatencyStatus& reflexStatus = device.GetNvLowLatency2Status();
    const melonDS::VulkanLowLatencyStatus& antiLagStatus = device.GetAmdAntiLagStatus();

    // "device-extension-enabled" is what vkCreateDevice accepted; "actual" is
    // what the frame path is really doing right now. They differ whenever the
    // extension is present but the user has the feature switched off, or when
    // a runtime failure disabled it -- and that gap is the whole reason both
    // columns exist. A reason from the running module wins over the device's,
    // because it is the more specific one.
    const std::string& reflexReason = Reflex.IsAvailable()
        ? (Reflex.IsActive()
            ? std::string("latency markers active; no frame-rate cap requested")
            : std::string("latency markers active; low-latency pacing switched off by NvidiaReflexMode"))
        : (Reflex.GetUnavailableReason().empty() ? reflexStatus.Reason : Reflex.GetUnavailableReason());

    Log(LogLevel::Info,
        "[Vulkan] %s NVIDIA Reflex (VK_NV_low_latency2): requested=%s supported=%s "
        "device-extension-enabled=%s actual=%s reason=%s\n",
        context,
        melonDS::VulkanNvidiaReflexModeName(Reflex.GetMode()),
        reflexStatus.Supported ? "yes" : "no",
        reflexStatus.Enabled ? "yes" : "no",
        Reflex.IsActive() ? "active" : "inactive",
        reflexReason.empty() ? "not evaluated" : reflexReason.c_str());

    const std::string& antiLagReason = AntiLag.IsAvailable()
        ? (AntiLag.IsActive()
            ? std::string("latency markers active; no frame-rate cap requested")
            : std::string("supported, switched off by AmdAntiLag2Enabled"))
        : (AntiLag.GetUnavailableReason().empty() ? antiLagStatus.Reason : AntiLag.GetUnavailableReason());

    Log(LogLevel::Info,
        "[Vulkan] %s AMD Radeon Anti-Lag 2 (VK_AMD_anti_lag): requested=%s supported=%s "
        "device-extension-enabled=%s actual=%s reason=%s\n",
        context,
        AntiLag.IsEnabledRequested() ? "on" : "off",
        antiLagStatus.Supported ? "yes" : "no",
        antiLagStatus.Enabled ? "yes" : "no",
        AntiLag.IsActive() ? "active" : "inactive",
        antiLagReason.empty() ? "not evaluated" : antiLagReason.c_str());
}

const char* VulkanLatencyController::ConsumeStateChangeContext() noexcept
{
    const int mode = static_cast<int>(Reflex.GetMode());
    const bool reflexActive = Reflex.IsActive();
    const bool antiLagActive = AntiLag.IsActive();

    if (StateLogged
        && mode == LoggedReflexMode
        && reflexActive == LoggedReflexActive
        && antiLagActive == LoggedAntiLagActive)
    {
        return nullptr;
    }

    LoggedReflexMode = mode;
    LoggedReflexActive = reflexActive;
    LoggedAntiLagActive = antiLagActive;

    const bool first = !StateLogged;
    StateLogged = true;
    return first ? "low-latency:" : "low-latency changed:";
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
