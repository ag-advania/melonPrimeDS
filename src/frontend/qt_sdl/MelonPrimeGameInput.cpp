#include "MelonPrimeInternal.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "main.h"
#include "Screen.h"
#include "MelonPrimeDef.h"

#include <utility>
#include <algorithm>  // std::clamp (P-29a)
#include <QCursor>

#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawInputState.h"
#endif

namespace MelonPrime {

    alignas(64) static constexpr std::array<uint8_t, 16> MoveLUT = {
        0xF0, 0xB0, 0x70, 0xF0,  0xD0, 0x90, 0x50, 0xD0,
        0xE0, 0xA0, 0x60, 0xE0,  0xF0, 0xB0, 0x70, 0xF0,
    };

    // V6: Direct hotkey-bit projection.
    //
    // UpdateInputState() runs every frame, so avoid generic lambda + table walks
    // when the relevant Metroid hotkey IDs are compile-time constants.
    // We first merge RawInput/joy masks into one uint64_t, then project that
    // bitset directly into IB_* bits with branchless 0/1 multipliers.
    [[nodiscard]] FORCE_INLINE uint64_t ProjectDownMask(const uint64_t hotMask) noexcept {
        uint64_t down = 0;
        down |= ((hotMask >> HK_MetroidMoveForward)        & 1ULL) * IB_MOVE_F;
        down |= ((hotMask >> HK_MetroidMoveBack)           & 1ULL) * IB_MOVE_B;
        down |= ((hotMask >> HK_MetroidMoveLeft)           & 1ULL) * IB_MOVE_L;
        down |= ((hotMask >> HK_MetroidMoveRight)          & 1ULL) * IB_MOVE_R;
        down |= ((hotMask >> HK_MetroidJump)               & 1ULL) * IB_JUMP;
        down |= ((hotMask >> HK_MetroidZoom)               & 1ULL) * IB_ZOOM;
        down |= ((hotMask >> HK_MetroidWeaponCheck)        & 1ULL) * IB_WEAPON_CHECK;
        down |= ((hotMask >> HK_MetroidHoldMorphBallBoost) & 1ULL) * IB_MORPH_BOOST;
        down |= ((hotMask >> HK_MetroidMenu)               & 1ULL) * IB_MENU;
        down |= ((((hotMask >> HK_MetroidShootScan) |
                   (hotMask >> HK_MetroidScanShoot))       & 1ULL) * IB_SHOOT);
        return down;
    }

    [[nodiscard]] FORCE_INLINE uint64_t ProjectPressMask(const uint64_t hotMask) noexcept {
        uint64_t press = 0;
        press |= ((hotMask >> HK_MetroidMorphBall)      & 1ULL) * IB_MORPH;
        press |= ((hotMask >> HK_MetroidScanVisor)      & 1ULL) * IB_SCAN_VISOR;
        press |= ((hotMask >> HK_MetroidUIOk)           & 1ULL) * IB_UI_OK;
        press |= ((hotMask >> HK_MetroidUILeft)         & 1ULL) * IB_UI_LEFT;
        press |= ((hotMask >> HK_MetroidUIRight)        & 1ULL) * IB_UI_RIGHT;
        press |= ((hotMask >> HK_MetroidUIYes)          & 1ULL) * IB_UI_YES;
        press |= ((hotMask >> HK_MetroidUINo)           & 1ULL) * IB_UI_NO;
        press |= ((hotMask >> HK_MetroidWeaponBeam)     & 1ULL) * IB_WEAPON_BEAM;
        press |= ((hotMask >> HK_MetroidWeaponMissile)  & 1ULL) * IB_WEAPON_MISSILE;
        press |= ((hotMask >> HK_MetroidWeapon1)        & 1ULL) * IB_WEAPON_1;
        press |= ((hotMask >> HK_MetroidWeapon2)        & 1ULL) * IB_WEAPON_2;
        press |= ((hotMask >> HK_MetroidWeapon3)        & 1ULL) * IB_WEAPON_3;
        press |= ((hotMask >> HK_MetroidWeapon4)        & 1ULL) * IB_WEAPON_4;
        press |= ((hotMask >> HK_MetroidWeapon5)        & 1ULL) * IB_WEAPON_5;
        press |= ((hotMask >> HK_MetroidWeapon6)        & 1ULL) * IB_WEAPON_6;
        press |= ((hotMask >> HK_MetroidWeaponSpecial)  & 1ULL) * IB_WEAPON_SPECIAL;
        press |= ((hotMask >> HK_MetroidWeaponNext)     & 1ULL) * IB_WEAPON_NEXT;
        press |= ((hotMask >> HK_MetroidWeaponPrevious) & 1ULL) * IB_WEAPON_PREV;
        return press;
    }

    HOT_FUNCTION void MelonPrimeCore::UpdateInputState()
    {
#ifdef _WIN32
        auto* const rawFilter = m_rawFilter.get();

        // OPT-Z3: PollAndSnapshot -- merged Poll + snapshot into single call.
        // Always runs to drain WM_INPUT messages even when unfocused,
        // preventing message buildup and stale delta accumulation.
        FrameHotkeyState hk{};
        if (rawFilter) {
            rawFilter->PollAndSnapshot(hk, m_input.mouseX, m_input.mouseY);
        }

        if (!isFocused) { // [FIX-2] moved after poll + clear stale state
            m_input.down = 0;
            m_input.press = 0;
            m_input.moveIndex = 0;
            m_input.mouseX = 0;
            m_input.mouseY = 0;
            m_input.wheelDelta = 0;
            return;
        }
#endif

#ifdef _WIN32
        const uint64_t hotDownMask = hk.down | emuInstance->joyHotkeyMask;
        const uint64_t hotPressMask = hk.pressed | emuInstance->joyHotkeyPress;
#else
        const uint64_t hotDownMask = emuInstance->hotkeyMask;
        const uint64_t hotPressMask = emuInstance->hotkeyPress;
#endif

        const uint64_t down = ProjectDownMask(hotDownMask);
        const uint64_t press = ProjectPressMask(hotPressMask);

        m_input.down = down;
        m_input.press = press;
        m_input.moveIndex = static_cast<uint32_t>((down >> 6) & 0xF);

#if !defined(_WIN32)
        const QPoint currentPos = QCursor::pos();
        m_input.mouseX = currentPos.x() - m_aimData.centerX;
        m_input.mouseY = currentPos.y() - m_aimData.centerY;
#endif

        {
            // P-3: Use cached panel pointer (avoids emuInstance->getMainWindow()->panel chase)
            m_input.wheelDelta = m_cachedPanel ? m_cachedPanel->getDelta() : 0;
        }
    }

    HOT_FUNCTION void MelonPrimeCore::UpdateInputStateReentrant()
    {
#ifdef _WIN32
        auto* const rawFilter = m_rawFilter.get();

        // Re-entrant path only needs current down-state + mouse deltas.
        // Use the no-edge snapshot so outer-frame press detection is preserved.
        FrameHotkeyState hk{};
        if (rawFilter) {
            rawFilter->PollAndSnapshotNoEdges(hk, m_input.mouseX, m_input.mouseY);
        }

        if (!isFocused) {
            m_input.down = 0;
            m_input.press = 0;
            m_input.moveIndex = 0;
            m_input.mouseX = 0;
            m_input.mouseY = 0;
            m_input.wheelDelta = 0;
            return;
        }
#endif

#ifdef _WIN32
        const uint64_t hotDownMask = hk.down | emuInstance->joyHotkeyMask;
#else
        const uint64_t hotDownMask = emuInstance->hotkeyMask;
#endif

        const uint64_t down = ProjectDownMask(hotDownMask);

        m_input.down = down;
        m_input.press = 0;
        m_input.moveIndex = static_cast<uint32_t>((down >> 6) & 0xF);

#if !defined(_WIN32)
        const QPoint currentPos = QCursor::pos();
        m_input.mouseX = currentPos.x() - m_aimData.centerX;
        m_input.mouseY = currentPos.y() - m_aimData.centerY;
#endif

        // Re-entrant path never consumes wheel / press-triggered UI actions.
        m_input.wheelDelta = 0;
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
    HOT_FUNCTION void MelonPrimeCore::ProcessMoveAndButtonsFast()
    {
        const uint32_t curr = m_input.moveIndex;
        uint32_t finalInput;

        if (LIKELY(!m_flags.test(StateFlags::BIT_SNAP_TAP))) {
            finalInput = curr;
        }
        else {
            const uint32_t last = m_snapState & 0xFFu;
            const uint32_t priority = m_snapState >> 8;
            const uint32_t newPress = curr & ~last;

            const uint32_t conflictFB = ((curr & 0x3u) == 0x3u) ? 0x3u : 0u;
            const uint32_t conflictLR = ((curr & 0xCu) == 0xCu) ? 0xCu : 0u;
            const uint32_t conflict = conflictFB | conflictLR;

            const bool hasNewConflict = (newPress & conflict) != 0;
            const uint32_t updateMask = hasNewConflict ? ~0u : 0u;

            const uint32_t newPriority = (priority & ~(conflict & updateMask)) | (newPress & conflict & updateMask);
            const uint32_t activePriority = newPriority & curr;

            m_snapState = static_cast<uint16_t>((curr & 0xFFu) | ((activePriority & 0xFFu) << 8));
            finalInput = (curr & ~conflict) | (activePriority & conflict);
        }

        const uint8_t lutResult = MoveLUT[finalInput & 0xF];
        uint16_t mask = (m_inputMaskFast & 0xFF0Fu) | (static_cast<uint16_t>(lutResult) & 0x00F0u);

        // --- Branchless button merge (B/L/R) ---
        constexpr uint16_t kModBits = (1u << INPUT_B) | (1u << INPUT_L) | (1u << INPUT_R);
        const uint64_t nd = ~m_input.down;
        const uint16_t bBit = static_cast<uint16_t>(((nd >> 0) & 1u) << INPUT_B);
        const uint16_t lBit = static_cast<uint16_t>(((nd >> 1) & 1u) << INPUT_L);
        const uint16_t rBit = static_cast<uint16_t>(((nd >> 2) & 1u) << INPUT_R);
        m_inputMaskFast = (mask & ~kModBits) | bBit | lBit | rBit;
    }

    void MelonPrimeCore::ProcessAimInputStylus()
    {
        if (LIKELY(emuInstance->isTouching)) {
            emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
        }
        else {
            emuInstance->getNDS()->ReleaseScreen();
        }
    }

    // P-29b: Cold path for aim reset (aimBlock or layout change).
    // Outlined from ProcessAimInputMouse to keep the hot path branch-free.
    COLD_FUNCTION void MelonPrimeCore::HandleAimEarlyReset()
    {
        m_aimResidualX = 0;
        m_aimResidualY = 0;

        if (m_isLayoutChangePending) {
            m_isLayoutChangePending = false;
#ifdef _WIN32
            if (m_rawFilter) m_rawFilter->discardDeltas();
#endif
        }
    }

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
    //     - P-17 residual accumulation with apply_aim branchless logic
    //     - ampShift = 0 (DS handles amplification internally)
    // =========================================================================
    HOT_FUNCTION void MelonPrimeCore::ProcessAimInputMouse()
    {
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

            // P-17: Accumulate into residual (Q14 fixed-point).
            m_aimResidualX += static_cast<int64_t>(deltaX) * m_aimFixedScaleX;
            m_aimResidualY += static_cast<int64_t>(deltaY) * m_aimFixedScaleY;

            // P-29a: Branchless residual clamp (CMOV on x86, CSEL on ARM).
            // Replaces 4 conditional branches with branchless min/max.
            m_aimResidualX = std::clamp(m_aimResidualX, -AIM_MAX_RESIDUAL, AIM_MAX_RESIDUAL);
            m_aimResidualY = std::clamp(m_aimResidualY, -AIM_MAX_RESIDUAL, AIM_MAX_RESIDUAL);

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
                const int16_t outX = static_cast<int16_t>(m_aimResidualX >> AIM_DIRECT_BITS);
                const int16_t outY = static_cast<int16_t>(m_aimResidualY >> AIM_DIRECT_BITS);

                // Subtract consumed integer portion; fractional remainder carries over.
                m_aimResidualX -= static_cast<int64_t>(outX) << AIM_DIRECT_BITS;
                m_aimResidualY -= static_cast<int64_t>(outY) << AIM_DIRECT_BITS;

                if ((outX | outY) == 0) return;

                PREFETCH_WRITE(m_ptrs.aimX);
                PREFETCH_WRITE(m_ptrs.aimY);

                // Direct write — no << ampShift needed.
                // >> 12 already produces the same scale as the old >> 14 << 2.
                *m_ptrs.aimX = static_cast<uint16_t>(outX);
                *m_ptrs.aimY = static_cast<uint16_t>(outY);
            }
            else {
                // =========================================================
                // Legacy path (DS-side smoothing active)
                //
                // Deadzone + snap + P-17 residual accumulation.
                // ampShift = 0 (DS handles amplification internally).
                // =========================================================

                // Early exit if both residuals are in deadzone.
                {
                    const int64_t absResX = m_aimResidualX < 0 ? -m_aimResidualX : m_aimResidualX;
                    const int64_t absResY = m_aimResidualY < 0 ? -m_aimResidualY : m_aimResidualY;
                    if (absResX < m_aimFixedAdjust && absResY < m_aimFixedAdjust)
                        return;
                }

                PREFETCH_WRITE(m_ptrs.aimX);
                PREFETCH_WRITE(m_ptrs.aimY);

                const int64_t adjT = m_aimFixedAdjust;
                const int64_t snapT = m_aimFixedSnapThresh;

                auto apply_aim = [adjT, snapT](int64_t raw) -> int16_t {
                    const int64_t sign = raw >> 63;
                    const int64_t absRaw = (raw ^ sign) - sign;

                    const int64_t isDeadzone = (absRaw - adjT) >> 63;
                    const int64_t isSnap = (absRaw - snapT) >> 63;

                    const int64_t snapVal = (1LL ^ sign) - sign;
                    const int64_t normVal = raw >> AIM_FRAC_BITS;

                    return static_cast<int16_t>(
                        (~isDeadzone & isSnap & snapVal) |
                        (~isSnap & normVal)
                        );
                    };

                const int16_t outX = apply_aim(m_aimResidualX);
                const int16_t outY = apply_aim(m_aimResidualY);

                m_aimResidualX -= static_cast<int64_t>(outX) << AIM_FRAC_BITS;
                m_aimResidualY -= static_cast<int64_t>(outY) << AIM_FRAC_BITS;

                if ((outX | outY) == 0) return;

                *m_ptrs.aimX = static_cast<uint16_t>(outX);
                *m_ptrs.aimY = static_cast<uint16_t>(outY);
            }

            // Discard sub-pixel residuals when accumulator is off.
            // Fractional remainder won't carry to the next frame.
            if (UNLIKELY(!m_enableAimAccumulator)) {
                m_aimResidualX = 0;
                m_aimResidualY = 0;
            }

#if !defined(_WIN32)
            QCursor::setPos(m_aimData.centerX, m_aimData.centerY);
#endif
            return;
        }

#if !defined(_WIN32)
        const QPoint center = GetAdjustedCenter();
        m_aimData.centerX = center.x();
        m_aimData.centerY = center.y();
        QCursor::setPos(center);
#endif
        m_isLayoutChangePending = false;
        m_aimResidualX = 0;
        m_aimResidualY = 0;
    }

} // namespace MelonPrime
