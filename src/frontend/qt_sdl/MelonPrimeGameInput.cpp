#include "MelonPrimeInternal.h"
#include "EmuInstance.h"
#include "MelonPrimeInputProjection.h"
#include "NDS.h"
#include "main.h"
#include "Screen.h"
#include "MelonPrimeDef.h"
#include "MelonPrimePlatformInput.h"
#include "MelonPrimePerfProbe.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeZoomStatus.h"
#include "MelonPrimeInstanceDiagnostics.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
//
// Keep them under this unity parent. They depend on the local aim/input helper
// scope in this file, including ApplyAim and nearby MelonPrimeCore methods.

namespace MelonPrime {

    // =========================================================================
    // UpdateInputStateImpl<kReentrant>
    //
    // Unified implementation for UpdateInputState (kReentrant=false) and
    // UpdateInputStateReentrant (kReentrant=true). if constexpr branches are
    // completely eliminated at compile time — zero overhead vs hand-written
    // duplicates.
    //
    // kReentrant=false (full path):
    //   - PollAndSnapshot        : drains WM_INPUT and latches edge state
    //   - reads press mask       : ProjectPressMask from hotPressMask
    //   - reads wheelDelta       : from cached panel (P-3)
    //
    // kReentrant=true (re-entrant FrameAdvance path):
    //   - PollAndSnapshotNoEdges : drains WM_INPUT, no edge latch
    //   - press = 0              : outer-frame press detection preserved
    //   - wheelDelta = 0         : never consumed mid-frame
    // =========================================================================
    template <bool kReentrant>
    FORCE_INLINE void MelonPrimeCore::UpdateInputStateImpl(const bool focused)
    {
        const int requestedCursorMode =
            m_threadBridge.ConsumeCursorModeForEmu();
        if (UNLIKELY(requestedCursorMode >= 0)) {
            isCursorMode = requestedCursorMode != 0;
            SetAimBlockBranchless(AIMBLK_CURSOR_MODE, isCursorMode);
        }
        const uint64_t layoutGeneration =
            m_threadBridge.LayoutGenerationForEmu();
        if (UNLIKELY(layoutGeneration != m_layoutGenerationSeen)) {
            m_layoutGenerationSeen = layoutGeneration;
            m_isLayoutChangePending = true;
            m_threadBridge.ReadCenterForEmu(
                m_aimData.centerX, m_aimData.centerY);
        }
        // Reject cursor mode before the bridge atomics. Menu and license
        // screens therefore avoid both acquire loads on their steady-state path.
        const bool captureEligible = focused
            && !isCursorMode
            && m_threadBridge.PanelAvailableForEmu()
            && m_threadBridge.CaptureWantedForEmu();
        const bool wasInputOwner =
            m_inputSubscription.activeOwner.load(std::memory_order_acquire);
#ifdef _WIN32
        auto* const rawFilter = m_rawFilter.get();

        // OPT-Z3: Always poll to drain WM_INPUT even when unfocused,
        // preventing message buildup and stale delta accumulation. [FIX-2]
        FrameHotkeyState hk{};
        if (LIKELY(rawFilter != nullptr)) {
            rawFilter->setRawInputTarget(
                m_rawInputSubscription,
                reinterpret_cast<HWND>(m_threadBridge.WindowHandleForEmu()));
            rawFilter->UpdateOwner(m_rawInputSubscription, captureEligible);
            if constexpr (kReentrant)
                rawFilter->PollAndSnapshotNoEdges(m_rawInputSubscription, hk, m_input.mouseX, m_input.mouseY);
            else {
                rawFilter->PollAndSnapshot(m_rawInputSubscription, hk, m_input.mouseX, m_input.mouseY);
                // P-47: Kernel buffer just drained; no FrameAdvance has occurred yet.
                // LateLatch skips processRawInputBatched on frames with no FrameAdvance.
                m_didFrameAdvanceSinceSnapshot = false;
            }
        }
#endif

#if !defined(_WIN32)
        PlatformInputOwnerService::Update(m_inputSubscription, captureEligible);
#endif
        const bool isInputOwner =
            m_inputSubscription.activeOwner.load(std::memory_order_acquire);
        if (UNLIKELY(wasInputOwner != isInputOwner)) {
            InstanceDiagnostics::LogInputSubscription(
                emuInstance, &m_inputSubscription,
                static_cast<unsigned long long>(m_inputSubscription.generation),
                isInputOwner);
        }

        if (UNLIKELY(!focused)) {
            m_input.down = 0;
            m_input.press = 0;
            m_input.moveIndex = 0;
            m_input.mouseX = 0;
            m_input.mouseY = 0;
            m_input.wheelDelta = 0;
            m_snapState = 0;
            // Re-entrant FrameAdvance does not call InputReset before rebuilding
            // the fast DS mask. Release it here so stale non-movement bits
            // cannot survive a focus loss.
            m_inputMaskFast = 0xFFFF;
            return;
        }

#ifdef _WIN32
        // MELONPRIME_WINDOWS_CURSOR_HOTKEY_FASTPATH_V2
        // Captured gameplay is the dominant path. Keep Raw Input as the likely
        // fall-through and touch this EmuInstance's Qt snapshot only while its
        // cursor-mode window is focused but does not own captured Raw Input.
        // One runtime branch selects both down and press sources, avoiding two
        // independent owner selections and preventing Raw/Qt edge duplication.
        uint64_t hotDownMask;
        if (LIKELY(isInputOwner)) {
            hotDownMask = hk.down | emuInstance->joyHotkeyMask;
            if constexpr (!kReentrant) {
                m_input.press = InputProjection::ProjectPressMask(
                    hk.pressed | emuInstance->joyHotkeyPress);
            }
        } else {
            hotDownMask = emuInstance->hotkeyMask;
            if constexpr (!kReentrant) {
                m_input.press = InputProjection::ProjectPressMask(
                    emuInstance->hotkeyPress);
            }
        }
        if constexpr (kReentrant)
            m_input.press = 0;
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        if (MelonPrimePerf::IsFrameActive() && rawFilter && isInputOwner)
            MelonPrimePerf::CountInputSource(MelonPrimePerf::InputSource::WinRaw);
#endif
#else
        const uint64_t hotDownMask = emuInstance->hotkeyMask;
        if constexpr (!kReentrant)
            m_input.press = InputProjection::ProjectPressMask(emuInstance->hotkeyPress);
        else
            m_input.press = 0;
#endif

        const InputProjection::ProjectedDownState downState =
            InputProjection::ProjectDownState(hotDownMask);
        m_input.down = downState.mask;
        m_input.moveIndex = downState.moveIndex;
        // Track the V-default ScanShoot key separately from the merged IB_SHOOT bit
        // so the Adventure map/user-action pause can drop the Mouse-Left ShootScan
        // contribution while keeping this one.
        m_scanShootKeyDown = ((hotDownMask >> HK_MetroidScanShoot) & 1ULL) != 0;

#if !defined(_WIN32)
        bool haveMouseDelta = false;
        PlatformInput_UpdateMouseDelta(
            MELONPRIME_RAW_FILTER_PTR(this),
            m_inputSubscription,
            &m_threadBridge,
            MELONPRIME_RAW_AIM_WAS_ACTIVE_PTR(this),
            haveMouseDelta,
            m_input.mouseX,
            m_input.mouseY,
            m_aimData.centerX,
            m_aimData.centerY);
#endif

        if constexpr (!kReentrant)
            m_input.wheelDelta = m_threadBridge.ConsumeWheelForEmu();
        else
            m_input.wheelDelta = 0;
    }

    HOT_FUNCTION void MelonPrimeCore::UpdateInputState(const bool focused)          { UpdateInputStateImpl<false>(focused); }
    HOT_FUNCTION void MelonPrimeCore::UpdateInputStateReentrant(const bool focused) { UpdateInputStateImpl<true>(focused);  }

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

        const uint8_t lutResult = InputProjection::MoveLUT[finalInput & 0xF];
        uint16_t mask;
        if constexpr (kInputMaskReset) {
            mask = 0xFF0Fu | (static_cast<uint16_t>(lutResult) & 0x00F0u);
        }
        else {
            mask = (m_inputMaskFast & 0xFF0Fu) | (static_cast<uint16_t>(lutResult) & 0x00F0u);
        }

        // --- Branchless button merge (B/L) ---
        // Zoom is preset-dependent and is applied by ApplyZoomBindingInput().
        // Native Biped Fire owns shoot via the ARM9 fire-edge hook, so it
        // leaves INPUT_L released instead of synthesizing the legacy fire input.
        const uint16_t modBits = m_enableNativeBipedFire
            ? static_cast<uint16_t>(1u << INPUT_B)
            : static_cast<uint16_t>((1u << INPUT_B) | (1u << INPUT_L));
        const uint64_t nd = ~m_input.down;
        const uint16_t bBit = static_cast<uint16_t>(((nd >> 0) & 1u) << INPUT_B);
        const uint16_t lBit = static_cast<uint16_t>(((nd >> 1) & 1u) << INPUT_L);
        const uint16_t nativeFireMask = m_enableNativeBipedFire ? 0u : lBit;
        m_inputMaskFast = static_cast<uint16_t>((mask & ~modBits) | bBit | nativeFireMask);
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
        const uint8_t lutResult = InputProjection::MoveLUT[finalInput & 0xF];
        // 0xFF0F = all non-D-pad bits released; OR in the released D-pad bits so
        // only currently-held directions stay pressed (cleared).
        m_inputMaskFast = static_cast<uint16_t>(0xFF0Fu | (static_cast<uint16_t>(lutResult) & 0x00F0u));
    }

    HOT_FUNCTION void MelonPrimeCore::ApplyBipedFireInput()
    {
        // m_native_BipedFire{Pending,DirectActive} are kept false elsewhere when
        // the feature is OFF: ApplyConfigReload clears them on toggle-off, game
        // exit / focus loss / game-join init also clear them, and they default
        // to false on construction. Skip the per-frame redundant resets here.
        if (m_enableNativeBipedFire)
            UpdateNativeBipedFireInput();
    }

    HOT_FUNCTION void MelonPrimeCore::ApplyZoomBindingInput()
    {
        // In Morph Ball / Alt Form the R input drives Morph Ball Boost, not zoom
        // (zoom does not exist in alt form). The game reads the boost binding
        // (player+0x3B4 = R in the standard presets), so always press the legacy
        // R bit here and bypass the newer zoom methods. The native zoom toggle
        // would otherwise just flip scope state, and the new-method remap would
        // press the zoom binding instead of R — both left R unpressed in alt
        // form, so pressing and releasing R after transforming produced no boost.
        if (IsPlayerAltForm()) {
            // Keep the native-toggle press edge in sync so leaving alt form mid
            // hold does not fire a stale toggle on the next biped frame.
            if (m_enableNativeZoomToggle)
                m_nativeZoomTogglePrevDown = IsDown(IB_ZOOM);

            if (IsDown(IB_ZOOM))
                m_inputMaskFast = static_cast<uint16_t>(m_inputMaskFast & ~(1u << INPUT_R));
            return;
        }

        if (m_enableNativeZoomToggle) {
            UpdateNativeZoomToggleInput();
            return;
        }

        if (!IsDown(IB_ZOOM))
            return;

        uint16_t zoomMask = static_cast<uint16_t>(1u << INPUT_R);

        if (m_enableNewZoomInputMethod && m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
            const uint16_t boundMask = static_cast<uint16_t>(m_bindingZoom & 0x0FFFu);
            if (boundMask != 0)
                zoomMask = boundMask;
        }

        m_inputMaskFast = static_cast<uint16_t>(m_inputMaskFast & ~zoomMask);
    }

    void MelonPrimeCore::ProcessAimInputStylus(melonDS::NDS* nds)
    {
        if (LIKELY(emuInstance->isTouching)) {
            nds->TouchScreen(emuInstance->touchX, emuInstance->touchY);
        }
        else {
            nds->ReleaseScreen();
        }
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

    void MelonPrimeCore::UpdateZoomAimEffectiveScale()
    {
        if (LIKELY(!m_enableZoomAimScale))
            return;

        uint32_t nextScaleQ14 = static_cast<uint32_t>(AIM_ONE_FP);
#ifdef MELONPRIME_DS
        melonDS::NDS* nds = emuInstance ? emuInstance->getNDS() : nullptr;
        const melonDS::u8* ram = nds ? nds->MainRAM : nullptr;
        const ZoomStatus::ScopeState scope =
            ZoomStatus::ReadScopeState(
                ram, m_currentRom.hookLocalPlayerPtrGlobal, m_zoomAimCanZoomCache);
        if (!scope.valid)
            m_zoomAimCanZoomCache = {};
        if (scope.valid && scope.rawVisible)
            nextScaleQ14 = m_zoomAimScaleQ14;
#endif
        if (LIKELY(nextScaleQ14 == m_activeZoomAimScaleQ14))
            return;

        m_activeZoomAimScaleQ14 = nextScaleQ14;
        RecalcAimEffectiveFixedScale();
        m_aimResidualX = 0;
        m_aimResidualY = 0;
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
#if !defined(_WIN32)
        const bool warpCursorAfterAim =
            PlatformInput_ShouldWarpCursorAfterAim(
                MELONPRIME_RAW_FILTER_PTR(this));
#endif

        // P-29b: Combined early-exit gate.
        // Single branch covers both aimBlock (morph/weapon) and layout change.
        // Cold path handles the specifics.
        if (UNLIKELY(m_aimBlockBits | static_cast<uint32_t>(m_isLayoutChangePending))) {
            HandleAimEarlyReset();
            return;
        }

        if (LIKELY(m_flags.test(StateFlags::BIT_LAST_FOCUSED))) {
#if defined(__APPLE__)
            if (m_threadBridge.CaptureWantedForEmu())
                m_threadBridge.RequestGuiFromEmu(
                    MelonPrimeThreadBridge::GuiRequestRecenter);
#endif
            const int32_t deltaX = m_input.mouseX;
            const int32_t deltaY = m_input.mouseY;
            const bool hasDeltaX = deltaX != 0;
            const bool hasDeltaY = deltaY != 0;
            const bool hasDelta = hasDeltaX || hasDeltaY;
            int64_t resX = m_aimResidualX;
            int64_t resY = m_aimResidualY;
            if (UNLIKELY(m_enableZoomAimScale) && (hasDelta || ((resX | resY) != 0))) {
                UpdateZoomAimEffectiveScale();
                resX = m_aimResidualX;
                resY = m_aimResidualY;
            }

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

            // P-17: Accumulate into residual (Q14 fixed-point).
            resX += static_cast<int64_t>(deltaX) * m_aimEffectiveFixedScaleX;
            resY += static_cast<int64_t>(deltaY) * m_aimEffectiveFixedScaleY;

            // P-29a: Clamp only when residual escapes the normal range.
            resX = ClampAimResidual(resX, AIM_MAX_RESIDUAL);
            resY = ClampAimResidual(resY, AIM_MAX_RESIDUAL);

            if (m_disableMphAimSmoothing) {
                // =========================================================
                // P-18a+b: Direct path (ASM patch enabled)
                //
                // >> 12 = >> 14 then << 2, but in one operation.
                // This preserves 2 extra fractional bits that >> 14 discards,
                // giving 4x finer resolution (minimum output ±1 vs ±4).
                //
                // No deadzone: mouse raw input has zero noise at rest
                // (delta=0 → residual unchanged → output 0).
                // DS-side deadzone is also bypassed by the ASM patch.
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

                if (m_enableNativeAimDeltaHook) {
                    m_nativeAimDeltaX = outX;
                    m_nativeAimDeltaY = outY;
                }
                else {
                    // Direct write fallback — no << ampShift needed.
                    // >> 12 already produces the same scale as the old >> 14 << 2.
                    *m_ptrs.aimX = static_cast<uint16_t>(outX);
                    *m_ptrs.aimY = static_cast<uint16_t>(outY);
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

                *m_ptrs.aimX = static_cast<uint16_t>(outX);
                *m_ptrs.aimY = static_cast<uint16_t>(outY);
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
