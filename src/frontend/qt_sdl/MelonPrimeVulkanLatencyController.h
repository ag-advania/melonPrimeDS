/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef MELONPRIME_VULKAN_LATENCY_CONTROLLER_H
#define MELONPRIME_VULKAN_LATENCY_CONTROLLER_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "VulkanAmdAntiLag.h"
#include "VulkanNvidiaReflex.h"
#include "VulkanPresentPacer.h"

namespace MelonPrime
{

// The vendor low-latency state machines behind the Vulkan presenter.
//
// NVIDIA Reflex and AMD Anti-Lag are scoped to whatever owns the swapchain and
// the present queue, which is why they live on the presenter side rather than
// the renderer -- the audit rated that placement better than the DX12 one it
// replaced. What it also noted is that the presenter had grown large enough
// that the vendor state machine deserved its own object inside it. This is
// that object.
//
// It owns: both vendor sessions, the logical frame index they share, and the
// "log only when the effective state changed" latch.
//
// It deliberately does not own:
//
//   VulkanPresentPacer         it participates in swapchain *construction* --
//                              surface capability queries, create flags,
//                              NV low-latency present-mode selection -- so it
//                              belongs with the object that builds swapchains
//   VulkanPresentLatencyCapture an A/B measurement instrument that spans both
//                              the vendor markers and the pacer, so it cannot
//                              sit on either side alone
//
// The presenter keeps what the audit says it must: the position where the real
// vkQueueSubmit and vkQueuePresentKHR happen. The markers around them are
// fired from here, at those positions.
class VulkanLatencyController
{
public:
    bool Initialize(melonDS::VulkanDevice& device);
    void Shutdown() noexcept;

    // Re-applied every frame from the live config value. Both setters are
    // no-ops when the value has not changed, so a settings-dialog change takes
    // effect on the next frame without recreating device or swapchain.
    void ApplyPreferences(int reflexMode, bool antiLag2Enabled);

    // --- effective state ---------------------------------------------------

    [[nodiscard]] bool IsReflexActive() const noexcept { return Reflex.IsActive(); }
    [[nodiscard]] bool IsAntiLagActive() const noexcept { return AntiLag.IsActive(); }

    // Only the presenter knows whether the vendor path is available and still
    // accepted at runtime, so screen-panel admission must use this rather than
    // the renderer's configured request.
    [[nodiscard]] bool HasEffectiveAuthority() const noexcept
    {
        return melonDS::VulkanHasEffectiveLowLatencyAuthority(
            Reflex.IsActive(), AntiLag.IsActive());
    }

    [[nodiscard]] melonDS::VulkanNvidiaReflexMode GetReflexMode() const noexcept
    {
        return Reflex.GetMode();
    }
    [[nodiscard]] bool IsReflexAvailable() const noexcept { return Reflex.IsAvailable(); }
    [[nodiscard]] bool IsReflexFramePathAvailable() const noexcept
    {
        return Reflex.IsFramePathAvailable();
    }
    [[nodiscard]] const std::string& GetReflexUnavailableReason() const noexcept
    {
        return Reflex.GetUnavailableReason();
    }
    [[nodiscard]] bool IsAntiLagAvailable() const noexcept { return AntiLag.IsAvailable(); }
    [[nodiscard]] bool IsAntiLagEnabledRequested() const noexcept
    {
        return AntiLag.IsEnabledRequested();
    }
    [[nodiscard]] const std::string& GetAntiLagUnavailableReason() const noexcept
    {
        return AntiLag.GetUnavailableReason();
    }

    // --- swapchain lifecycle ----------------------------------------------
    //
    // Reflex binds to the swapchain object, so the presenter tells it when one
    // is created or destroyed. Present-mode selection stays with the pacer.

    [[nodiscard]] bool WantsSwapchainLatencyMode() const noexcept
    {
        return Reflex.WantsSwapchainLatencyMode();
    }
    void OnSwapchainCreated(VkSwapchainKHR swapchain) { Reflex.SetSwapchain(swapchain); }
    void OnSwapchainDestroyed() { Reflex.SetSwapchain(VK_NULL_HANDLE); }

    // --- frame phases ------------------------------------------------------
    //
    // One emulated frame, in order:
    //   BeginReflexSleep -> BeginAntiLagInput -> MarkInputSample
    //   -> MarkSimulationStart/End -> MarkRenderSubmitStart/End
    //   -> MarkPresentStart -> (vkQueuePresentKHR) -> MarkPresentEnd
    //   -> EndAntiLagPresent -> FinishFrame
    //
    // Reflex's sleep and Anti-Lag's INPUT stage are separate entry points
    // rather than one BeginFrame, because the presenter times them separately
    // and the pacer decision sits between them.

    void BeginReflexSleep(melonDS::u64 logicalFrameId);
    void BeginAntiLagInput(melonDS::u64 logicalFrameId);

    void MarkInputSample() { Reflex.MarkInputSample(); }
    void MarkSimulationStart() { Reflex.MarkSimulationStart(); }
    void MarkSimulationEnd() { Reflex.MarkSimulationEnd(); }
    void MarkRenderSubmitStart() { Reflex.MarkRenderSubmitStart(); }
    void MarkRenderSubmitEnd() { Reflex.MarkRenderSubmitEnd(); }
    void MarkPresentStart() { Reflex.MarkPresentStart(); }
    void MarkPresentEnd() { Reflex.MarkPresentEnd(); }
    void EndAntiLagPresent() { AntiLag.EndFrame(LogicalFrameIndex); }
    void FinishFrame() { Reflex.FinishFrame(); }
    void NotifyPresented() noexcept { Reflex.NotifyPresented(); }

    // Reflex chains frame ids across submissions when the driver correlates
    // them; the presenter tags its submissions with this.
    [[nodiscard]] bool WantsFrameIdChaining() const noexcept
    {
        return Reflex.WantsFrameIdChaining();
    }
    [[nodiscard]] melonDS::u64 GetReflexFrameId() const noexcept
    {
        return Reflex.GetFrameId();
    }

    // The logical emulated frame id, owned by EmuThread and shared by both
    // vendors so turning one off does not create a second numbering.
    [[nodiscard]] melonDS::u64 GetLogicalFrameIndex() const noexcept
    {
        return LogicalFrameIndex;
    }
    [[nodiscard]] melonDS::u64 GetLastAcceptedLogicalFrameId() const noexcept
    {
        return LastAcceptedLogicalFrameId;
    }
    void SetLastAcceptedLogicalFrameId(melonDS::u64 id) noexcept
    {
        LastAcceptedLogicalFrameId = id;
    }

    [[nodiscard]] melonDS::u32 QueryReflexTimings(
        VkLatencyTimingsFrameReportNV* out, melonDS::u32 maxCount)
    {
        return Reflex.QueryTimings(out, maxCount);
    }

    // Hands the driver an explicit "off" state while the device and swapchain
    // are still alive. Anti-Lag applies state on its next BeginFrame, so the
    // off state only reaches the driver if a frame boundary follows it --
    // hence the End/Begin pair. Doing this after the device drain and then
    // destroying the Reflex semaphore left NVIDIA's driver holding active
    // low-latency state when switching away from Vulkan mid-game.
    void PrepareForTeardown();

    // --- reporting ---------------------------------------------------------

    // Requested / Supported / Enabled / Actual / Reason for both vendors, one
    // line each, so a log excerpt always says what is really running. The
    // device supplies what vkCreateDevice accepted, which is a different
    // question from what the frame path is doing.
    //
    // The presenter appends the pacer's own line after this, because the pacer
    // is not owned here.
    void LogVendorState(
        const melonDS::VulkanDevice& device, const char* context) const;

    // The context string to log with, or nullptr when the effective state has
    // not changed since the last call. Splitting the latch from the logging is
    // what lets the presenter keep one log call that covers vendors and pacer
    // together while a per-frame caller still cannot flood it.
    [[nodiscard]] const char* ConsumeStateChangeContext() noexcept;

private:
    melonDS::VulkanNvidiaReflex Reflex;
    melonDS::VulkanAmdAntiLag AntiLag;
    melonDS::u64 LogicalFrameIndex = 0;
    // Last logical emulated frame whose present WSI accepted. The difference
    // from LogicalFrameIndex is a diagnostic count of logical frames since
    // that accepted present; it never participates in synchronization.
    melonDS::u64 LastAcceptedLogicalFrameId = 0;
    int LoggedReflexMode = -1;
    bool LoggedReflexActive = false;
    bool LoggedAntiLagActive = false;
    bool StateLogged = false;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // MELONPRIME_VULKAN_LATENCY_CONTROLLER_H
