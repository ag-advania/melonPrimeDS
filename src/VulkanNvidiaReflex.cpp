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
#include "VulkanPerf.h"

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
    FrameGeneration = 0;
    PresentedSinceSleep = true;
    DiscardPendingSleepOwnership();

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

    AsyncSleepEnabled = true;
    if (!StartAsyncSleepWorker())
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
    CompleteAsyncSleep();
    DiscardPendingSleepOwnership();
    StopAsyncSleepWorker();

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
    RenderSubmitOpen = false;
    PresentOpen = false;
    FrameId = 0;
    FrameGeneration = 0;
    SleepValue = 0;
    Mode = VulkanNvidiaReflexMode::Off;
    AppliedMode = VulkanNvidiaReflexMode::Off;
    PresentedSinceSleep = true;
    AsyncSleepEnabled = false;
}


void VulkanNvidiaReflex::Disable(std::string reason) noexcept
{
    Available = false;
    ModeApplied = false;
    FrameOpen = false;
    InputSampled = false;
    SimulationOpen = false;
    RenderSubmitOpen = false;
    PresentOpen = false;
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

    CompleteAsyncSleep();
    DiscardPendingSleepOwnership();

    Swapchain = swapchain;

    // Sleep mode is per swapchain and does not survive recreation, so a resize
    // or a vsync change has to re-arm it. Forcing AppliedMode to a value that
    // cannot match makes ApplySleepMode() do the call unconditionally.
    ModeApplied = false;
    FrameOpen = false;
    InputSampled = false;
    SimulationOpen = false;
    RenderSubmitOpen = false;
    PresentOpen = false;
    PresentedSinceSleep = true;

    if (!Available || Swapchain == VK_NULL_HANDLE)
        return;

    ApplySleepMode();
}


void VulkanNvidiaReflex::SetMode(VulkanNvidiaReflexMode mode)
{
    if (Mode == mode && ModeApplied)
        return;

    CompleteAsyncSleep();
    DiscardPendingSleepOwnership();

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

    VkResult res = VK_SUCCESS;
    {
        VulkanPerf::ScopedCpuTimer timer(VulkanPerf::CpuMetric::ReflexSetSleepMode);
        res = Device->Fns().SetLatencySleepModeNV(
            Device->GetHandle(), Swapchain, &info);
    }
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
            "[Vulkan] NVIDIA Reflex mode=%s lowLatencyMode=%s lowLatencyBoost=%s minimumIntervalUs=%u\n",
            VulkanNvidiaReflexModeName(Mode),
            info.lowLatencyMode ? "true" : "false",
            info.lowLatencyBoost ? "true" : "false",
            info.minimumIntervalUs);
    }
    return true;
}


// ---------------------------------------------------------------------------
// Frame path
// ---------------------------------------------------------------------------

bool VulkanNvidiaReflex::StartAsyncSleepWorker()
{
    try
    {
        AsyncSleepStop = false;
        AsyncSleepThread = std::thread(&VulkanNvidiaReflex::AsyncSleepWorkerMain, this);
    }
    catch (...)
    {
        AsyncSleepEnabled = false;
        Disable("the asynchronous Reflex pacing worker could not be started");
        return false;
    }

    Log(LogLevel::Info,
        "[Vulkan] asynchronous Reflex Off-mode WSI pacing enabled\n");
    return true;
}


void VulkanNvidiaReflex::StopAsyncSleepWorker() noexcept
{
    if (!AsyncSleepThread.joinable())
        return;

    {
        std::lock_guard<std::mutex> lock(AsyncSleepMutex);
        AsyncSleepStop = true;
    }
    AsyncSleepCv.notify_all();
    AsyncSleepThread.join();

    AsyncSleepStop = false;
    AsyncSleepRequest = false;
    AsyncSleepRunning = false;
    AsyncSleepReady = false;
    DiscardPendingSleepOwnership();
}


void VulkanNvidiaReflex::AsyncSleepWorkerMain() noexcept
{
    for (;;)
    {
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        u64 value = 0;
        {
            std::unique_lock<std::mutex> lock(AsyncSleepMutex);
            AsyncSleepCv.wait(lock, [this]() {
                return AsyncSleepStop || AsyncSleepRequest;
            });
            if (AsyncSleepStop)
                return;

            swapchain = AsyncSleepSwapchain;
            value = AsyncSleepValue;
            AsyncSleepRequest = false;
            AsyncSleepRunning = true;
        }

        VkLatencySleepInfoNV sleep{};
        sleep.sType = VK_STRUCTURE_TYPE_LATENCY_SLEEP_INFO_NV;
        sleep.signalSemaphore = SleepSemaphore;
        sleep.value = value;

        VkResult sleepResult = VK_SUCCESS;
        {
            VulkanPerf::ScopedCpuTimer timer(VulkanPerf::CpuMetric::ReflexLatencySleep);
            sleepResult = Device->Fns().LatencySleepNV(
                Device->GetHandle(), swapchain, &sleep);
        }

        VkResult waitResult = VK_SUCCESS;
        if (sleepResult == VK_SUCCESS)
        {
            VkSemaphoreWaitInfoKHR wait{};
            wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
            wait.semaphoreCount = 1;
            wait.pSemaphores = &SleepSemaphore;
            wait.pValues = &sleep.value;
            {
                VulkanPerf::ScopedCpuTimer timer(VulkanPerf::CpuMetric::ReflexSleepWait);
                waitResult = Device->Fns().WaitSemaphoresKHR(
                    Device->GetHandle(), &wait, 1000ull * 1000ull * 1000ull);
            }
        }

        {
            std::lock_guard<std::mutex> lock(AsyncSleepMutex);
            AsyncSleepCallResult = sleepResult;
            AsyncSleepWaitResult = waitResult;
            AsyncSleepRunning = false;
            AsyncSleepReady = true;
        }
        AsyncSleepCv.notify_all();
    }
}


// `ownerGeneration` is the frame generation permitted to join this sleep, and
// nothing else may. Issuing after a present stamps FrameGeneration + 1; the
// cold-start path inside BeginFrame stamps the generation it just opened.
bool VulkanNvidiaReflex::QueueAsyncSleep(u64 ownerGeneration)
{
    {
        std::lock_guard<std::mutex> lock(AsyncSleepMutex);
        if (!AsyncSleepThread.joinable() || AsyncSleepRequest
            || AsyncSleepRunning || AsyncSleepReady)
        {
            return false;
        }

        AsyncSleepSwapchain = Swapchain;
        AsyncSleepValue = ++SleepValue;
        AsyncSleepRequest = true;
        PresentedSinceSleep = false;
        AsyncSleepCv.notify_one();
    }

    PendingSleepValid = true;
    PendingSleepGeneration = ownerGeneration;
    return true;
}


void VulkanNvidiaReflex::DiscardPendingSleepOwnership() noexcept
{
    PendingSleepValid = false;
    PendingSleepGeneration = 0;
}


bool VulkanNvidiaReflex::CompleteAsyncSleep()
{
    if (!AsyncSleepThread.joinable())
    {
        DiscardPendingSleepOwnership();
        return true;
    }

    VkResult sleepResult = VK_SUCCESS;
    VkResult waitResult = VK_SUCCESS;
    u64 value = 0;
    {
        std::unique_lock<std::mutex> lock(AsyncSleepMutex);
        if (!AsyncSleepRequest && !AsyncSleepRunning && !AsyncSleepReady)
        {
            DiscardPendingSleepOwnership();
            return true;
        }
        // Measured apart from ReflexLatencySleep/ReflexSleepWait on purpose:
        // those two are the driver cost the worker absorbed, this one is what
        // actually reaches the CPU hot path. A near-zero join with a ~1ms
        // worker wait is the whole point of issuing the request early.
        {
            VulkanPerf::ScopedCpuTimer timer(VulkanPerf::CpuMetric::ReflexSleepJoin);
            AsyncSleepCv.wait(lock, [this]() { return AsyncSleepReady; });
        }
        sleepResult = AsyncSleepCallResult;
        waitResult = AsyncSleepWaitResult;
        value = AsyncSleepValue;
        AsyncSleepReady = false;
    }

    // Consumed either way: a failed sleep must not stay adoptable by a later
    // frame, and a successful one has just been joined by its owner.
    DiscardPendingSleepOwnership();

    if (sleepResult != VK_SUCCESS)
    {
        if (SleepValue == value)
            --SleepValue;
        DisableForRuntimeFailure("vkLatencySleepNV(async pacing)", sleepResult);
        FrameOpen = false;
        return false;
    }
    if (ClassifyVulkanReflexSleepWaitResult(waitResult)
        == VulkanReflexSleepWaitAction::DisableForRuntimeFailure)
    {
        DisableForRuntimeFailure(
            "vkWaitSemaphores(Reflex async pacing sleep)", waitResult);
        PresentedSinceSleep = true;
        FrameOpen = false;
        return false;
    }
    return true;
}


void VulkanNvidiaReflex::NotifyPresented() noexcept
{
    PresentedSinceSleep = true;

    // The previous present is now accepted and its PRESENT_END marker has
    // already been emitted. Start the next Off-mode WSI pacing operation at
    // this earliest legal point so presenter cleanup and the entire next game
    // frame can overlap the driver-side wait. The request is stamped for the
    // *next* generation, so the frame that is still open cannot join it;
    // FinishFrame() checks that ownership.
    //
    // On/OnBoost deliberately do NOT preissue here. Measured on 2026-08-24
    // (F7, 2x, VSync off, frame limit off): the blocking lives inside
    // vkLatencySleepNV itself (p50 1247 us) and not in the semaphore wait
    // (p50 2 us), so there is no semaphore to let mature. Off can hide that
    // block behind a whole emulated frame and its join falls to p50 191 us,
    // but On/OnBoost must complete the wait before input sampling and so can
    // only overlap the few tens of microseconds of presenter cleanup between
    // present and the next BeginFrame. Preissuing for them moved p50 1247 us
    // to p50 1240 us and left FPS unchanged at ~332 against ~337, so it was
    // not adopted: it buys nothing and moves a vendor pacing call off the
    // thread NVIDIA's integration model expects it on.
    //
    // If the worker is unexpectedly busy the request is simply not made and
    // any sleep already in flight keeps the generation it was stamped with, so
    // its owner still drains it. PresentedSinceSleep stays set, and BeginFrame
    // falls back to issuing the sleep itself rather than skipping pacing or
    // spending a second sleep between the same pair of presents.
    if (AsyncSleepEnabled && Mode == VulkanNvidiaReflexMode::Off
        && IsFramePathAvailable())
    {
        QueueAsyncSleep(FrameGeneration + 1);
    }
}

void VulkanNvidiaReflex::BeginFrame(u64 logicalFrameId)
{
    if (!IsFramePathAvailable())
    {
        FrameOpen = false;
        return;
    }

    if (FrameOpen)
        FinishFrame();

    // Reflex frame IDs represent logical emulated/game frames. They are not
    // presenter callbacks, swapchain image indices, or frame-ring slots. A
    // skipped frame therefore leaves a gap, while a repeated callback cannot
    // split one logical frame into two driver IDs.
    if (logicalFrameId == 0 || logicalFrameId <= FrameId)
    {
        Log(
            LogLevel::Error,
            "[Vulkan] NVIDIA Reflex rejected a non-increasing logical frame ID=%llu previous=%llu\n",
            static_cast<unsigned long long>(logicalFrameId),
            static_cast<unsigned long long>(FrameId));
        return;
    }
    FrameId = logicalFrameId;
    FrameOpen = true;
    InputSampled = false;
    SimulationOpen = false;
    RenderSubmitOpen = false;
    PresentOpen = false;

    // The generation advances only once the frame is really open, and only
    // after the FinishFrame() above has closed the previous one. That ordering
    // is what stops the previous frame from joining the sleep that was issued
    // for this one.
    ++FrameGeneration;

    // Off adopts the sleep issued after the previous present and keeps it
    // overlapped with this whole game frame, joining it at the present
    // boundary (MarkPresentStart). That policy is worth about 195 FPS on this
    // workload. On/OnBoost never have a sleep to adopt -- see NotifyPresented()
    // for the measurement that rejected preissuing for them -- so they fall
    // through to the inline issue-and-wait below.
    if (PendingSleepBelongsToThisFrame())
        return;

    // vkLatencySleepNV is specified to be called exactly once between presents.
    // The presenter legitimately skips frames (minimised window, swapchain not
    // ready, no ROM), so a second sleep with no present in between would break
    // that contract. Those frames keep their markers and their id and simply do
    // not sleep again.
    if (!PresentedSinceSleep)
        return;

    if (AsyncSleepEnabled && Mode == VulkanNvidiaReflexMode::Off)
    {
        if (!QueueAsyncSleep(FrameGeneration))
        {
            Disable("the asynchronous Reflex pacing request overlapped another request");
            FrameOpen = false;
        }
        return;
    }

    // On/OnBoost every frame, plus any Off frame with no preissued sleep to
    // adopt (cold start, swapchain recreate, mode change, or a present that
    // never reached NotifyPresented()). The pacing wait completes here, before
    // input sampling, which is the position Reflex's contract constrains.
    const Vk::DeviceDispatch& fns = Device->Fns();

    VkLatencySleepInfoNV sleep{};
    sleep.sType = VK_STRUCTURE_TYPE_LATENCY_SLEEP_INFO_NV;
    sleep.signalSemaphore = SleepSemaphore;
    sleep.value = ++SleepValue;

    VkResult res = VK_SUCCESS;
    {
        VulkanPerf::ScopedCpuTimer timer(VulkanPerf::CpuMetric::ReflexLatencySleep);
        res = fns.LatencySleepNV(Device->GetHandle(), Swapchain, &sleep);
    }
    if (res != VK_SUCCESS)
    {
        // Roll the value back: the driver did not accept the request, so
        // nothing will ever signal it and a later wait on it would hang.
        --SleepValue;
        DisableForRuntimeFailure("vkLatencySleepNV", res);
        FrameOpen = false;
        return;
    }

    // The API contract separates sleep request issue from the semaphore wait,
    // but on this driver the block is in vkLatencySleepNV above (p50 1247 us
    // with pacing on) and this wait is nearly free (p50 2 us). It is kept
    // because the semaphore is the specified completion signal, not because it
    // is where the time goes.
    VkSemaphoreWaitInfoKHR wait{};
    wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR;
    wait.semaphoreCount = 1;
    wait.pSemaphores = &SleepSemaphore;
    wait.pValues = &sleep.value;

    // One second is far beyond any legitimate pacing delay; it exists so a
    // driver bug degrades into a dropped frame instead of a hung emulator.
    {
        VulkanPerf::ScopedCpuTimer timer(VulkanPerf::CpuMetric::ReflexSleepWait);
        res = fns.WaitSemaphoresKHR(
            Device->GetHandle(), &wait, 1000ull * 1000ull * 1000ull);
    }
    if (ClassifyVulkanReflexSleepWaitResult(res)
        == VulkanReflexSleepWaitAction::DisableForRuntimeFailure)
    {
        DisableForRuntimeFailure("vkWaitSemaphores(Reflex sleep)", res);
        // The frame is closed by DisableForRuntimeFailure(). Mark the sleep as
        // consumed so a later recovery/re-enable cannot wait on this
        // unsignalled semaphore value.
        PresentedSinceSleep = true;
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

    VulkanPerf::CpuMetric metric = VulkanPerf::CpuMetric::ReflexMarkerInputSample;
    switch (marker)
    {
    case VK_LATENCY_MARKER_SIMULATION_START_NV:
        metric = VulkanPerf::CpuMetric::ReflexMarkerSimulationStart;
        break;
    case VK_LATENCY_MARKER_SIMULATION_END_NV:
        metric = VulkanPerf::CpuMetric::ReflexMarkerSimulationEnd;
        break;
    case VK_LATENCY_MARKER_RENDERSUBMIT_START_NV:
        metric = VulkanPerf::CpuMetric::ReflexMarkerRenderSubmitStart;
        break;
    case VK_LATENCY_MARKER_RENDERSUBMIT_END_NV:
        metric = VulkanPerf::CpuMetric::ReflexMarkerRenderSubmitEnd;
        break;
    case VK_LATENCY_MARKER_PRESENT_START_NV:
        metric = VulkanPerf::CpuMetric::ReflexMarkerPresentStart;
        break;
    case VK_LATENCY_MARKER_PRESENT_END_NV:
        metric = VulkanPerf::CpuMetric::ReflexMarkerPresentEnd;
        break;
    case VK_LATENCY_MARKER_INPUT_SAMPLE_NV:
    default:
        break;
    }

    // vkSetLatencyMarkerNV returns void: there is no result to check and no
    // failure path to handle here.
    VulkanPerf::ScopedCpuTimer timer(metric);
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
    if (!FrameOpen || RenderSubmitOpen || PresentOpen)
        return;
    if (SimulationOpen)
    {
        SetMarker(VK_LATENCY_MARKER_SIMULATION_END_NV);
        SimulationOpen = false;
    }
    SetMarker(VK_LATENCY_MARKER_RENDERSUBMIT_START_NV);
    RenderSubmitOpen = true;
}

void VulkanNvidiaReflex::MarkRenderSubmitEnd()
{
    if (!FrameOpen || !RenderSubmitOpen)
        return;
    SetMarker(VK_LATENCY_MARKER_RENDERSUBMIT_END_NV);
    RenderSubmitOpen = false;
}

void VulkanNvidiaReflex::MarkPresentStart()
{
    if (!FrameOpen || PresentOpen)
        return;
    // Off's join point. On/OnBoost already joined in BeginFrame, so this is a
    // no-op for them; the ownership check keeps it from ever reaching past
    // this frame's own sleep.
    if (PendingSleepBelongsToThisFrame() && !CompleteAsyncSleep())
        return;
    MarkRenderSubmitEnd();
    MarkSimulationEnd();
    SetMarker(VK_LATENCY_MARKER_PRESENT_START_NV);
    PresentOpen = true;
}

void VulkanNvidiaReflex::MarkPresentEnd()
{
    if (!FrameOpen || !PresentOpen)
        return;
    SetMarker(VK_LATENCY_MARKER_PRESENT_END_NV);
    PresentOpen = false;
}


void VulkanNvidiaReflex::FinishFrame()
{
    // Drain only a sleep this frame owns. The sleep queued after this frame's
    // present belongs to the next frame, and joining it here would put the full
    // pacing wait back on the CPU hot path -- exactly the regression this
    // generation stamp exists to prevent.
    if (PendingSleepBelongsToThisFrame())
        CompleteAsyncSleep();
    // Defensive closure covers a presenter admission skip or a renderer
    // transition. It never invents a present: the present markers are only
    // closed when MarkPresentStart() already bracketed QueuePresentKHR.
    MarkPresentEnd();
    MarkRenderSubmitEnd();
    MarkSimulationEnd();
    FrameOpen = false;
    InputSampled = false;
    SimulationOpen = false;
    RenderSubmitOpen = false;
    PresentOpen = false;
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
