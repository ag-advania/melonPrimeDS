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

#ifndef VULKAN_NVIDIA_REFLEX_H
#define VULKAN_NVIDIA_REFLEX_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <string>

#include "VulkanCommon.h"
#include "VulkanDevice.h"

namespace melonDS
{

// A Reflex sleep wait is complete only when the driver reports success. A
// watchdog timeout must close the active frame and disable Reflex; treating it
// as success would leave the frame open while its semaphore value is still
// unsignalled, causing the next sleep to wait on an invalid pacing contract.
enum class VulkanReflexSleepWaitAction : int
{
    Continue = 0,
    DisableForRuntimeFailure,
};

constexpr VulkanReflexSleepWaitAction ClassifyVulkanReflexSleepWaitResult(
    VkResult result) noexcept
{
    return result == VK_SUCCESS
        ? VulkanReflexSleepWaitAction::Continue
        : VulkanReflexSleepWaitAction::DisableForRuntimeFailure;
}

// Mode values are the *config* values, shared with the DX12 backend so one
// setting means the same thing on both. DX12NvidiaReflexMode has the identical
// Off/On/OnBoost triple; keeping the numbers equal is what lets
// Config's NvidiaReflexMode (range {0,2}, default 1) drive either renderer.
//
// On and OnBoost are genuinely different modes, not a bool plus a hint: both
// set VkLatencySleepModeInfoNV::lowLatencyMode, and only OnBoost additionally
// sets lowLatencyBoost (which keeps the GPU clocks up at the cost of power).
enum class VulkanNvidiaReflexMode : int
{
    Off = 0,
    On = 1,
    OnBoost = 2,
};

[[nodiscard]] VulkanNvidiaReflexMode VulkanNvidiaReflexModeFromConfig(int value) noexcept;
[[nodiscard]] const char* VulkanNvidiaReflexModeName(VulkanNvidiaReflexMode mode) noexcept;

// NVIDIA Reflex on top of VK_NV_low_latency2.
//
// Scope and ownership
// -------------------
// One instance belongs to whichever object owns the *swapchain*, because every
// entry point in the extension except vkQueueNotifyOutOfBandNV is scoped to a
// VkSwapchainKHR. In this tree that is MelonPrime::VulkanPresenter: the 3D
// renderer's VkDevice has no surface and never presents, so it could not drive
// Reflex even though it is the object that carries the user's setting.
//
// The class lives in src/ rather than next to the presenter because it depends
// on nothing above the Vulkan backend layer -- VulkanDevice and a swapchain
// handle -- and sits at the same level as VulkanSync/VulkanMemory, which are
// also generic device-scoped helpers consumed by the frontend.
//
// Threading
// ---------
// Initialize()/Shutdown()/SetSwapchain() run on whichever thread owns the
// swapchain's lifetime, and every marker call runs on the presenting thread.
// The two never overlap: the presenter recreates its swapchain from inside its
// own BeginFrame(). Nothing here is internally synchronised.
//
// Failure policy
// --------------
// Reflex can never fail a renderer. An unsupported GPU, a missing dependency,
// or a driver that starts rejecting calls at runtime all end in the same place:
// Available goes false, UnavailableReason says why, every frame hook becomes a
// predictable no-op, and rendering continues untouched.
class VulkanNvidiaReflex
{
public:
    VulkanNvidiaReflex() = default;
    ~VulkanNvidiaReflex();

    VulkanNvidiaReflex(const VulkanNvidiaReflex&) = delete;
    VulkanNvidiaReflex& operator=(const VulkanNvidiaReflex&) = delete;

    // `device` must outlive this object. Returns false when Reflex is not
    // usable, which is a supported outcome and not an error: the caller keeps
    // going and only the feature is lost.
    bool Initialize(VulkanDevice& device);
    void Shutdown() noexcept;

    // Every swapchain (re)creation must be reported, and VK_NULL_HANDLE must be
    // reported *before* the swapchain is destroyed. The sleep mode is a
    // swapchain property, so it is re-applied here rather than only at init.
    void SetSwapchain(VkSwapchainKHR swapchain);

    // True when the extension is enabled and the entry points resolved. Says
    // nothing about the user's setting -- see IsActive().
    [[nodiscard]] bool IsAvailable() const noexcept { return Available; }
    [[nodiscard]] const std::string& GetUnavailableReason() const noexcept
    {
        return UnavailableReason;
    }

    // True when the driver is actually pacing frames right now: available, a
    // live swapchain, mode != Off, and vkSetLatencySleepModeNV accepted. This
    // is the "Actual" column of the low-latency log line.
    [[nodiscard]] bool IsActive() const noexcept
    {
        return Available && Swapchain != VK_NULL_HANDLE
            && Mode != VulkanNvidiaReflexMode::Off && ModeApplied;
    }

    // Sleep, latency markers and Present ID correlation remain live in Off
    // mode, as required by NVIDIA's Reflex QA contract. IsActive() above only
    // describes whether low-latency pacing itself is enabled.
    [[nodiscard]] bool IsFramePathAvailable() const noexcept
    {
        return Available && Swapchain != VK_NULL_HANDLE && ModeApplied;
    }

    [[nodiscard]] VulkanNvidiaReflexMode GetMode() const noexcept { return Mode; }

    // Safe to call every frame with an unchanged value; the driver is only
    // touched on a real transition. This is what makes a settings-dialog change
    // take effect without recreating the device or the swapchain.
    void SetMode(VulkanNvidiaReflexMode mode);

    // True when VkSwapchainLatencyCreateInfoNV must be chained into
    // VkSwapchainCreateInfoKHR. Latency mode is enabled on the swapchain
    // whenever the extension is present so the user can toggle Reflex at
    // runtime; vkSetLatencySleepModeNV, not swapchain recreation, is what
    // actually turns pacing on and off.
    [[nodiscard]] bool WantsSwapchainLatencyMode() const noexcept { return Available; }

    // --- frame path --------------------------------------------------------
    //
    // Per emulated frame, in this order:
    //
    //   BeginFrame()            latency sleep, before any input is read
    //   MarkInputSample()       immediately before the first input read
    //   MarkSimulationStart()   emulation begins
    //   MarkSimulationEnd()     emulation returned
    //   MarkRenderSubmitStart() immediately before vkQueueSubmit
    //   MarkRenderSubmitEnd()   immediately after vkQueueSubmit
    //   MarkPresentStart()      immediately before vkQueuePresentKHR
    //   MarkPresentEnd()        immediately after vkQueuePresentKHR
    //   FinishFrame()
    //
    // All eight are no-ops unless IsFramePathAvailable(). Markers carry
    // GetFrameId(), and the same value is chained into the submission
    // (VkLatencySubmissionPresentIdNV) and the present (VkPresentIdKHR), which
    // is what lets the driver correlate the three.
    // `logicalFrameId` is allocated by the emulation thread exactly once per
    // game frame and remains stable through every marker, submit, and present.
    void BeginFrame(u64 logicalFrameId);
    void MarkInputSample();
    void MarkSimulationStart();
    void MarkSimulationEnd();
    void MarkRenderSubmitStart();
    void MarkRenderSubmitEnd();
    void MarkPresentStart();
    void MarkPresentEnd();
    void FinishFrame();

    // presentID for the frame currently open. Stable from BeginFrame() to
    // FinishFrame(), which is exactly the window the submission and the present
    // both fall inside.
    [[nodiscard]] u64 GetFrameId() const noexcept { return FrameId; }

    // The presenter calls this to decide whether to chain
    // VkLatencySubmissionPresentIdNV / VkPresentIdKHR onto this frame's submit
    // and present.
    [[nodiscard]] bool WantsFrameIdChaining() const noexcept
    {
        return IsFramePathAvailable() && FrameOpen;
    }

    // Driver-side latency reports, newest last, for the frames whose markers
    // the driver has finished correlating. Fills at most `maxCount` entries and
    // returns how many were written; 0 means Reflex is not running or the
    // driver has nothing complete yet.
    //
    // This is the only observable proof that the markers, the tagged submission
    // and the tagged present all reached the driver and were matched to one
    // presentID -- every marker entry point itself returns void. Cold path:
    // intended for a periodic diagnostic, not for every frame.
    [[nodiscard]] u32 QueryTimings(VkLatencyTimingsFrameReportNV* out, u32 maxCount);

    // Records that this frame reached vkQueuePresentKHR. vkLatencySleepNV is
    // specified to be called once between presents, so a frame the presenter
    // skipped must not consume another sleep.
    void NotifyPresented() noexcept { PresentedSinceSleep = true; }

private:
    bool CreateSleepSemaphore();
    bool ApplySleepMode();
    void SetMarker(VkLatencyMarkerNV marker);
    void DisableForRuntimeFailure(const char* operation, VkResult result);
    void Disable(std::string reason) noexcept;

    VulkanDevice* Device = nullptr;
    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;

    // Timeline semaphore the driver signals when the frame should start.
    // Created once per device; VkLatencySleepInfoNV requires a timeline.
    VkSemaphore SleepSemaphore = VK_NULL_HANDLE;
    u64 SleepValue = 0;

    VulkanNvidiaReflexMode Mode = VulkanNvidiaReflexMode::Off;
    VulkanNvidiaReflexMode AppliedMode = VulkanNvidiaReflexMode::Off;
    std::string UnavailableReason;

    u64 FrameId = 0;
    bool Available = false;
    bool ModeApplied = false;
    bool FrameOpen = false;
    bool InputSampled = false;
    bool SimulationOpen = false;
    bool RenderSubmitOpen = false;
    bool PresentOpen = false;
    bool PresentedSinceSleep = true;
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_NVIDIA_REFLEX_H
