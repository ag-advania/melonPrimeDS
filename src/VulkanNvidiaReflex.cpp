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

#include "VulkanNvidiaReflex.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <utility>

#include "Platform.h"

namespace melonDS
{

using Platform::Log;
using Platform::LogLevel;


VulkanNvidiaReflexMode VulkanNvidiaReflexModeFromConfig(int value) noexcept
{
    switch (value)
    {
    case 1:  return VulkanNvidiaReflexMode::On;
    case 2:  return VulkanNvidiaReflexMode::OnBoost;
    default: return VulkanNvidiaReflexMode::Off;
    }
}


const char* VulkanNvidiaReflexModeName(VulkanNvidiaReflexMode mode) noexcept
{
    switch (mode)
    {
    case VulkanNvidiaReflexMode::On:      return "on";
    case VulkanNvidiaReflexMode::OnBoost: return "on+boost";
    default:                              return "off";
    }
}


VulkanNvidiaReflex::~VulkanNvidiaReflex()
{
    Shutdown();
}


// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

bool VulkanNvidiaReflex::Initialize(VulkanDevice& device)
{
    Shutdown();

    Device = &device;
    Mode = VulkanNvidiaReflexMode::Off;
    AppliedMode = VulkanNvidiaReflexMode::Off;
    ModeApplied = false;
    FrameId = 0;
    PresentedSinceSleep = true;

    // VulkanDevice already decided this at vkCreateDevice time and recorded
    // why. Repeating the reason verbatim keeps one explanation in the log
    // instead of two that can drift apart.
    const VulkanLowLatencyStatus& status = device.GetNvLowLatency2Status();
    if (!status.Enabled)
    {
        Disable(status.Reason.empty()
            ? std::string("VK_NV_low_latency2 was not enabled on this device")
            : status.Reason);
        return false;
    }

    // A driver that advertised the extension but did not hand back its entry
    // points is broken rather than unsupported, so it is called out separately.
    const Vk::DeviceDispatch& fns = device.Fns();
    if (!fns.SetLatencySleepModeNV || !fns.LatencySleepNV || !fns.SetLatencyMarkerNV
        || !fns.WaitSemaphoresKHR || !fns.CreateSemaphore || !fns.DestroySemaphore)
    {
        Disable("the driver enabled VK_NV_low_latency2 but did not provide its entry points");
        return false;
    }

    if (!CreateSleepSemaphore())
        return false;

    Available = true;
    UnavailableReason.clear();
    return true;
}


bool VulkanNvidiaReflex::CreateSleepSemaphore()
{
    // vkLatencySleepNV signals this semaphore to a caller-chosen value when the
    // frame should start, and the app blocks on it with vkWaitSemaphores. That
    // is a timeline semaphore by construction -- a binary one has no value to
    // wait for -- which is why VulkanDevice makes VK_KHR_timeline_semaphore a
    // hard dependency of enabling Reflex at all.
    VkSemaphoreTypeCreateInfoKHR typeInfo{};
    typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE_KHR;
    typeInfo.initialValue = 0;

    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = &typeInfo;

    const VkResult res = Device->Fns().CreateSemaphore(
        Device->GetHandle(), &info, nullptr, &SleepSemaphore);
    if (res != VK_SUCCESS)
    {
        SleepSemaphore = VK_NULL_HANDLE;
        Disable("the Reflex timeline semaphore could not be created: " + Vk::FormatResult(res));
        return false;
    }

    SleepValue = 0;
    Device->SetDebugName(VK_OBJECT_TYPE_SEMAPHORE, SleepSemaphore, "MelonPrime Reflex sleep");
    return true;
}


void VulkanNvidiaReflex::Shutdown() noexcept
{
    // The swapchain is dropped first: leaving pacing enabled on a swapchain the
    // caller is about to destroy would leave the driver holding a stale handle.
    if (Available && Swapchain != VK_NULL_HANDLE && Mode != VulkanNvidiaReflexMode::Off)
    {
        Mode = VulkanNvidiaReflexMode::Off;
        ApplySleepMode();
    }
    Swapchain = VK_NULL_HANDLE;

    if (SleepSemaphore != VK_NULL_HANDLE && Device && Device->IsValid()
        && Device->Fns().DestroySemaphore)
    {
        // Caller contract: the device is idle by the time Reflex is torn down
        // (VulkanPresenter::Shutdown drains before destroying anything), so the
        // driver cannot still be signalling this semaphore.
        Device->Fns().DestroySemaphore(Device->GetHandle(), SleepSemaphore, nullptr);
    }
    SleepSemaphore = VK_NULL_HANDLE;

    Device = nullptr;
    Available = false;
    ModeApplied = false;
    FrameOpen = false;
    InputSampled = false;
    SimulationOpen = false;
    FrameId = 0;
    SleepValue = 0;
    Mode = VulkanNvidiaReflexMode::Off;
    AppliedMode = VulkanNvidiaReflexMode::Off;
    PresentedSinceSleep = true;
}


void VulkanNvidiaReflex::Disable(std::string reason) noexcept
{
    Available = false;
    ModeApplied = false;
    FrameOpen = false;
    InputSampled = false;
    SimulationOpen = false;
    UnavailableReason = std::move(reason);
}


void VulkanNvidiaReflex::DisableForRuntimeFailure(const char* operation, VkResult result)
{
    const std::string reason =
        std::string(operation) + " failed at runtime: " + Vk::FormatResult(result);
    Log(LogLevel::Warn, "[Vulkan] NVIDIA Reflex disabled: %s\n", reason.c_str());
    Disable(reason);
}


// ---------------------------------------------------------------------------
// Swapchain and mode
// ---------------------------------------------------------------------------

void VulkanNvidiaReflex::SetSwapchain(VkSwapchainKHR swapchain)
{
    if (Swapchain == swapchain)
        return;

    Swapchain = swapchain;

    // Sleep mode is per swapchain and does not survive recreation, so a resize
    // or a vsync change has to re-arm it. Forcing AppliedMode to a value that
    // cannot match makes ApplySleepMode() do the call unconditionally.
    ModeApplied = false;
    FrameOpen = false;
    InputSampled = false;
    SimulationOpen = false;
    PresentedSinceSleep = true;

    if (!Available || Swapchain == VK_NULL_HANDLE)
        return;

    ApplySleepMode();
}


void VulkanNvidiaReflex::SetMode(VulkanNvidiaReflexMode mode)
{
    if (Mode == mode && ModeApplied)
        return;

    Mode = mode;
    if (!Available || Swapchain == VK_NULL_HANDLE)
        return;

    ApplySleepMode();
}


bool VulkanNvidiaReflex::ApplySleepMode()
{
    if (!Available || Swapchain == VK_NULL_HANDLE)
        return false;

    const bool enable = (Mode != VulkanNvidiaReflexMode::Off);

    VkLatencySleepModeInfoNV info{};
    info.sType = VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV;
    info.lowLatencyMode = enable ? VK_TRUE : VK_FALSE;
    // Boost is the *only* difference between On and On+Boost. It tells the
    // driver to hold GPU clocks up rather than let them drop while the CPU is
    // being paced, which trades power for a little more consistency.
    info.lowLatencyBoost = (Mode == VulkanNvidiaReflexMode::OnBoost) ? VK_TRUE : VK_FALSE;
    // 0 = no application-imposed frame cap. MelonPrimeDS paces itself against
    // the DS's own 60Hz timing and the swapchain present mode, so imposing a
    // second limiter here would fight both.
    info.minimumIntervalUs = 0;

    const VkResult res = Device->Fns().SetLatencySleepModeNV(
        Device->GetHandle(), Swapchain, &info);
    if (res != VK_SUCCESS)
    {
        DisableForRuntimeFailure("vkSetLatencySleepModeNV", res);
        return false;
    }

    const bool changed = (AppliedMode != Mode) || !ModeApplied;
    AppliedMode = Mode;
    ModeApplied = true;

    if (changed)
    {
        Log(LogLevel::Info,
            "[Vulkan] NVIDIA Reflex mode=%s lowLatencyMode=%s lowLatencyBoost=%s\n",
            VulkanNvidiaReflexModeName(Mode),
            info.lowLatencyMode ? "true" : "false",
            info.lowLatencyBoost ? "true" : "false");
    }
    return true;
}


// ---------------------------------------------------------------------------
// Frame path
// ---------------------------------------------------------------------------

void VulkanNvidiaReflex::BeginFrame()
{
    if (!IsFramePathAvailable())
    {
        FrameOpen = false;
        return;
    }

    // presentID must strictly increase for a swapchain, so it is bumped per
    // frame the app *starts*, not per frame it manages to present. A skipped
    // frame simply leaves a gap, which is legal and is what makes the id usable
    // as the correlation key for the markers, the submit and the present.
    ++FrameId;
    FrameOpen = true;
    InputSampled = false;
    SimulationOpen = false;

    // vkLatencySleepNV is specified to be called exactly once between presents.
    // The presenter legitimately skips frames (minimised window, swapchain not
    // ready, no ROM), so a second sleep with no present in between would break
    // that contract. Those frames keep their markers and their id and simply do
    // not sleep again.
    if (!PresentedSinceSleep)
        return;

    const Vk::DeviceDispatch& fns = Device->Fns();

    VkLatencySleepInfoNV sleep{};
    sleep.sType = VK_STRUCTURE_TYPE_LATENCY_SLEEP_INFO_NV;
    sleep.signalSemaphore = SleepSemaphore;
    sleep.value = ++SleepValue;

    VkResult res = fns.LatencySleepNV(Device->GetHandle(), Swapchain, &sleep);
    if (res != VK_SUCCESS)
    {
        // Roll the value back: the driver did not accept the request, so
        // nothing will ever signal it and a later wait on it would hang.
        --SleepValue;
        DisableForRuntimeFailure("vkLatencySleepNV", res);
        FrameOpen = false;
        return;
    }

    // vkLatencySleepNV returns immediately; the actual pacing delay is this
    // host wait. It is deliberately NOT a device idle -- it blocks only this
    // thread until the driver says the frame should start, which is the entire
    // point of the extension.
    VkSemaphoreWaitInfoKHR wait{};
    wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
    wait.semaphoreCount = 1;
    wait.pSemaphores = &SleepSemaphore;
    wait.pValues = &sleep.value;

    // One second is far beyond any legitimate pacing delay; it exists so a
    // driver bug degrades into a dropped frame instead of a hung emulator.
    res = fns.WaitSemaphoresKHR(Device->GetHandle(), &wait, 1000ull * 1000ull * 1000ull);
    if (res != VK_SUCCESS && res != VK_TIMEOUT)
    {
        DisableForRuntimeFailure("vkWaitSemaphores(Reflex sleep)", res);
        FrameOpen = false;
        return;
    }

    PresentedSinceSleep = false;
}


void VulkanNvidiaReflex::SetMarker(VkLatencyMarkerNV marker)
{
    if (!FrameOpen || !IsFramePathAvailable())
        return;

    VkSetLatencyMarkerInfoNV info{};
    info.sType = VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV;
    info.presentID = FrameId;
    info.marker = marker;

    // vkSetLatencyMarkerNV returns void: there is no result to check and no
    // failure path to handle here.
    Device->Fns().SetLatencyMarkerNV(Device->GetHandle(), Swapchain, &info);
}


void VulkanNvidiaReflex::MarkInputSample()
{
    if (!FrameOpen || InputSampled || SimulationOpen)
        return;
    SetMarker(VK_LATENCY_MARKER_INPUT_SAMPLE_NV);
    InputSampled = true;
}

void VulkanNvidiaReflex::MarkSimulationStart()
{
    if (!FrameOpen || !InputSampled || SimulationOpen)
        return;
    SetMarker(VK_LATENCY_MARKER_SIMULATION_START_NV);
    SimulationOpen = true;
}

void VulkanNvidiaReflex::MarkSimulationEnd()
{
    if (!FrameOpen || !SimulationOpen)
        return;
    SetMarker(VK_LATENCY_MARKER_SIMULATION_END_NV);
    SimulationOpen = false;
}

void VulkanNvidiaReflex::MarkRenderSubmitStart()
{
    SetMarker(VK_LATENCY_MARKER_RENDERSUBMIT_START_NV);
}

void VulkanNvidiaReflex::MarkRenderSubmitEnd()
{
    SetMarker(VK_LATENCY_MARKER_RENDERSUBMIT_END_NV);
}

void VulkanNvidiaReflex::MarkPresentStart()
{
    SetMarker(VK_LATENCY_MARKER_PRESENT_START_NV);
}

void VulkanNvidiaReflex::MarkPresentEnd()
{
    SetMarker(VK_LATENCY_MARKER_PRESENT_END_NV);
}


void VulkanNvidiaReflex::FinishFrame()
{
    FrameOpen = false;
    InputSampled = false;
    SimulationOpen = false;
}


u32 VulkanNvidiaReflex::QueryTimings(VkLatencyTimingsFrameReportNV* out, u32 maxCount)
{
    if (!out || maxCount == 0 || !IsFramePathAvailable()
        || !Device->Fns().GetLatencyTimingsNV)
        return 0;

    const Vk::DeviceDispatch& fns = Device->Fns();

    // Two-call idiom: with pTimings null the driver only reports how many
    // completed frames it is holding.
    VkGetLatencyMarkerInfoNV info{};
    info.sType = VK_STRUCTURE_TYPE_GET_LATENCY_MARKER_INFO_NV;
    info.timingCount = 0;
    info.pTimings = nullptr;
    fns.GetLatencyTimingsNV(Device->GetHandle(), Swapchain, &info);

    if (info.timingCount == 0)
        return 0;

    const u32 count = (info.timingCount < maxCount) ? info.timingCount : maxCount;

    // Every entry must carry its sType before the call: the driver writes into
    // an array the application owns and does not initialise it.
    for (u32 i = 0; i < count; i++)
    {
        out[i] = VkLatencyTimingsFrameReportNV{};
        out[i].sType = VK_STRUCTURE_TYPE_LATENCY_TIMINGS_FRAME_REPORT_NV;
    }

    info.timingCount = count;
    info.pTimings = out;
    fns.GetLatencyTimingsNV(Device->GetHandle(), Swapchain, &info);

    return count;
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
