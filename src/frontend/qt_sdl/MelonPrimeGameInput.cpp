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
#ifdef _WIN32
        auto* const rawFilter = m_rawFilter.get();

        // OPT-Z3: PollAndSnapshot -- merged Poll + snapshot into single call.
        // Always runs to drain WM_INPUT messages even when unfocused,
        // preventing message buildup and stale delta accumulation.
        FrameHotkeyState hk{};
        if (rawFilter) {
            rawFilter->PollAndSnapshot(hk, m_input.mouseX, m_input.mouseY);
        }

        if (!isFocused) return; // [FIX-2] moved after poll
#endif

        uint64_t down = 0;
        uint64_t press = 0;

#ifdef _WIN32
        const auto hkDown = [&](int id) -> bool {
            return hk.isDown(id) || ((emuInstance->joyHotkeyMask >> id) & 1);
            };
        const auto hkPressed = [&](int id) -> bool {
            return hk.isPressed(id) || ((emuInstance->joyHotkeyPress >> id) & 1);
            };
#else
        const auto hkDown = [&](int id) -> bool {
            return (emuInstance->hotkeyMask >> id) & 1;
            };
        const auto hkPressed = [&](int id) -> bool {
            return (emuInstance->hotkeyPress >> id) & 1;
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

        {
            auto* panel = emuInstance->getMainWindow()->panel;
            m_input.wheelDelta = panel ? panel->getDelta() : 0;
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

    // =========================================================================
    // ProcessAimInputMouse
    // =========================================================================
    HOT_FUNCTION void MelonPrimeCore::ProcessAimInputMouse()
    {
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

            {
                const int32_t signX = deltaX >> 31;
                const int32_t signY = deltaY >> 31;
                const uint32_t adx = static_cast<uint32_t>((deltaX ^ signX) - signX);
                const uint32_t ady = static_cast<uint32_t>((deltaY ^ signY) - signY);

                if (adx < static_cast<uint32_t>(m_aimMinDeltaX) &&
                    ady < static_cast<uint32_t>(m_aimMinDeltaY))
                    return;
            }

            PREFETCH_WRITE(m_ptrs.aimX);
            PREFETCH_WRITE(m_ptrs.aimY);

            const int64_t rawX = static_cast<int64_t>(deltaX) * m_aimFixedScaleX;
            const int64_t rawY = static_cast<int64_t>(deltaY) * m_aimFixedScaleY;

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

            const int16_t outX = apply_aim(rawX);
            const int16_t outY = apply_aim(rawY);

            if ((outX | outY) == 0) return;

            // Write only the latest slot, just before the DS engine's zero-clear.
            // (The ASM patch side reads from +0x3C / +0x44 bypass, so this is 1:1)
            // --- 感度統一 & デッドゾーン突破 (Pre-amplification) ---
            // ASMパッチでDS側の加算処理（4倍増幅）をスキップしている場合、
            // C++側で予め値を4倍（<< 2）にして渡すことで感度を一致させつつ、
            // DS内部のデッドゾーン(±3)を確定で突破する。
            const uint32_t ampShift = m_disableMphAimSmoothing ? 2 : 0;

            *m_ptrs.aimX = static_cast<uint16_t>(outX << ampShift);
            *m_ptrs.aimY = static_cast<uint16_t>(outY << ampShift);

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
