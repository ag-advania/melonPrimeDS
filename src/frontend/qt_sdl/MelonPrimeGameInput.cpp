#include "MelonPrimeInternal.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "main.h"
#include "Screen.h"
#include "MelonPrimeDef.h"

#include <utility>
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

    struct KeyMap {
        int      hkID;
        uint64_t bit;
    };

    static constexpr std::array<KeyMap, 11> kDownMaps = { {
        { HK_MetroidMoveForward,        IB_MOVE_F },
        { HK_MetroidMoveBack,           IB_MOVE_B },
        { HK_MetroidMoveLeft,           IB_MOVE_L },
        { HK_MetroidMoveRight,          IB_MOVE_R },
        { HK_MetroidJump,               IB_JUMP },
        { HK_MetroidZoom,               IB_ZOOM },
        { HK_MetroidWeaponCheck,        IB_WEAPON_CHECK },
        { HK_MetroidHoldMorphBallBoost, IB_MORPH_BOOST },
        { HK_MetroidMenu,               IB_MENU },
        { HK_MetroidShootScan,          IB_SHOOT },
        { HK_MetroidScanShoot,          IB_SHOOT },
    } };

    static constexpr std::array<KeyMap, 18> kPressMaps = { {
        { HK_MetroidMorphBall,     IB_MORPH },
        { HK_MetroidScanVisor,     IB_SCAN_VISOR },
        { HK_MetroidUIOk,          IB_UI_OK },
        { HK_MetroidUILeft,        IB_UI_LEFT },
        { HK_MetroidUIRight,       IB_UI_RIGHT },
        { HK_MetroidUIYes,         IB_UI_YES },
        { HK_MetroidUINo,          IB_UI_NO },
        { HK_MetroidWeaponBeam,    IB_WEAPON_BEAM },
        { HK_MetroidWeaponMissile, IB_WEAPON_MISSILE },
        { HK_MetroidWeapon1,       IB_WEAPON_1 },
        { HK_MetroidWeapon2,       IB_WEAPON_2 },
        { HK_MetroidWeapon3,       IB_WEAPON_3 },
        { HK_MetroidWeapon4,       IB_WEAPON_4 },
        { HK_MetroidWeapon5,       IB_WEAPON_5 },
        { HK_MetroidWeapon6,       IB_WEAPON_6 },
        { HK_MetroidWeaponSpecial, IB_WEAPON_SPECIAL },
        { HK_MetroidWeaponNext,    IB_WEAPON_NEXT },
        { HK_MetroidWeaponPrevious,IB_WEAPON_PREV },
    } };

    template <typename Func, size_t... Is>
    FORCE_INLINE void UnrollCheckDown(uint64_t& mask, Func&& checker, std::index_sequence<Is...>) {
        ((checker(kDownMaps[Is].hkID) ? (mask |= kDownMaps[Is].bit) : 0), ...);
    }

    template <typename Func, size_t... Is>
    FORCE_INLINE void UnrollCheckPress(uint64_t& mask, Func&& checker, std::index_sequence<Is...>) {
        ((checker(kPressMaps[Is].hkID) ? (mask |= kPressMaps[Is].bit) : 0), ...);
    }

    HOT_FUNCTION void MelonPrimeCore::UpdateInputState()
    {
        // Removed global memset
#ifdef _WIN32
        if (!isFocused) return;
        // OPT-I: setRawInputTarget removed from per-frame path (~8 cyc saved).
        // OPT-M: Single pointer load — subsequent uses via register.
        auto* const rawFilter = m_rawFilter.get();
#endif

        uint64_t down = 0;
        uint64_t press = 0;

#ifdef _WIN32
        FrameHotkeyState hk{};
        if (rawFilter) {
            // OPT-S: Fused snapshot — shares one acquire fence for both
            //   hotkey state and mouse delta (was 2 separate calls + 2 fences).
            rawFilter->snapshotInputFrame(hk, m_input.mouseX, m_input.mouseY);
        }

        const auto hkDown = [&](int id) -> bool {
            return hk.isDown(id) || emuInstance->joyHotkeyMask.testBit(id);
            };
        const auto hkPressed = [&](int id) -> bool {
            return hk.isPressed(id) || emuInstance->joyHotkeyPress.testBit(id);
            };
#else
        const auto hkDown = [&](int id) -> bool {
            return emuInstance->hotkeyMask.testBit(id);
            };
        const auto hkPressed = [&](int id) -> bool {
            return emuInstance->hotkeyPress.testBit(id);
            };
#endif

        UnrollCheckDown(down, hkDown, std::make_index_sequence<kDownMaps.size()>{});
        UnrollCheckPress(press, hkPressed, std::make_index_sequence<kPressMaps.size()>{});

        m_input.down = down;
        m_input.press = press;
        m_input.moveIndex = static_cast<uint32_t>((down >> 6) & 0xF);

#if !defined(_WIN32)
        const QPoint currentPos = QCursor::pos();
        m_input.mouseX = currentPos.x() - m_aimData.centerX;
        m_input.mouseY = currentPos.y() - m_aimData.centerY;
#endif

        // OPT-A: Pre-fetch wheel delta into FrameInputState.
        //   Eliminates per-frame emuInstance→getMainWindow()→panel pointer chase
        //   from ProcessWeaponSwitch's LIKELY (no-op) path.
        {
            auto* panel = emuInstance->getMainWindow()->panel;
            m_input.wheelDelta = panel ? panel->getDelta() : 0;
        }
    }

    HOT_FUNCTION void MelonPrimeCore::ProcessMoveInputFast()
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
        m_inputMaskFast = (m_inputMaskFast & 0xFF0Fu) | (static_cast<uint16_t>(lutResult) & 0x00F0u);
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

    // =========================================================================
    // ProcessAimInputMouse — per-frame aim computation (THE hottest inner loop)
    //
    // OPT-O: Fixed-point Q14 pipeline.
    //   Old: CVTSI2SS×2 + MULSS×2 + float AimAdjust + CVTTSS2SI×2 (~29 cyc)
    //   New: IMUL×2 + integer AimAdjust (CMP) + SAR×2 (~15 cyc)
    //   Eliminates all int↔float domain crossings (4× CVTSI2SS/CVTTSS2SI).
    //
    // OPT-P: Fused scale + AimAdjust.
    //   Deadzone/snap thresholds precomputed in Q14 space.
    //   When adjust disabled: thresholds = 0 → comparisons always fail → SAR-only path.
    //   Hot case (fast aim): |scaled| >> snap threshold → 2 predicted-false branches + SAR.
    //
    // OPT-Q: Redundant zero check removed.
    //   `(deltaX | deltaY) == 0` is subsumed by OPT-F threshold (thresholds ≥ 1).
    // =========================================================================
    HOT_FUNCTION void MelonPrimeCore::ProcessAimInputMouse()
    {
        // OPT-G: Unified aim-disable check via m_aimBlockBits
        if (m_aimBlockBits) return;

        if (UNLIKELY(m_isLayoutChangePending)) {
            m_isLayoutChangePending = false;
#ifdef _WIN32
            if (m_rawFilter) m_rawFilter->discardDeltas();
#endif
            return;
        }

        if (LIKELY(m_flags.test(StateFlags::BIT_LAST_FOCUSED))) {
            const int32_t deltaX = m_input.mouseX;
            const int32_t deltaY = m_input.mouseY;

            // OPT-F: Integer threshold skip — both axes below deadzone → output is 0.
            //   Thresholds ≥ 1, so (deltaX|deltaY)==0 is a strict subset (OPT-Q).
            {
                const auto adx = static_cast<uint32_t>(deltaX >= 0 ? deltaX : -deltaX);
                const auto ady = static_cast<uint32_t>(deltaY >= 0 ? deltaY : -deltaY);
                if (adx < static_cast<uint32_t>(m_aimMinDeltaX) &&
                    ady < static_cast<uint32_t>(m_aimMinDeltaY))
                    return;
            }

            // OPT-H: Prefetch aim write targets (aimX/Y are 8 bytes apart → same CL).
            PREFETCH_WRITE(m_ptrs.aimX);

            // OPT-O: Fixed-point multiply — Q14 intermediate.
            //   int64 multiply avoids overflow at any realistic sensitivity × delta.
            //   IMUL r64 on x86-64 has same latency (~3 cyc) as IMUL r32.
            const int64_t rawX = static_cast<int64_t>(deltaX) * m_aimFixedScaleX;
            const int64_t rawY = static_cast<int64_t>(deltaY) * m_aimFixedScaleY;

            // OPT-P: Fused AimAdjust — integer comparisons in Q14 space.
            //
            // Per-axis three-way check:
            //   |scaled| < adjThresh   → 0       (deadzone)
            //   |scaled| < snapThresh  → ±1      (snap to minimum movement)
            //   otherwise              → SAR 14   (normal scale-down)
            //
            // When adjust disabled: adjThresh=0, snapThresh=0.
            //   Both `< 0` checks are always false → straight to SAR.
            //   This matches the original UNLIKELY(a ≤ 0) early-return behavior
            //   WITHOUT an extra branch — the comparisons simply fall through.
            //
            // Hot case (fast aim, |scaled| >> ONE):
            //   Two false-predicted branches per axis + SAR. ~4 cyc/axis.
            //   Same branch pattern as the original float ternaries, but without
            //   CVTSI2SS/MULSS/CVTTSS2SI overhead.
            const int64_t adjT = m_aimFixedAdjust;
            const int64_t snapT = m_aimFixedSnapThresh;

            int16_t outX, outY;

            {
                const int64_t ax = rawX >= 0 ? rawX : -rawX;
                if (ax < adjT)       outX = 0;
                else if (ax < snapT) outX = static_cast<int16_t>(rawX >= 0 ? 1 : -1);
                else                 outX = static_cast<int16_t>(rawX >> AIM_FRAC_BITS);
            }
            {
                const int64_t ay = rawY >= 0 ? rawY : -rawY;
                if (ay < adjT)       outY = 0;
                else if (ay < snapT) outY = static_cast<int16_t>(rawY >= 0 ? 1 : -1);
                else                 outY = static_cast<int16_t>(rawY >> AIM_FRAC_BITS);
            }

            if ((outX | outY) == 0) return;

            *m_ptrs.aimX = static_cast<uint16_t>(outX);
            *m_ptrs.aimY = static_cast<uint16_t>(outY);

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
    }

} // namespace MelonPrime