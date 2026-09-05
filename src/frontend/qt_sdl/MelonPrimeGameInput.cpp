#include "MelonPrimeInternal.h"
#include "EmuInstance.h"
#include "MelonPrimeInputProjection.h"
#include "MelonPrimeDirectAimSource.h"
#include "NDS.h"
#include "main.h"
#include "Screen.h"
#include "MelonPrimeDef.h"
#include "MelonPrimePlatformInput.h"
#include "MelonPrimePerfProbe.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeZoomStatus.h"
#include "MelonPrimeInstanceDiagnostics.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>
#include <QCursor>

#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawInputState.h"
#endif

// Unity-owned hook fragments in this file:
// - MelonPrimePatchNativeAimDeltaHookRegisterInjectionVersion.inc
// - MelonPrimePatchNativeAimDeltaHookPostFoldWriteVersion.inc
// - MelonPrimePatchLowLatencyAimHook.inc
// - MelonPrimePatchNativeBipedFireHook.inc
// - MelonPrimePatchNativeZoomToggleHook.inc
// - MelonPrimePatchImmediateInputEdgeOverlay.inc
// - MelonPrimePatchImmediateTransformGateHook.inc
// - MelonPrimePatchWeaponSwitchHook.inc
// - MelonPrimePatchDirectInvocationHook.inc
//
// Keep them under this unity parent. They depend on the local aim/input helper
// scope in this file, including ApplyAim and nearby MelonPrimeCore methods.

namespace MelonPrime {

    // =========================================================================
    // Aim configuration ownership (cold path)
    //
    // Aim-derived values, fixed-point conversion and invalidation live beside
    // the aim state machine. RuntimeConfigSnapshot remains the single config
    // read/clamp transaction; this owner only applies its already-resolved
    // scalar values. None of these functions run in the steady-state aim path.
    // =========================================================================
    void MelonPrimeCore::ApplyAimConfigSnapshot(const AimConfigSnapshot& s)
    {
        m_runtimeAimSensitivity = s.aimSensitivity;
        m_runtimeAimYScale = s.aimYScale;
        m_aimSensiFactor = s.aimSensiFactor;
        m_aimCombinedY = s.aimCombinedY;
        m_aimAdjust = s.aimAdjust;
        RecalcAimFixedPoint();
    }

    void MelonPrimeCore::ReloadAimConfigFromTable(Config::Table& cfg)
    {
        ApplyAimConfigSnapshot(LoadAimConfigSnapshot(cfg));
    }

    void MelonPrimeCore::ApplyRuntimeAimSensitivity(int sensitivity)
    {
        m_runtimeAimSensitivity = std::max(1, sensitivity);
        m_aimSensiFactor =
            static_cast<float>(m_runtimeAimSensitivity) * 0.01f;
        m_aimCombinedY = m_aimSensiFactor * m_runtimeAimYScale;
        RecalcAimFixedPoint();
    }

    void MelonPrimeCore::RecalcAimFixedPoint()
    {
        m_aimFixedScaleX = static_cast<int32_t>(m_aimSensiFactor * AIM_ONE_FP + 0.5f);
        m_aimFixedScaleY = static_cast<int32_t>(m_aimCombinedY * AIM_ONE_FP + 0.5f);
        RecalcAimEffectiveFixedScale();

        if (m_aimAdjust > 0.0f) {
            m_aimFixedAdjust = static_cast<int64_t>(m_aimAdjust * AIM_ONE_FP + 0.5f);
            m_aimFixedSnapThresh = AIM_ONE_FP;
        }
        else {
            m_aimFixedAdjust = 0;
            m_aimFixedSnapThresh = 0;
        }

        // Residuals and pending native delivery were produced with the old
        // scale. The Aim owner invalidates both as one transition.
        ResetAimTransientState();
    }

    void MelonPrimeCore::ApplyAimRuntimeConfig(const RuntimeConfigSnapshot& s)
    {
        m_disableMphAimSmoothing = s.disableMphAimSmoothing;
        m_enableAimAccumulator = s.aimAccumulator;
        m_nativeAimHookMode = s.nativeAimHookMode;
        m_enableNativeAimDeltaHook = s.enableNativeAimDeltaHook;
        m_lowLatencyAimMode = s.lowLatencyAimMode;
        m_moonLikeAimNormalStepQ12 = s.moonLikeAimNormalStepQ12;
        m_moonLikeAimFastStepQ12 = s.moonLikeAimFastStepQ12;
        m_moonLikeAimFastThresholdQ12 = s.moonLikeAimFastThresholdQ12;

        m_zoomAimScaleQ14 = s.zoomAimScaleQ14;
        m_enableZoomAimScale = s.zoomAimScaleEnable;
        // A settings change must not wait for this frame's cached sample.
        m_zoomAimSampledRam = nullptr;
        m_zoomAimSampledFrame = 0xFFFFFFFFu;
        if (!m_enableZoomAimScale) {
            if (m_activeZoomAimScaleQ14 != static_cast<uint32_t>(AIM_ONE_FP)) {
                m_activeZoomAimScaleQ14 = static_cast<uint32_t>(AIM_ONE_FP);
                RecalcAimEffectiveFixedScale();
            }
        }
        else if (m_activeZoomAimScaleQ14 != static_cast<uint32_t>(AIM_ONE_FP)
                 && m_activeZoomAimScaleQ14 != m_zoomAimScaleQ14) {
            m_activeZoomAimScaleQ14 = m_zoomAimScaleQ14;
            RecalcAimEffectiveFixedScale();
        }

        ApplyAimConfigSnapshot(s.aimConfig);
    }

    // =========================================================================
    // UpdateInputStateImpl<kReentrant>
    //
    // Unified implementation for UpdateInputState (kReentrant=false) and
    // UpdateInputStateReentrant (kReentrant=true). if constexpr branches are
    // completely eliminated at compile time — zero overhead vs hand-written
    // duplicates.
    //
    // kReentrant=false (full path):
    //   - UpdateOwnerAndSnapshot : resolves ownership, drains WM_INPUT and
    //                              latches edge state
    //   - reads press mask       : ProjectPressMask from hotPressMask
    //   - reads wheelSteps       : from the Raw Input generation, or the
    //                              generation-tagged Qt fallback mailbox
    //
    // kReentrant=true (re-entrant FrameAdvance path):
    //   - UpdateOwnerAndSnapshotNoEdges : same transaction, no edge latch
    //   - press = 0              : outer-frame press detection preserved
    //   - wheelSteps = 0         : never consumed mid-frame
    // =========================================================================
    template <bool kReentrant>
    FORCE_INLINE void MelonPrimeCore::UpdateInputStateImpl(
        const GuiInputPolicySnapshot& guiPolicy)
    {
        const bool focused = guiPolicy.focused;
        const int requestedCursorMode =
            m_threadBridge.ConsumeCursorModeForEmu();
        if (requestedCursorMode >= 0) {
            isCursorMode = requestedCursorMode != 0;
            SetAimBlockBranchless(AIMBLK_CURSOR_MODE, isCursorMode);
        }
        const uint64_t layoutGeneration =
            m_threadBridge.LayoutGenerationForEmu();
        if (layoutGeneration != m_layoutGenerationSeen) {
            m_layoutGenerationSeen = layoutGeneration;
            m_isLayoutChangePending = true;
            m_threadBridge.ReadCenterForEmu(
                m_aimData.centerX, m_aimData.centerY);
        }
        // Traditional Stylus Mode keeps the panel's DS touch lifecycle and
        // never owns relative input. Its optional direct-aim sub-mode requests
        // capture only for the held touch action, so Raw ownership follows the
        // same request/active split as normal mouse aim.
        const bool captureEligible = InputProjection::ShouldOwnRelativeAimInput(
            guiPolicy.focused,
            guiPolicy.panelAvailable,
            isCursorMode,
            isStylusMode,
            m_enableStylusDirectAimWhileTouching,
            guiPolicy.captureWanted);
        const bool wasInputOwner =
            m_inputSubscription.activeOwner.load(std::memory_order_acquire);
        // A different Raw owner can only publish a reset request for this
        // subscription. Consume it on this EmuThread before taking the local
        // generation snapshot; this keeps generation, cursor sync, and Qt
        // gameplay edge baseline under one writer.
        if (UNLIKELY(!wasInputOwner
                && m_inputSubscription.ConsumeRegistrationReset())) {
            ResetGameplayEdgeBaselines();
        }
        const uint64_t wasInputGeneration = m_inputSubscription.generation;
#ifdef _WIN32
        auto* const rawFilter = m_rawFilter.get();

        // OPT-Z3: Always poll to drain WM_INPUT even when unfocused,
        // preventing message buildup and stale delta accumulation. [FIX-2]
        FrameHotkeyState hk{};
        bool platformInputOwner = false;
        if (rawFilter) {
            void* const currentHwnd = reinterpret_cast<void*>(
                m_threadBridge.WindowHandleForEmu());
            if (currentHwnd != m_cachedHwnd) {
                rawFilter->setRawInputTarget(
                    m_rawInputSubscription,
                    static_cast<HWND>(currentHwnd));
                m_cachedHwnd = currentHwnd;
            }
            if constexpr (kReentrant)
                platformInputOwner = rawFilter->UpdateOwnerAndSnapshotNoEdges(
                    m_rawInputSubscription, captureEligible, hk,
                    m_input.mouseX, m_input.mouseY, m_input.wheelSteps);
            else {
                platformInputOwner = rawFilter->UpdateOwnerAndSnapshot(
                    m_rawInputSubscription, captureEligible, hk,
                    m_input.mouseX, m_input.mouseY, m_input.wheelSteps);
                // P-47: Kernel buffer just drained; no FrameAdvance has occurred yet.
                // LateLatch skips processRawInputBatched on frames with no FrameAdvance.
                m_didFrameAdvanceSinceSnapshot = false;
            }
        }
#endif

#if !defined(_WIN32)
        const bool platformInputOwner =
            PlatformInputOwnerService::Update(
                m_inputSubscription, captureEligible);
#endif
#if defined(_WIN32)
        const bool isInputOwner = platformInputOwner;
#else
        // Update already resolved the process owner for this frame; avoid a
        // second atomic read of the same result on macOS/Linux.
        const bool isInputOwner = platformInputOwner;
#endif
        m_rawAimActiveThisFrame = isInputOwner;
        if (UNLIKELY(m_publishedInputGeneration
                != m_inputSubscription.generation)) {
            m_publishedInputGeneration = m_inputSubscription.generation;
            m_threadBridge.SetInputGenerationFromEmu(
                m_inputSubscription.generation);
        }
        if (wasInputOwner != isInputOwner
            || wasInputGeneration != m_inputSubscription.generation) {
            InstanceDiagnostics::LogInputSubscription(
                emuInstance, &m_inputSubscription,
                static_cast<unsigned long long>(m_inputSubscription.generation),
                isInputOwner);
        }

        if (!focused) {
            m_rawAimActiveThisFrame = false;
            m_directAimAbsoluteAuthorityThisFrame = false;
#if !defined(_WIN32)
            m_warpCursorAfterAimThisFrame = false;
#endif
            m_input.down = 0;
            m_input.press = 0;
            m_input.moveIndex = 0;
            m_input.mouseX = 0;
            m_input.mouseY = 0;
            m_input.wheelSteps = 0;
            m_input.weaponCycleSteps = 0;
            m_snapState = 0;
            // Re-entrant FrameAdvance does not call InputReset before rebuilding
            // the fast DS mask. Release it here so stale non-movement bits
            // cannot survive a focus loss.
            m_inputMaskFast = 0xFFFF;
            return;
        }

#ifdef _WIN32
        // The Raw snapshot is usable for gameplay only after the owner,
        // baseline, and registration generation all agree. Compute this
        // once and share the result with wheel and keyboard projection.
        const bool rawActionReady = isInputOwner
            && hk.baselineReady
            && hk.generation == m_inputSubscription.generation;
#endif

        if constexpr (!kReentrant) {
#ifdef _WIN32
            if (isInputOwner) {
                // Qt remains a fallback producer for cursor mode. While Raw
                // Input owns the registration, consume and discard that
                // mailbox so the same wheel tick cannot be counted twice.
                (void)m_threadBridge.ConsumeWheelForEmu(
                    m_inputSubscription.generation);
                m_input.wheelSteps = rawActionReady ? hk.wheelSteps : 0;
            }
            else {
                m_input.wheelSteps = m_threadBridge.ConsumeWheelForEmu(
                    m_inputSubscription.generation);
            }
#else
            m_input.wheelSteps = m_threadBridge.ConsumeWheelForEmu(
                m_inputSubscription.generation);
#endif
        }
        else {
            m_input.wheelSteps = 0;
        }

        // Qt keyboard/mouse gameplay edges are committed at the guest-frame
        // late latch, independently from inputProcess()'s global emulator
        // command baseline. Wheel never enters this held/pending path, so a
        // no-wheel frame does not load either published wheel binding mask.
        const uint64_t qtGameplayHeld =
            emuInstance->keyHotkeyMask.load(std::memory_order_relaxed);
        uint64_t qtGameplayPressed = 0;
        if constexpr (!kReentrant) {
            uint64_t eventPressed = 0;
            if (UNLIKELY(emuInstance->qtGameplayPressPending.load(
                    std::memory_order_relaxed) != 0)) {
                eventPressed = emuInstance->qtGameplayPressPending.exchange(
                    0, std::memory_order_acq_rel);
            }
            if (m_qtGameplayEdgeNeedsBaseline) {
                m_qtGameplayEdgeNeedsBaseline = false;
            }
            else {
                qtGameplayPressed = qtGameplayHeld
                    & ~m_qtGameplayHotkeyPrevious;
            }
            qtGameplayPressed |= eventPressed;
            m_qtGameplayHotkeyPrevious = qtGameplayHeld;
        }

        uint64_t wheelHotkeyBits = 0;
        m_input.weaponCycleSteps = 0;
        if constexpr (!kReentrant) {
            if (m_input.wheelSteps) {
                wheelHotkeyBits = emuInstance->wheelHotkeyMaskForDelta(
                    m_input.wheelSteps);
                // Edge actions remain coalesced to wheelHotkeyBits. Weapon
                // next/prev is count-sensitive and keeps the signed physical
                // detent magnitude through to its semantic consumer.
                const int64_t signedSteps = m_input.wheelSteps;
                const int64_t rawMagnitude = signedSteps < 0
                    ? -signedSteps : signedSteps;
                const int32_t magnitude = static_cast<int32_t>(
                    std::min<int64_t>(
                        rawMagnitude, std::numeric_limits<int32_t>::max()));
                const uint64_t wheelPressBits =
                    InputProjection::ProjectPressMask(wheelHotkeyBits);
                const uint64_t cyclePressBits = wheelPressBits
                    & (IB_WEAPON_NEXT | IB_WEAPON_PREV);
                if (cyclePressBits == IB_WEAPON_NEXT)
                    m_input.weaponCycleSteps = magnitude;
                else if (cyclePressBits == IB_WEAPON_PREV)
                    m_input.weaponCycleSteps = -magnitude;
            }
        }

#ifdef _WIN32
        // Mouse-wheel bindings are virtual one-frame keys. Raw Input has no VK
        // for wheel ticks, so a ready Raw Input snapshot injects matching bits.
        // The Qt path (!isInputOwner / non-Windows) gets gameplay wheel steps
        // from ThreadBridge. EmuInstance::onMouseWheel independently publishes
        // only emulator-command/accessory pulses.
        // We never OR Raw and Qt wheel pulses into the same gameplay frame.
        // MELONPRIME_WINDOWS_CURSOR_HOTKEY_FALLBACK_V1
        // Raw Input ownership is reserved for captured FPS aim. Cursor-mode
        // screens intentionally release that owner, but their focused window
        // still records keyboard hotkeys in this EmuInstance's Qt snapshot.
        //
        // Select one keyboard source instead of OR-ing Raw and Qt together.
        // This keeps instances isolated and avoids duplicate press edges when
        // active ownership changes.
        uint64_t hotDownMask = qtGameplayHeld;
        [[maybe_unused]] uint64_t hotPressMask = qtGameplayPressed;
        const bool rawOnlyFastPath =
            rawActionReady && m_qtFallbackGameplayMask == 0;
        if (LIKELY(rawOnlyFastPath)) {
            // Default/simple bindings keep the original Raw fast path for
            // both held and pressed state.
            hotDownMask = hk.down;
            hotPressMask = hk.pressed;
        }
        else if (rawActionReady) {
            // Raw owns only exact single-predicate bindings. Canonical
            // chords and unsupported identities stay with Qt, whose held and
            // pressed state is already keyed by the normalized binding
            // identity. Resolve this mixed source once for both projections.
            hotDownMask = (hk.down & m_rawOwnedGameplayMask)
                | (qtGameplayHeld & m_qtFallbackGameplayMask);
            hotPressMask = (hk.pressed & m_rawOwnedGameplayMask)
                | (qtGameplayPressed & m_qtFallbackGameplayMask);
        }
        hotDownMask |= emuInstance->lateJoystick.hotkeyHeld
            | wheelHotkeyBits;
        if constexpr (!kReentrant) {
            hotPressMask |= emuInstance->lateJoystick.hotkeyPressed
                | wheelHotkeyBits;
            m_input.press = InputProjection::ProjectPressMask(hotPressMask);
        } else {
            m_input.press = 0;
        }
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (MelonPrimePerf::IsFrameActive() && rawFilter && isInputOwner)
            MelonPrimePerf::CountInputSource(MelonPrimePerf::InputSource::WinRaw);
#endif
#else
        const uint64_t hotDownMask = qtGameplayHeld
            | emuInstance->lateJoystick.hotkeyHeld | wheelHotkeyBits;
        if constexpr (!kReentrant)
            m_input.press = InputProjection::ProjectPressMask(
                qtGameplayPressed | emuInstance->lateJoystick.hotkeyPressed
                | wheelHotkeyBits);
        else
            m_input.press = 0;
#endif

        const InputProjection::ProjectedDownState downState =
            InputProjection::ProjectDownState(hotDownMask, isStylusMode);
        m_input.down = downState.mask;
        m_input.moveIndex = downState.moveIndex;
        // Track the active mode-specific ScanShoot key separately from the
        // merged IB_SHOOT bit so the Adventure map/user-action pause can drop
        // the Mouse-Left ShootScan contribution while keeping this one.
        m_scanShootKeyDown =
            (downState.modeFlags & InputProjection::PMF_SCAN_SHOOT_DOWN) != 0;
        m_stylusTouchKeyDown =
            (downState.modeFlags & InputProjection::PMF_STYLUS_TOUCH_DOWN) != 0;

#if !defined(_WIN32)
        bool haveMouseDelta = false;
        const ResolvedAimInput resolvedAim = PlatformInput_UpdateMouseDelta(
            MELONPRIME_RAW_FILTER_PTR(this),
            m_inputSubscription,
            isInputOwner,
            &m_threadBridge,
            MELONPRIME_RAW_AIM_WAS_ACTIVE_PTR(this),
            haveMouseDelta,
            m_input.mouseX,
            m_input.mouseY,
            m_aimData.centerX,
            m_aimData.centerY);
        m_rawAimActiveThisFrame = resolvedAim.rawActive;
        m_warpCursorAfterAimThisFrame = resolvedAim.warpAfterAim;
#endif

        // MELONPRIME_DIRECT_AIM_TABLET_MAILBOX_V1
        // Frame projection owns the Raw-vs-DirectAim decision. Exactly one
        // source supplies this frame's aim delta; the two are never summed.
        // The mailbox is consumed only during an eligible, held direct-aim
        // frame. This keeps a stale/idle tablet publication from being
        // observed while the option is enabled but the touch action is not.
        const bool tabletDirectAimFrame =
            InputProjection::ShouldConsumeDirectAimMailbox(
                m_enableStylusDirectAimAllowTabletInput,
                captureEligible,
                m_stylusTouchKeyDown);
        if (UNLIKELY(tabletDirectAimFrame)) {
            int32_t directAimDx = 0;
            int32_t directAimDy = 0;
            const auto directAimSource = static_cast<DirectAimHostSource>(
                m_threadBridge.ConsumeDirectAimForEmu(
                    directAimDx, directAimDy));
            // Resolved per frame, not latched per capture: an absolute source
            // owns the frame only while it is actually moving. A still pen
            // therefore hands the frame straight back to the Raw Mouse path,
            // which is what lets both devices stay usable inside one capture.
            m_directAimAbsoluteAuthorityThisFrame =
                DirectAimSourceIsAbsolute(directAimSource)
                && (directAimDx | directAimDy) != 0;
            if (m_directAimAbsoluteAuthorityThisFrame) {
                m_input.mouseX = directAimDx;
                m_input.mouseY = directAimDy;
#if !defined(_WIN32)
                // An absolute source is its own coordinate signal. Warping the
                // cursor after aim would corrupt the next difference.
                m_warpCursorAfterAimThisFrame = false;
#endif
            }
        }
        else {
            m_directAimAbsoluteAuthorityThisFrame = false;
        }
    }

    HOT_FUNCTION void MelonPrimeCore::UpdateInputState(
        const GuiInputPolicySnapshot& guiPolicy)
    {
        UpdateInputStateImpl<false>(guiPolicy);
    }

    HOT_FUNCTION void MelonPrimeCore::UpdateInputStateReentrant(
        const GuiInputPolicySnapshot& guiPolicy)
    {
        UpdateInputStateImpl<true>(guiPolicy);
    }

    // =========================================================================
    // Input lifecycle/reset ownership (cold/transition path)
    // =========================================================================
    COLD_FUNCTION void MelonPrimeCore::ResetAimTransientState() noexcept
    {
        m_aimResidualX = 0;
        m_aimResidualY = 0;
        m_nativeAimDeltaX = 0;
        m_nativeAimDeltaY = 0;
    }

    COLD_FUNCTION void MelonPrimeCore::ResetImmediateOverlayInputState() noexcept
    {
        m_immediateOverlayPrevActions = 0;
        m_immediateOverlayFrameHeld = 0;
        m_immediateOverlayFramePressed = 0;
        m_immediateOverlayFrameReleased = 0;
        m_immediateOverlayLatchValid = false;
    }

    COLD_FUNCTION void MelonPrimeCore::ResetPostPollOverlayCoordinatorState() noexcept
    {
        m_postPollOverlayLocalPlayerPtr = 0;
    }

    COLD_FUNCTION void MelonPrimeCore::ResetDirectTransformInputState() noexcept
    {
        m_directTransformPendingFrames = 0;
    }

    COLD_FUNCTION void MelonPrimeCore::ResetNativeBipedFireInputState() noexcept
    {
        m_nativeBipedFirePrevHeld = false;
        m_nativeBipedFirePrevAltForm = false;
        m_nativeBipedFireLatchValid = false;
        m_nativeBipedFireFrameHeld = false;
        m_nativeBipedFireFramePressed = false;
        m_nativeBipedFireFrameReleased = false;
    }

    COLD_FUNCTION void MelonPrimeCore::ResetGameplayEdgeBaselines() noexcept
    {
        m_qtGameplayHotkeyPrevious = 0;
        m_qtGameplayEdgeNeedsBaseline = true;
        emuInstance->qtGameplayPressPending.store(
            0, std::memory_order_release);
        // Owner transfer and lifecycle boundaries must not hand direct-aim
        // motion published under the previous epoch to the next one.
        m_threadBridge.ResetDirectAimForEmu();
        m_directAimAbsoluteAuthorityThisFrame = false;
    }

    COLD_FUNCTION void MelonPrimeCore::ResetInputForLifecycleBoundary(
        const InputLifecycleBoundary boundary) noexcept
    {
        ResetGameplayEdgeBaselines();
        // These profiles intentionally preserve the pre-SRP reset subsets.
        // In particular EmuStart and EmuStop remain asymmetric; widening a
        // profile is a behavior change, not an architecture cleanup.
        switch (boundary) {
        case InputLifecycleBoundary::EmuStart:
            ResetPostPollOverlayCoordinatorState();
            ResetAimTransientState();
            ResetImmediateOverlayInputState();
            ResetDirectTransformInputState();
            break;
        case InputLifecycleBoundary::Boot:
            ResetPostPollOverlayCoordinatorState();
            ResetAimTransientState();
            ResetImmediateOverlayInputState();
            ResetDirectTransformInputState();
            ResetNativeBipedFireInputState();
            break;
        case InputLifecycleBoundary::EmuStop:
        case InputLifecycleBoundary::FocusLoss:
            ResetPostPollOverlayCoordinatorState();
            ResetDirectTransformInputState();
            ResetNativeBipedFireInputState();
            break;
        case InputLifecycleBoundary::GameLeave:
            ResetPostPollOverlayCoordinatorState();
            ResetImmediateOverlayInputState();
            ResetDirectTransformInputState();
            ResetNativeBipedFireInputState();
            break;
        case InputLifecycleBoundary::GameJoin:
            ResetPostPollOverlayCoordinatorState();
            ResetImmediateOverlayInputState();
            ResetDirectTransformInputState();
            ResetNativeBipedFireInputState();
#ifdef MELONPRIME_DS
            m_weaponSwitchPending.Clear();
            m_directInvocationPending.Clear();
#endif
            break;
        case InputLifecycleBoundary::SavestateLoad:
            ResetPostPollOverlayCoordinatorState();
            ResetAimTransientState();
            ResetImmediateOverlayInputState();
            ResetDirectTransformInputState();
            ResetNativeBipedFireInputState();
#ifdef MELONPRIME_DS
            m_weaponSwitchPending.Clear();
            m_directInvocationPending.Clear();
#endif
            break;
        }
    }

    // OPT-Z2: Unified move + button mask update.
    //
    //   Previously split across ProcessMoveInputFast() and an inline block,
    //   causing two separate store-load cycles on m_inputMaskFast and code
    //   duplication between HandleInGameLogic and RunFrameHook re-entrant path.
    //
    //   Now: single function, single store to m_inputMaskFast, zero duplication.
    //   Also enables the compiler to keep m_inputMaskFast in a register across
    //   the move LUT lookup and button merge.
    // SnapTap conflict resolution. Outlined as COLD_FUNCTION so the SnapTap-OFF
    // hot path (the dominant case) keeps a tiny inline body and the bit-twiddling
    // here lives off the hot icache region. ProcessMoveAndButtonsFastImpl already
    // marks the !m_snapTapMode branch LIKELY.
    [[nodiscard]] COLD_FUNCTION static uint32_t ResolveSnapTapInput(
        uint32_t curr, uint16_t& snapState) noexcept
    {
        const uint32_t last = snapState & 0xFFu;
        const uint32_t priority = snapState >> 8;
        const uint32_t newPress = curr & ~last;

        const uint32_t conflictFB = ((curr & 0x3u) == 0x3u) ? 0x3u : 0u;
        const uint32_t conflictLR = ((curr & 0xCu) == 0xCu) ? 0xCu : 0u;
        const uint32_t conflict = conflictFB | conflictLR;

        const bool hasNewConflict = (newPress & conflict) != 0;
        const uint32_t updateMask = hasNewConflict ? ~0u : 0u;

        const uint32_t newPriority = (priority & ~(conflict & updateMask)) | (newPress & conflict & updateMask);
        const uint32_t activePriority = newPriority & curr;

        snapState = static_cast<uint16_t>((curr & 0xFFu) | ((activePriority & 0xFFu) << 8));
        return (curr & ~conflict) | (activePriority & conflict);
    }

    template <bool kInputMaskReset>
    FORCE_INLINE void MelonPrimeCore::ProcessMoveAndButtonsFastImpl()
    {
        const uint32_t curr = m_input.moveIndex;
        const uint32_t finalInput = LIKELY(!m_snapTapMode)
            ? curr
            : ResolveSnapTapInput(curr, m_snapState);

        // --- Branchless button merge, driven by the control preset ---
        //
        // Every button here comes from m_presetBindings, which the game-join
        // snapshot derived from the player's own control-preset table. The
        // movement table replaces the fixed D-pad LUT: it is the same single
        // indexed read, just built for the active preset (the left-handed
        // presets move on Y/A/X/B, not the D-pad).
        //
        // Zoom is applied separately by ApplyZoomBindingInput(). Native Biped
        // Fire owns shoot through the post-poll player+0x464 overlay, so it
        // holds the fire button released instead of synthesizing it.
        // PresetButtonBindings declares the DS button masks itself because the
        // INPUT_* enum is not visible from MelonPrime.h. Both are in scope here,
        // so tie them together where the masks are actually consumed.
        static_assert(PresetButtonBindings::BtnA     == (1u << INPUT_A),     "DS button mask drift");
        static_assert(PresetButtonBindings::BtnB     == (1u << INPUT_B),     "DS button mask drift");
        static_assert(PresetButtonBindings::BtnRight == (1u << INPUT_RIGHT), "DS button mask drift");
        static_assert(PresetButtonBindings::BtnLeft  == (1u << INPUT_LEFT),  "DS button mask drift");
        static_assert(PresetButtonBindings::BtnUp    == (1u << INPUT_UP),    "DS button mask drift");
        static_assert(PresetButtonBindings::BtnDown  == (1u << INPUT_DOWN),  "DS button mask drift");
        static_assert(PresetButtonBindings::BtnR     == (1u << INPUT_R),     "DS button mask drift");
        static_assert(PresetButtonBindings::BtnL     == (1u << INPUT_L),     "DS button mask drift");

        const auto& binds = m_presetBindings;
        const uint64_t down = m_input.down;

        // Select-masks rather than branches: 0 or 0xFFFF.
        const uint16_t jumpSel = static_cast<uint16_t>(
            0u - static_cast<uint16_t>(down & 1u));
        const uint16_t fireSel = static_cast<uint16_t>(
            0u - static_cast<uint16_t>(((down >> 1) & 1u)
                                       & static_cast<uint64_t>(!m_enableNativeBipedFire)));

        const uint16_t pressed = static_cast<uint16_t>(
            binds.MoveMask[finalInput & 0xF]
            | (binds.Jump & jumpSel)
            | (binds.Fire & fireSel));

        // The mask is active-low: a set bit means "not pressed". Release
        // everything this function owns, then press what the preset wants, so a
        // stale press from the out-of-game UI path cannot survive into a match.
        const uint16_t owned = static_cast<uint16_t>(
            binds.MoveAll | binds.Jump | binds.Fire);
        if constexpr (kInputMaskReset) {
            m_inputMaskFast = static_cast<uint16_t>(0xFFFFu & ~pressed);
        }
        else {
            m_inputMaskFast = static_cast<uint16_t>((m_inputMaskFast | owned) & ~pressed);
        }
    }

    HOT_FUNCTION void MelonPrimeCore::ProcessMoveAndButtonsFast()
    {
        ProcessMoveAndButtonsFastImpl<false>();
    }

    HOT_FUNCTION void MelonPrimeCore::ProcessMoveAndButtonsFastFromReset()
    {
        ProcessMoveAndButtonsFastImpl<true>();
    }

    // Movement-only update: synthesize the D-pad bits from moveIndex and leave
    // every other DS button released. Used on out-of-game screens (e.g. the
    // Adventure planet/region map) so WASD can move the cursor/ship while cursor
    // mode keeps the mouse driving the touch screen. Fire/jump/zoom are not
    // synthesized here, matching "movement only" intent.
    HOT_FUNCTION void MelonPrimeCore::ProcessMovementOnlyFromReset()
    {
        const uint32_t curr = m_input.moveIndex;
        const uint32_t finalInput = LIKELY(!m_snapTapMode)
            ? curr
            : ResolveSnapTapInput(curr, m_snapState);
        // Everything released except the D-pad directions currently held.
        // Deliberately the fixed menu table, not m_presetBindings: this path is
        // for out-of-game screens, which navigate on the D-pad regardless of the
        // in-game control preset.
        m_inputMaskFast = static_cast<uint16_t>(
            0xFFFFu & ~InputProjection::MenuMoveMask[finalInput & 0xF]);
    }

    // Frame-path entry for both post-poll overlays, called once per frame before
    // NDS::RunFrame(). The ROM action consumer they feed runs once per player per
    // frame, so neither edge may be recomputed inside the hook. See
    // MelonPrimePatchNativeBipedFireHook.inc.
    HOT_FUNCTION void MelonPrimeCore::ApplyPostPollOverlayInput()
    {
        // Both consumers are opt-in. Their disable edges reset feature-owned
        // latches in ApplyRuntimeConfigSnapshot, so the common disabled frame
        // performs no guest pointer read and no latch maintenance.
        if (LIKELY(!m_enableNativeBipedFire
            && !m_enableImmediateInputEdgeOverlay))
            return;

        // Shared stale-edge input: a change of the ROM's local Player* means
        // both latches are describing a different entity and must re-baseline.
        // Read once here so the two latches cannot disagree about the frame.
        uint32_t localPlayerPtr = 0;
#ifdef MELONPRIME_DS
        const uint32_t localPlayerPtrAddr = m_currentRom.hookLocalPlayerPtrGlobal;
        if (localPlayerPtrAddr >= 0x02000000u && localPlayerPtrAddr <= 0x023FFFFCu) {
            if (melonDS::NDS* const nds = emuInstance->getNDS()) {
                std::memcpy(&localPlayerPtr,
                            nds->MainRAM + (localPlayerPtrAddr & 0x3FFFFFu),
                            sizeof(localPlayerPtr));
            }
        }
#endif
        // Only a genuine player->player swap forces a re-baseline. Transitions
        // through 0 (out of match, pointer not yet published) must not, or a
        // single unreadable frame would swallow that frame's press edges; the
        // lifecycle resets already cover entering and leaving a match.
        const bool localPlayerChanged =
            localPlayerPtr != 0
            && m_postPollOverlayLocalPlayerPtr != 0
            && localPlayerPtr != m_postPollOverlayLocalPlayerPtr;
        if (localPlayerPtr != 0)
            m_postPollOverlayLocalPlayerPtr = localPlayerPtr;

        if (m_enableNativeBipedFire)
            UpdateNativeBipedFireInput(localPlayerChanged);
        if (m_enableImmediateInputEdgeOverlay)
            UpdateImmediateInputEdgeOverlayInput(localPlayerChanged);
    }

    // Resolves the generic overlay's edges once per frame, in host-action space.
    //
    // Action space (not binding-bit space) on purpose: the binding masks and
    // m_immediateOverlayPreserveMask are only final later in the frame, and the
    // edge itself is a property of host input, not of what the current control
    // preset happens to bind each action to. The hook expands these actions onto
    // whatever masks it sees.
    //
    // Once per frame, not once per hook entry: the ROM action consumer the hook
    // sits on runs once per player per frame, so a per-entry recompute writes
    // Pressed on the first entry and erases it on the next, before the local
    // player's own entry reads it. Same reason as UpdateNativeBipedFireInput().
    HOT_FUNCTION void MelonPrimeCore::UpdateImmediateInputEdgeOverlayInput(
        bool localPlayerChanged)
    {
        m_immediateOverlayFramePressed = 0;
        m_immediateOverlayFrameReleased = 0;

        if (!m_flags.test(StateFlags::BIT_IN_GAME_INIT)
            || !m_flags.test(StateFlags::BIT_LAST_FOCUSED)
            || (m_aimBlockBits & AIMBLK_NOT_IN_GAME))
        {
            m_immediateOverlayFrameHeld = 0;
            m_immediateOverlayLatchValid = false;
            return;
        }

        const uint64_t down = m_input.down;
        uint8_t held = 0;
        if (down & IB_MOVE_L) held = static_cast<uint8_t>(held | OVA_MOVE_L);
        if (down & IB_MOVE_R) held = static_cast<uint8_t>(held | OVA_MOVE_R);
        if (down & IB_MOVE_F) held = static_cast<uint8_t>(held | OVA_MOVE_F);
        if (down & IB_MOVE_B) held = static_cast<uint8_t>(held | OVA_MOVE_B);
        if (down & IB_SHOOT)  held = static_cast<uint8_t>(held | OVA_FIRE);
        if (down & IB_JUMP)   held = static_cast<uint8_t>(held | OVA_JUMP);
        if (down & IB_ZOOM)   held = static_cast<uint8_t>(held | OVA_ZOOM);

        const uint8_t prev = m_immediateOverlayPrevActions;
        const bool wasValid = m_immediateOverlayLatchValid && !localPlayerChanged;
        m_immediateOverlayPrevActions = held;
        m_immediateOverlayLatchValid = true;
        m_immediateOverlayFrameHeld = held;

        // Re-entry while a button is already held restores Down without
        // manufacturing a Pressed edge.
        if (LIKELY(wasValid)) {
            m_immediateOverlayFramePressed = static_cast<uint8_t>(held & ~prev);
            m_immediateOverlayFrameReleased = static_cast<uint8_t>(~held & prev);
        }
    }

    HOT_FUNCTION void MelonPrimeCore::ApplyZoomBindingInput()
    {
        // Cheap host-side gate before the alt-form MainRAM read. With the
        // native toggle off and the zoom input released, every branch below is
        // a no-op, so IsPlayerAltForm() would dereference isAltForm for a
        // result nothing consumes. The native toggle still needs the released
        // frames (release edge / auto-disable), so it keeps the gate open.
        // Ordering only -- no behavior change.
        const bool zoomDown = IsDown(IB_ZOOM);
        // Both native zoom methods need the released frames too (release edge /
        // auto-disable), so either one keeps the gate open.
        const bool nativeZoom = m_enableNativeZoomToggle || m_enableDirectInvocationZoom;
        if (LIKELY(!nativeZoom && !zoomDown))
            return;

        // In Morph Ball / Alt Form the R input drives Morph Ball Boost, not zoom
        // (zoom does not exist in alt form). The game reads the boost binding
        // (player+0x3B4 = R in the standard presets), so always press the legacy
        // R bit here and bypass the newer zoom methods. The native zoom toggle
        // would otherwise just flip scope state, and the new-method remap would
        // press the zoom binding instead of R — both left R unpressed in alt
        // form, so pressing and releasing R after transforming produced no boost.
        if (IsPlayerAltForm()) {
            // Keep the native press edge in sync so leaving alt form mid hold
            // does not fire a stale toggle on the next biped frame. The latch
            // is shared by both native methods.
            if (nativeZoom)
                m_nativeZoomTogglePrevDown = zoomDown;

            if (zoomDown) {
                m_inputMaskFast = static_cast<uint16_t>(
                    m_inputMaskFast & ~m_presetBindings.MorphBoost);
            }
            return;
        }

        // Spawn invulnerability window: every native method defers to the
        // Standard path for its whole duration. Keep the shared pressed-edge
        // latch in sync so leaving the window mid-hold does not fire a stale
        // toggle, exactly as the alt-form branch above does.
        if (UNLIKELY(nativeZoom && IsLocalPlayerInSpawnInvulnerability())) {
            m_nativeZoomTogglePrevDown = zoomDown;
#ifdef MELONPRIME_DS
            m_nativeZoomPending.Clear();
            m_directInvocationPending.ClearZoom();
#endif
            if (!zoomDown)
                return;
        }
        else if (m_enableNativeZoomToggle) {
            UpdateNativeZoomToggleInput();
            return;
        }
        else if (m_enableDirectInvocationZoom) {
            UpdateDirectInvocationZoomInput();
            return;
        }

        // Redundant after the gate above (both native branches already
        // returned, so zoomDown is true here); kept so the legacy path stays
        // correct on its own if the gate is ever revisited.
        if (!zoomDown)
            return;

        // Always the preset's own zoom button. This used to be a fixed INPUT_R
        // unless ZoomInputMethod opted into the preset table, which only
        // happened to be right on Touch R (Touch L zooms on L, both Dual
        // presets on Select). The snapshot makes the preset value the default,
        // so the opt-in no longer changes anything for this path.
        m_inputMaskFast = static_cast<uint16_t>(
            m_inputMaskFast & ~m_presetBindings.Zoom);
    }

    void MelonPrimeCore::ProcessAimInputStylus(melonDS::NDS* nds)
    {
        if (LIKELY(emuInstance->isTouching)) {
            nds->TouchScreen(emuInstance->touchX, emuInstance->touchY);
            return;
        }

        int touchX = 0;
        int touchY = 0;
        if (m_stylusTouchKeyDown
            && m_threadBridge.ReadStylusPointerForEmu(touchX, touchY))
        {
            nds->TouchScreen(touchX, touchY);
            return;
        }

        nds->ReleaseScreen();
    }

    // P-29b: Cold path for aim reset (aimBlock or layout change).
    // Outlined from ProcessAimInputMouse to keep the hot path branch-free.
    COLD_FUNCTION void MelonPrimeCore::HandleAimEarlyReset()
    {
        if (m_aimResidualX | m_aimResidualY) {
            m_aimResidualX = 0;
            m_aimResidualY = 0;
        }
        m_nativeAimDeltaX = 0;
        m_nativeAimDeltaY = 0;

        if (m_isLayoutChangePending) {
            m_isLayoutChangePending = false;
#ifdef _WIN32
            if (m_rawFilter) m_rawFilter->discardDeltas(m_rawInputSubscription);
#else
            const QPoint center = GetAdjustedCenter();
            m_aimData.centerX = center.x();
            m_aimData.centerY = center.y();
            m_threadBridge.RequestGuiFromEmu(
                MelonPrimeThreadBridge::GuiRequestRecenter);
            PlatformInput_ResetAfterLayoutWarp(
                MELONPRIME_RAW_FILTER_PTR(this), m_inputSubscription, &m_threadBridge);
#endif
        }
    }

    [[nodiscard]] static FORCE_INLINE int64_t ClampAimResidual(
        const int64_t value, const int64_t limit) noexcept
    {
        if (LIKELY(value <= limit && value >= -limit))
            return value;
        return (value < 0) ? -limit : limit;
    }

    // Convert signed fixed-point to an integer by truncating toward zero.
    // A signed arithmetic right shift rounds negative values toward -infinity:
    // e.g. -1 >> 12 == -1. That turns any tiny negative residual into a real
    // -1 aim step and is the source of accumulator-only post-stop drift.
    [[nodiscard]] static FORCE_INLINE int64_t AimFixedToIntTowardZero(
        const int64_t value, const int fractionalBits) noexcept
    {
        return value >= 0
            ? (value >> fractionalBits)
            : -((-value) >> fractionalBits);
    }

    // =========================================================================
    // ApplyAim - branchless deadzone/snap helper for the legacy aim path.
    //
    // Input:  raw     Q14 fixed-point residual for one axis
    //         adjT    deadzone threshold (Q14)
    //         snapT   snap threshold (Q14); 0 disables snap
    //
    // Output: Q14→int16 reduced value (consumed portion)
    //
    // All branches are replaced with arithmetic using sign-extension tricks:
    //   isDeadzone / isSnap produce 0 or -1 masks via SAR 63.
    //   snapVal computes ±1 from the sign bit without a branch.
    //   normVal is the standard >> FracBits reduction.
    // The final OR selects the appropriate output for each region.
    // =========================================================================
    [[nodiscard]] static FORCE_INLINE int16_t ApplyAim(
        int64_t raw, int64_t adjT, int64_t snapT) noexcept
    {
        constexpr int kFracBits = 14; // AIM_FRAC_BITS
        const int64_t sign      = raw >> 63;
        const int64_t absRaw    = (raw ^ sign) - sign;
        const int64_t isDeadzone = (absRaw - adjT) >> 63;
        const int64_t isSnap     = (absRaw - snapT) >> 63;
        const int64_t snapVal    = (1LL ^ sign) - sign;
        const int64_t normVal    = AimFixedToIntTowardZero(raw, kFracBits);
        return static_cast<int16_t>(
            (~isDeadzone & isSnap & snapVal) |
            (~isSnap     & normVal)
        );
    }

    void MelonPrimeCore::RecalcAimEffectiveFixedScale()
    {
        if (m_activeZoomAimScaleQ14 == static_cast<uint32_t>(AIM_ONE_FP)) {
            m_aimEffectiveFixedScaleX = m_aimFixedScaleX;
            m_aimEffectiveFixedScaleY = m_aimFixedScaleY;
            return;
        }

        m_aimEffectiveFixedScaleX = static_cast<int32_t>(
            ZoomStatus::ApplyQ14Scale(m_aimFixedScaleX, m_activeZoomAimScaleQ14));
        m_aimEffectiveFixedScaleY = static_cast<int32_t>(
            ZoomStatus::ApplyQ14Scale(m_aimFixedScaleY, m_activeZoomAimScaleQ14));
    }

    void MelonPrimeCore::UpdateZoomAimEffectiveScale(bool authoritative)
    {
        if (LIKELY(!m_enableZoomAimScale))
            return;

        uint32_t nextScaleQ14 = static_cast<uint32_t>(AIM_ONE_FP);
#ifdef MELONPRIME_DS
        melonDS::NDS* nds = emuInstance ? emuInstance->getNDS() : nullptr;
        const melonDS::u8* ram = nds ? nds->MainRAM : nullptr;
        const uint32_t gameFrame = nds ? nds->NumFrames : 0xFFFFFFFFu;
        // The scope flag is guest state the ROM updates once per emulated
        // frame, so re-reading it for every input event that carried a delta
        // cannot see anything new -- it just puts a guest-memory pointer chase
        // on the aim path. Sample once per frame, exactly like the HUD's zoom
        // reticle does. The native aim-delta hook still forces a re-sample
        // because it runs at the point in the guest's update where the flag is
        // final, so a zoom transition is never picked up a frame late.
        if (!authoritative
            && ram == m_zoomAimSampledRam
            && gameFrame == m_zoomAimSampledFrame)
        {
            return;
        }
        m_zoomAimSampledRam = ram;
        m_zoomAimSampledFrame = gameFrame;

        const ZoomStatus::ScopeState scope =
            ZoomStatus::ReadScopeState(
                ram, m_currentRom.hookLocalPlayerPtrGlobal, m_zoomAimCanZoomCache);
        if (!scope.valid)
            m_zoomAimCanZoomCache = {};
        if (scope.valid && scope.rawVisible)
            nextScaleQ14 = m_zoomAimScaleQ14;
        MelonPrimePerf::RecordZoomAimSample(
            nextScaleQ14 != static_cast<uint32_t>(AIM_ONE_FP),
            nextScaleQ14 != m_activeZoomAimScaleQ14);
#else
        (void)authoritative;
#endif
        if (LIKELY(nextScaleQ14 == m_activeZoomAimScaleQ14))
            return;

        m_activeZoomAimScaleQ14 = nextScaleQ14;
        RecalcAimEffectiveFixedScale();
        // The residual is deliberately kept. It is fractional carry expressed in
        // *output* aim units, not in input units, so a change to the input scale
        // does not invalidate it. Zeroing it here discarded up to a whole output
        // unit of real mouse movement on every scope in and scope out -- which
        // is felt as the aim dropping input at exactly the moment precision
        // matters most, and it is the only thing this setting did differently
        // from the 100% case where the scale never changes.
    }

    // =========================================================================
    // Shared spawn barrier for the native input methods.
    //
    // Spawn (JP1_0 0201212C) restores HP early but keeps initialising camera,
    // model, animation, gun and HUD state well past that point, and the same
    // player runtime update (0200EEAC) then falls through to the player input
    // update (02026374) and its ProcessTouchInput call site. So "HP > 0 and we
    // reached the hook" is satisfied inside the very update that spawned the
    // player, and a native call made there lands on half-initialised state.
    //
    // The boundary is identifiable without any host latch. Spawn stores the
    // hunter's configured invulnerability into player+0xE1, and the same update
    // decrements it exactly once before reaching the input code, so on that
    // first input hook, and only there:
    //
    //     player+0xE1 == (uint8_t)([player+0x404] + 0xE2) - 1
    //
    // The next update reads configured-2, so this costs one input frame rather
    // than the whole invulnerability window. Structure offsets are the same on
    // all seven ROMs, so this needs no per-version address table.
    //
    // See docs: mphCodex Direct-Invocation-Spawn-Freeze-Investigation-JP1_0.md.
    // =========================================================================
    namespace {
        constexpr uint32_t kPlayerSpawnInvulnOffset = 0xE1u;
        constexpr uint32_t kPlayerValuesPtrOffset = 0x404u;
        constexpr uint32_t kPlayerValuesSpawnInvulnOffset = 0xE2u;

        [[nodiscard]] FORCE_INLINE bool SpawnBarrier_IsMainRamRange(
            uint32_t address, uint32_t size) noexcept
        {
            return size != 0
                && address >= 0x02000000u
                && size - 1u <= 0x023FFFFFu - address;
        }
    } // anonymous namespace

    bool MelonPrimeCore::IsFirstPostSpawnInput(
        melonDS::NDS* nds, uint32_t player) const noexcept
    {
        if (!nds || !SpawnBarrier_IsMainRamRange(player + kPlayerValuesPtrOffset, 4u))
            return false;

        const uint32_t values = Read32(nds->MainRAM, player + kPlayerValuesPtrOffset);
        if (!SpawnBarrier_IsMainRamRange(values, kPlayerValuesSpawnInvulnOffset + 2u))
            return false;

        // player+0xE1 is a byte, so the configured value is compared truncated
        // the same way Spawn stores it.
        const uint8_t configured = static_cast<uint8_t>(
            Read16(nds->MainRAM, values + kPlayerValuesSpawnInvulnOffset));
        if (configured == 0)
            return false;

        const uint8_t current = Read8(nds->MainRAM, player + kPlayerSpawnInvulnOffset);
        return current == static_cast<uint8_t>(configured - 1u);
    }

    bool MelonPrimeCore::IsLocalPlayerInSpawnInvulnerability() const noexcept
    {
        if (!m_flags.test(StateFlags::BIT_IN_GAME_INIT))
            return false;

        melonDS::NDS* const nds = emuInstance ? emuInstance->getNDS() : nullptr;
        if (!nds)
            return false;

        const uint32_t addr =
            m_currentRom.playerStructStart
            + static_cast<uint32_t>(m_playerPosition) * Consts::PLAYER_ADDR_INC
            + kPlayerSpawnInvulnOffset;
        if (!SpawnBarrier_IsMainRamRange(addr, 1u))
            return false;

        return Read8(nds->MainRAM, addr) != 0;
    }

    // =========================================================================
    // Hook implementation unity fragments.
    //
    // These are intentionally included after the local aim helpers above. Do not
    // move them to standalone translation units or add them to CMakeLists.txt.
    // =========================================================================
#include "MelonPrimePatchNativeAimDeltaHookRegisterInjectionVersion.inc"
#include "MelonPrimePatchNativeAimDeltaHookPostFoldWriteVersion.inc"
#include "MelonPrimePatchLowLatencyAimHook.inc"
#include "MelonPrimePatchNativeBipedFireHook.inc"
#include "MelonPrimePatchNativeZoomToggleHook.inc"
#include "MelonPrimePatchImmediateInputEdgeOverlay.inc"
#include "MelonPrimePatchImmediateTransformGateHook.inc"
#include "MelonPrimePatchWeaponSwitchHook.inc"
#include "MelonPrimePatchDirectInvocationHook.inc"

    // =========================================================================
    // ProcessAimInputMouse
    //
    // P-17: Sub-pixel residual accumulation.
    // P-18: Dual-path aim pipeline.
    //
    //   Direct path (m_disableMphAimSmoothing = true, ASM patch active):
    //     - No deadzone (DS-side also bypassed by ASM patch)
    //     - >> 12 direct output (4x resolution vs >> 14 << 2)
    //     - Every frame with mouse movement produces output
    //     - ~8 instructions (SAR ×2 + SUB ×2 + test + store ×2)
    //
    //   Legacy path (m_disableMphAimSmoothing = false):
    //     - Deadzone/snap for DS-side compatibility
    //     - P-17 residual accumulation with ApplyAim branchless logic
    //     - ampShift = 0 (DS handles amplification internally)
    // =========================================================================
    HOT_FUNCTION void MelonPrimeCore::ProcessAimInputMouse()
    {
        m_nativeAimDeltaX = 0;
        m_nativeAimDeltaY = 0;
        // Morph Boost already consumed this frame's m_input.mouseX/Y; this
        // routine applies the same sample to aim. // MELONPRIME_MORPH_BOOST_CURRENT_FRAME_RAW_V13
#if !defined(_WIN32)
        const bool warpCursorAfterAim = m_warpCursorAfterAimThisFrame;
#endif

        // P-29b: Combined early-exit gate.
        // Single branch covers both aimBlock (morph/weapon) and layout change.
        // Cold path handles the specifics.
        if (UNLIKELY(m_aimBlockBits | static_cast<uint32_t>(m_isLayoutChangePending))) {
            HandleAimEarlyReset();
            return;
        }

        if (LIKELY(m_flags.test(StateFlags::BIT_LAST_FOCUSED))) {
            const int32_t deltaX = m_input.mouseX;
            const int32_t deltaY = m_input.mouseY;
            const bool hasDeltaX = deltaX != 0;
            const bool hasDeltaY = deltaY != 0;
            const bool hasDelta = hasDeltaX || hasDeltaY;
            // A residual is fractional carry, not autonomous movement. Once the
            // physical mouse stops, do not drain that carry into later frames. Keep
            // it for the next real delta so sub-pixel accumulation still works.
            if (UNLIKELY(!hasDelta)) {
                if (!m_enableAimAccumulator) {
                    m_aimResidualX = 0;
                    m_aimResidualY = 0;
                }
                return;
            }

            // Resolve guest-dependent scale only for input we will consume.
            // Idle fractional carry needs neither a RAM sample nor a scale update;
            // the next delta refreshes it before multiplication. Native hook late
            // input retains its separate authoritative refresh at the hook PC.
            if (UNLIKELY(m_enableZoomAimScale))
                UpdateZoomAimEffectiveScale();

            int64_t resX = m_aimResidualX;
            int64_t resY = m_aimResidualY;

            // P-17: Accumulate into residual (Q14 fixed-point).
            resX += static_cast<int64_t>(deltaX) * m_aimEffectiveFixedScaleX;
            resY += static_cast<int64_t>(deltaY) * m_aimEffectiveFixedScaleY;

            // P-29a: Clamp only when residual escapes the normal range.
            resX = ClampAimResidual(resX, AIM_MAX_RESIDUAL);
            resY = ClampAimResidual(resY, AIM_MAX_RESIDUAL);

            if (AimBypassesDsSmoothing()) {
                // =========================================================
                // P-18a+b: Direct path (ASM patch enabled, or Dual preset)
                //
                // >> 12 = >> 14 then << 2, but in one operation.
                // This preserves 2 extra fractional bits that >> 14 discards,
                // giving 4x finer resolution (minimum output ±1 vs ±4).
                //
                // No deadzone: mouse raw input has zero noise at rest
                // (delta=0 → residual unchanged → output 0).
                // DS-side deadzone is also bypassed -- by the ASM patch on a
                // Touch preset, and by not entering the touch producer at all
                // on a Dual one.
                // =========================================================
                // Only an axis with a fresh raw delta may emit output. This keeps
                // an old residual from the other axis from turning a straight move
                // into a diagonal move. Conversion truncates toward zero so a tiny
                // negative fraction does not become an unwanted -1 step.
                const int16_t outX = hasDeltaX
                    ? static_cast<int16_t>(AimFixedToIntTowardZero(resX, AIM_DIRECT_BITS))
                    : 0;
                const int16_t outY = hasDeltaY
                    ? static_cast<int16_t>(AimFixedToIntTowardZero(resY, AIM_DIRECT_BITS))
                    : 0;

                // Subtract only the portion actually emitted on an active axis.
                if (hasDeltaX)
                    resX -= static_cast<int64_t>(outX) << AIM_DIRECT_BITS;
                if (hasDeltaY)
                    resY -= static_cast<int64_t>(outY) << AIM_DIRECT_BITS;

                if ((outX | outY) == 0) {
                    m_aimResidualX = m_enableAimAccumulator ? resX : 0;
                    m_aimResidualY = m_enableAimAccumulator ? resY : 0;
#if !defined(_WIN32)
                    if (warpCursorAfterAim)
                        m_threadBridge.RequestGuiFromEmu(
                            MelonPrimeThreadBridge::GuiRequestRecenter);
#endif
                    return;
                }

                // Dual control presets never reach the native aim hooks: every
                // one of those hook PCs sits inside the ROM's touch aim branch,
                // which a Dual preset jumps over. Leaving the deltas at zero
                // also keeps the alt-form hook's own zero check from fighting
                // the direct write below.
                if (m_enableNativeAimDeltaHook && LIKELY(m_ptrs.dualAim == nullptr)) {
                    m_nativeAimDeltaX = outX;
                    m_nativeAimDeltaY = outY;
                }
                else {
                    // Direct write fallback — no << ampShift needed.
                    // >> 12 already produces the same scale as the old >> 14 << 2.
                    WriteAimDelta(outX, outY);
                }
            }
            else {
                // =========================================================
                // Legacy path (DS-side smoothing active)
                //
                // Deadzone + snap + P-17 residual accumulation.
                // ampShift = 0 (DS handles amplification internally).
                // =========================================================

                // Ignore an inactive axis when checking the deadzone. Its stored
                // fractional carry must not wake up merely because the other axis moved.
                {
                    const int64_t activeResX = hasDeltaX ? resX : 0;
                    const int64_t activeResY = hasDeltaY ? resY : 0;
                    const int64_t absResX = activeResX < 0 ? -activeResX : activeResX;
                    const int64_t absResY = activeResY < 0 ? -activeResY : activeResY;
                    if (absResX < m_aimFixedAdjust && absResY < m_aimFixedAdjust) {
                        m_aimResidualX = m_enableAimAccumulator ? resX : 0;
                        m_aimResidualY = m_enableAimAccumulator ? resY : 0;
#if !defined(_WIN32)
                        if (warpCursorAfterAim)
                            m_threadBridge.RequestGuiFromEmu(
                                MelonPrimeThreadBridge::GuiRequestRecenter);
#endif
                        return;
                    }
                }

                const int64_t adjT  = m_aimFixedAdjust;
                const int64_t snapT = m_aimFixedSnapThresh;

                const int16_t outX = hasDeltaX ? ApplyAim(resX, adjT, snapT) : 0;
                const int16_t outY = hasDeltaY ? ApplyAim(resY, adjT, snapT) : 0;

                if (hasDeltaX)
                    resX -= static_cast<int64_t>(outX) << AIM_FRAC_BITS;
                if (hasDeltaY)
                    resY -= static_cast<int64_t>(outY) << AIM_FRAC_BITS;

                if ((outX | outY) == 0) {
                    m_aimResidualX = m_enableAimAccumulator ? resX : 0;
                    m_aimResidualY = m_enableAimAccumulator ? resY : 0;
#if !defined(_WIN32)
                    if (warpCursorAfterAim)
                        m_threadBridge.RequestGuiFromEmu(
                            MelonPrimeThreadBridge::GuiRequestRecenter);
#endif
                    return;
                }

                WriteAimDelta(outX, outY);
            }

            // Discard sub-pixel residuals when accumulator is off.
            // Fractional remainder won't carry to the next frame.
            if (!m_enableAimAccumulator) {
                resX = 0;
                resY = 0;
            }
            m_aimResidualX = resX;
            m_aimResidualY = resY;

#if !defined(_WIN32)
            if (warpCursorAfterAim)
                m_threadBridge.RequestGuiFromEmu(
                    MelonPrimeThreadBridge::GuiRequestRecenter);
#endif
            return;
        }

#if !defined(_WIN32)
        const QPoint center = GetAdjustedCenter();
        m_aimData.centerX = center.x();
        m_aimData.centerY = center.y();
        m_threadBridge.RequestGuiFromEmu(
            MelonPrimeThreadBridge::GuiRequestRecenter);
        PlatformInput_ResetPanelAfterWarp(&m_threadBridge);
#endif
        m_isLayoutChangePending = false;
        m_aimResidualX = 0;
        m_aimResidualY = 0;
        m_nativeAimDeltaX = 0;
        m_nativeAimDeltaY = 0;
    }

} // namespace MelonPrime
