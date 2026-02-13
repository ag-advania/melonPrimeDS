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
        if (m_rawFilter) {
            m_rawFilter->setRawInputTarget(static_cast<HWND>(m_cachedHwnd));
        }
#endif

        uint64_t down = 0;
        uint64_t press = 0;

#ifdef _WIN32
        FrameHotkeyState hk{};
        if (m_rawFilter) {
            m_rawFilter->pollHotkeys(hk);
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

#if defined(_WIN32)
        if (m_rawFilter) {
            m_rawFilter->fetchMouseDelta(m_input.mouseX, m_input.mouseY);
        }
#else
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
            const int deltaX = m_input.mouseX;
            const int deltaY = m_input.mouseY;

            if ((deltaX | deltaY) == 0) return;

            // OPT-F: Integer threshold check — skip float conversion for tiny deltas.
            //   When both |delta| are below their precomputed thresholds, the scaled
            //   float values will truncate to 0 after int16 cast. This eliminates
            //   CVTSI2SS ×2 + MULSS ×2 + AimAdjust + CVTTSS2SI ×2 (~14-20 cyc).
            //   At high sensitivity thresholds approach 1, matching the (dx|dy)==0 check.
            //
            //   Unsigned comparison trick: (uint32_t)|x| is equivalent to abs(x) for
            //   the purpose of < comparison against a positive threshold.
            {
                const auto adx = static_cast<uint32_t>(deltaX >= 0 ? deltaX : -deltaX);
                const auto ady = static_cast<uint32_t>(deltaY >= 0 ? deltaY : -deltaY);
                if (adx < static_cast<uint32_t>(m_aimMinDeltaX) &&
                    ady < static_cast<uint32_t>(m_aimMinDeltaY))
                    return;
            }

            // OPT-H: Prefetch aim write targets.
            //   m_ptrs.aimX/Y point into NDS mainRAM (4MB). After cache-disruptive
            //   events (weapon switch, shader compile) this line may be in L2/L3.
            //   ~20 cyc of float math below gives the prefetch time to complete.
            PREFETCH_WRITE(m_ptrs.aimX);

            float adjX = static_cast<float>(deltaX) * m_aimSensiFactor;
            float adjY = static_cast<float>(deltaY) * m_aimCombinedY;
            ApplyAimAdjustBranchless(adjX, adjY);

            const int16_t outX = static_cast<int16_t>(adjX);
            const int16_t outY = static_cast<int16_t>(adjY);

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