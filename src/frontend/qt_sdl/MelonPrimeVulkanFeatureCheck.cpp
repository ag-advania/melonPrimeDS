/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#include "MelonPrimeVulkanFeatureCheck.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <mutex>
#include <utility>

#include "Platform.h"
#include "VulkanContext.h"
#include "VulkanDevice.h"
#include "VulkanFeatureProbe.h"

// Platform::Log lives in melonDS::Platform; the frontend spells it unqualified
// everywhere else, so the using-directive keeps this file consistent.
using namespace melonDS;

namespace MelonPrime::VulkanFeatureCheck
{

namespace
{

// Guards both the cache and the probe itself. The settings dialog (GUI thread)
// and EmuThread both call in, and the probe mutates the process-wide
// VulkanContext, so two concurrent probes would race on the shared instance.
std::mutex g_mutex;
Result g_result;
bool g_probed = false;

// A runtime failure outranks whatever the static probe found: the device passed
// every limit check and still failed to render or present, so the answer stays
// "unavailable" until ResetProbeForRetry() clears it.
bool g_runtimeFailed = false;
std::string g_runtimeFailureReason;

void RunProbeLocked()
{
    g_result = Result{};

    if (g_runtimeFailed)
    {
        g_result.Available = false;
        g_result.Reason = g_runtimeFailureReason.empty()
            ? std::string("Vulkan was disabled after a runtime failure")
            : g_runtimeFailureReason;
        g_result.NvidiaReflexReason = g_result.Reason;
        g_result.AmdAntiLag2Reason = g_result.Reason;
        return;
    }

    auto& context = melonDS::VulkanContext::Get();

    // Presentation is requested even though this probe creates no surface.
    //
    // VulkanContext refuses to hand a headless instance to a later caller that
    // needs to present, and the settings dialog can easily be the first thing
    // in the process to touch Vulkan. Asking for the surface extensions here
    // means the instance the presenter later reuses is already the right one;
    // if the runtime has no WSI extension at all, that is itself a truthful
    // reason to refuse the renderer, because this build only offers Vulkan as a
    // presented renderer.
    if (!context.Acquire(true))
    {
        g_result.Available = false;
        g_result.Reason = context.GetFailureReason();
        if (g_result.Reason.empty())
            g_result.Reason = "the Vulkan runtime could not be initialized";
        g_result.NvidiaReflexReason = g_result.Reason;
        g_result.AmdAntiLag2Reason = g_result.Reason;
        return;
    }

    // VK_NULL_HANDLE: present support cannot be evaluated without a real
    // surface, and QueueFamilySelection reports that as "not checked" rather
    // than "supported". The presenter re-runs this with its own surface before
    // creating a device, which is cheap because no device exists yet.
    const bool selected = context.SelectPhysicalDevice(VK_NULL_HANDLE);
    if (selected && context.HasSelectedDevice())
    {
        const melonDS::Vk::DeviceProbeResult& device = context.GetSelectedDevice();

        g_result.Available = true;
        g_result.AdapterName = device.DeviceName;

        g_result.NvidiaReflexAvailable = device.HasNvLowLatency2;
        if (!g_result.NvidiaReflexAvailable)
        {
            g_result.NvidiaReflexReason =
                device.DeviceName + " does not expose VK_NV_low_latency2";
        }

        g_result.AmdAntiLag2Available = device.HasAmdAntiLag;
        if (!g_result.AmdAntiLag2Available)
        {
            g_result.AmdAntiLag2Reason =
                device.DeviceName + " does not expose VK_AMD_anti_lag";
        }

        Platform::Log(
            Platform::LogLevel::Info,
            "[Vulkan] feature check: available device=%s api=%s driver=%s maxScale=%dx "
            "reflex=%s antilag2=%s\n",
            device.DeviceName.c_str(),
            device.ApiVersionText.c_str(),
            device.DriverVersionText.c_str(),
            device.MaxScaleFactor,
            g_result.NvidiaReflexAvailable ? "yes" : "no",
            g_result.AmdAntiLag2Available ? "yes" : "no");
    }
    else
    {
        g_result.Available = false;
        g_result.Reason = context.GetFailureReason();
        if (g_result.Reason.empty())
        {
            g_result.Reason = context.GetCandidateCount() == 0
                ? std::string("no Vulkan physical device was reported by the runtime")
                : std::string("no Vulkan device met the renderer's requirements");
        }
        g_result.NvidiaReflexReason = g_result.Reason;
        g_result.AmdAntiLag2Reason = g_result.Reason;

        Platform::Log(
            Platform::LogLevel::Warn,
            "[Vulkan] feature check: unavailable candidates=%u reason=%s\n",
            context.GetCandidateCount(),
            g_result.Reason.c_str());
    }

    // Released unconditionally. Holding a reference here would keep the
    // VkInstance alive for the whole session just because the settings dialog
    // was opened once, and -- worse -- a reference taken by a future headless
    // probe would make the presenter's Acquire(true) fail.
    context.Release();
}

} // namespace


const Result& Probe()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_probed)
    {
        RunProbeLocked();
        g_probed = true;
    }
    return g_result;
}


bool IsRuntimeAvailable()
{
    return Probe().Available;
}


void ReportRuntimeFailure(std::string reason)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    // First failure wins: it is the one closest to the root cause. Later
    // failures are consequences of running without a working renderer.
    if (!g_runtimeFailed)
    {
        g_runtimeFailed = true;
        g_runtimeFailureReason = std::move(reason);
        Platform::Log(
            Platform::LogLevel::Error,
            "[Vulkan] runtime failure reported: %s\n",
            g_runtimeFailureReason.empty()
                ? "unspecified Vulkan failure"
                : g_runtimeFailureReason.c_str());
    }

    g_probed = false;
}


void ResetProbeForRetry()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    // Opening Video Settings calls this to retry a previously failed probe.
    // A live shared logical device is already stronger evidence than another
    // physical-device probe, and probing it again while the emulation thread
    // is actively submitting Vulkan work races NVIDIA's driver during a
    // Vulkan -> other-backend transition. Keep the successful cached result;
    // once the last Vulkan client is gone, a later retry can probe normally.
    if (VulkanDevice::HasSharedDevice(VulkanContext::Get()))
        return;

    g_runtimeFailed = false;
    g_runtimeFailureReason.clear();
    g_probed = false;
}

} // namespace MelonPrime::VulkanFeatureCheck

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
