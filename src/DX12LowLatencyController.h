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

#ifndef DX12_LOW_LATENCY_CONTROLLER_H
#define DX12_LOW_LATENCY_CONTROLLER_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <cstdint>

#include "DX12AmdAntiLag2.h"
#include "DX12Common.h"
#include "DX12IntelXeLL.h"
#include "DX12LowLatencyPacing.h"
#include "DX12NvidiaReflex.h"

namespace melonDS
{

// process-service: the sole owner of the DX12 vendor low-latency state
// machines (NVIDIA Reflex, AMD Anti-Lag 2, Intel XeLL) and of the pacing
// authority derived from them.
//
// Why this is not owned by DX12Renderer: the three vendor sessions are
// D3D12-device scoped, exactly like DX12Context, and their markers must
// bracket events that belong to three different subsystems -- input and
// simulation on the emulation thread, render submission inside the renderer,
// and the actual IDXGISwapChain::Present inside the presenter. A renderer that
// owned them would have to re-export every one of those as a public method,
// which is how the pre-refactor tree ended up mediating Present markers
// through Screen.cpp.
//
// Callers speak only in frame phases. No vendor object, NVAPI handle or XeLL
// context leaves this class, and nothing here knows what a Renderer3D, a
// swapchain or a Qt panel is.
//
// Threading: MelonPrimeDS drives the whole DX12 frame -- emulation, render
// submission and Present -- from the emulation thread, so every entry point
// below is called from that one thread, in frame-phase order. This class adds
// no locking of its own; the vendor wrappers it owns have the same contract.
class DX12LowLatencyController
{
public:
    // Renderer-owned configuration, forwarded verbatim from RendererSettings.
    // Values, not vendor state: the caller supplies what the user asked for and
    // the controller decides what is actually applicable.
    struct Settings
    {
        int NvidiaReflexMode = 0;
        bool AmdAntiLag2Enabled = false;
        bool IntelXeLLEnabled = false;
        int IntelXeLLPacingPolicy = 0;
    };

    // Some XeLL transitions may only be applied while the render queue is
    // idle. The controller must not know which class owns that queue, so the
    // lifecycle owner installs this hook. Returning false means "not idle";
    // the transition is then skipped rather than applied unsafely.
    using QueueIdleFn = bool (*)(void* userData);

    static DX12LowLatencyController& Get() noexcept;

    // Null unless a DX12 backend session is live. Frame-phase callers use this
    // so a Software or Vulkan frame costs one pointer test.
    [[nodiscard]] static DX12LowLatencyController* GetIfActive() noexcept;

    DX12LowLatencyController(const DX12LowLatencyController&) = delete;
    DX12LowLatencyController& operator=(const DX12LowLatencyController&) = delete;

    // ---- Session lifecycle -------------------------------------------------

    void SetQueueIdleHook(QueueIdleFn hook, void* userData) noexcept
    {
        QueueIdleHook = hook;
        QueueIdleUserData = userData;
    }

    void Initialize(ID3D12Device* device, u32 vendorId);

    // Closes any frame still open, then releases the vendor sessions. Safe to
    // call when never initialized.
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept { return Initialized; }

    // ---- Configuration -----------------------------------------------------

    void ApplySettings(const Settings& settings);

    // Discrete host-frame-interval transition. Applied only while XeLL owns
    // the frame cap; otherwise the request is cleared to "no cap".
    void UpdateFrameCap(std::uint32_t minimumIntervalUs);

    // ---- Frame phases ------------------------------------------------------
    //
    // One emulated frame, in order:
    //   BeginFrame -> MarkInputSample -> (input polling) -> MarkSimulationStart
    //   -> BeginRenderSubmit/EndRenderSubmit -> EndRenderPhase
    //   -> BeginPresent/EndPresent -> FinishFrame
    //
    // BeginFrame is where the vendor sleep happens, so it must sit immediately
    // before late input sampling.
    void BeginFrame(u64 logicalFrameId);
    void MarkInputSample();
    void MarkSimulationStart();
    void BeginRenderSubmit();
    void EndRenderSubmit();
    void EndRenderPhase();
    void BeginPresent();
    void EndPresent();
    void FinishFrame();

    // ---- Pacing authority --------------------------------------------------

    [[nodiscard]] DX12LowLatencyPacingDecision GetPacingDecision() const noexcept;
    [[nodiscard]] bool ShouldBypassHostLimiter() const noexcept
    {
        return GetPacingDecision().BypassHostLimiter;
    }
    [[nodiscard]] bool ShouldBypassPresentWait() const noexcept
    {
        return GetPacingDecision().BypassPresentWait;
    }
    [[nodiscard]] bool IsIntelXeLLActive() const noexcept
    {
        return IntelXeLL.IsActive();
    }

private:
    DX12LowLatencyController() = default;

    void LogPacingStateIfChanged();
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    void ReportReflexLatencyTimings();
#endif

    DX12AmdAntiLag2 AmdAntiLag2;
    DX12IntelXeLL IntelXeLL;
    DX12NvidiaReflex NvidiaReflex;
    DX12IntelXeLLPacingPolicy IntelXeLLPacingPolicy =
        DX12IntelXeLLPacingPolicy::Compatibility;
    std::uint32_t IntelXeLLRequestedIntervalUs = 0;
    DX12LowLatencyPacingDecision LastLoggedPacingDecision{};
    QueueIdleFn QueueIdleHook = nullptr;
    void* QueueIdleUserData = nullptr;
    bool PacingDecisionLogged = false;
    bool Initialized = false;
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    int ReflexLatencyTimingCountdown = 0;
#endif
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_LOW_LATENCY_CONTROLLER_H
