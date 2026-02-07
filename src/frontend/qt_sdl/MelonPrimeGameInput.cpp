#include "MelonPrimeInternal.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "main.h"
#include "Screen.h"
#include "MelonPrimeDef.h"

#include <QCursor>
#include <QBitArray>

#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawInputState.h"
#endif

namespace MelonPrime {

    alignas(64) static constexpr std::array<uint8_t, 16> MoveLUT = {
        0xF0, 0xB0, 0x70, 0xF0, 0xD0, 0x90, 0x50, 0xD0,
        0xE0, 0xA0, 0x60, 0xE0, 0xF0, 0xB0, 0x70, 0xF0,
    };

    HOT_FUNCTION void MelonPrimeCore::UpdateInputState()
    {
        m_input.down = 0;
        m_input.press = 0;
        m_input.mouseX = 0;
        m_input.mouseY = 0;
        m_input.moveIndex = 0;

#ifdef _WIN32
        if (!isFocused) return;
        if (m_rawFilter) {
            HWND myHwnd = (HWND)emuInstance->getMainWindow()->winId();
            m_rawFilter->setRawInputTarget(myHwnd);

            if (!m_flags.test(StateFlags::BIT_JOY2KEY)) {
                m_rawFilter->poll();
            }
        }
#endif

        uint64_t down = 0;
        uint64_t press = 0;

#ifdef _WIN32
        FrameHotkeyState hk{};
        if (m_rawFilter) {
            m_rawFilter->pollHotkeys(hk);
        }

        auto hkDown = [&](int id) -> bool {
            return hk.isDown(id) || IsJoyDown(id);
            };
        auto hkPressed = [&](int id) -> bool {
            return hk.isPressed(id) || IsJoyPressed(id);
            };
#else
        auto hkDown = [&](int id) -> bool {
            return emuInstance->hotkeyMask.testBit(id);
            };
        auto hkPressed = [&](int id) -> bool {
            return emuInstance->hotkeyPress.testBit(id);
            };
#endif

        down |= hkDown(HK_MetroidMoveForward) ? IB_MOVE_F : 0;
        down |= hkDown(HK_MetroidMoveBack) ? IB_MOVE_B : 0;
        down |= hkDown(HK_MetroidMoveLeft) ? IB_MOVE_L : 0;
        down |= hkDown(HK_MetroidMoveRight) ? IB_MOVE_R : 0;
        down |= hkDown(HK_MetroidJump) ? IB_JUMP : 0;
        down |= hkDown(HK_MetroidZoom) ? IB_ZOOM : 0;
        down |= (hkDown(HK_MetroidShootScan) || hkDown(HK_MetroidScanShoot)) ? IB_SHOOT : 0;
        down |= hkDown(HK_MetroidWeaponCheck) ? IB_WEAPON_CHECK : 0;
        down |= hkDown(HK_MetroidHoldMorphBallBoost) ? IB_MORPH_BOOST : 0;
        down |= hkDown(HK_MetroidMenu) ? IB_MENU : 0;

        press |= hkPressed(HK_MetroidMorphBall) ? IB_MORPH : 0;
        press |= hkPressed(HK_MetroidScanVisor) ? IB_SCAN_VISOR : 0;
        press |= hkPressed(HK_MetroidUIOk) ? IB_UI_OK : 0;
        press |= hkPressed(HK_MetroidUILeft) ? IB_UI_LEFT : 0;
        press |= hkPressed(HK_MetroidUIRight) ? IB_UI_RIGHT : 0;
        press |= hkPressed(HK_MetroidUIYes) ? IB_UI_YES : 0;
        press |= hkPressed(HK_MetroidUINo) ? IB_UI_NO : 0;
        press |= hkPressed(HK_MetroidWeaponBeam) ? IB_WEAPON_BEAM : 0;
        press |= hkPressed(HK_MetroidWeaponMissile) ? IB_WEAPON_MISSILE : 0;
        press |= hkPressed(HK_MetroidWeapon1) ? IB_WEAPON_1 : 0;
        press |= hkPressed(HK_MetroidWeapon2) ? IB_WEAPON_2 : 0;
        press |= hkPressed(HK_MetroidWeapon3) ? IB_WEAPON_3 : 0;
        press |= hkPressed(HK_MetroidWeapon4) ? IB_WEAPON_4 : 0;
        press |= hkPressed(HK_MetroidWeapon5) ? IB_WEAPON_5 : 0;
        press |= hkPressed(HK_MetroidWeapon6) ? IB_WEAPON_6 : 0;
        press |= hkPressed(HK_MetroidWeaponSpecial) ? IB_WEAPON_SPECIAL : 0;
        press |= hkPressed(HK_MetroidWeaponNext) ? IB_WEAPON_NEXT : 0;
        press |= hkPressed(HK_MetroidWeaponPrevious) ? IB_WEAPON_PREV : 0;

        m_input.down = down;
        m_input.press = press;
        m_input.moveIndex = static_cast<uint32_t>((down >> 6) & 0xF);

#if defined(_WIN32)
        if (m_rawFilter) {
            m_rawFilter->fetchMouseDelta(m_input.mouseX, m_input.mouseY);
        }
        else {
            m_input.mouseX = 0;
            m_input.mouseY = 0;
        }
#else
        const QPoint currentPos = QCursor::pos();
        m_input.mouseX = currentPos.x() - m_aimData.centerX;
        m_input.mouseY = currentPos.y() - m_aimData.centerY;
#endif
    }

    HOT_FUNCTION void MelonPrimeCore::ProcessMoveInputFast()
    {
        uint32_t curr = m_input.moveIndex;
        uint32_t finalInput;

        if (LIKELY(!m_flags.test(StateFlags::BIT_SNAP_TAP))) {
            finalInput = curr;
        }
        else {
            const uint32_t last = m_snapState & 0xFFu;
            const uint32_t priority = m_snapState >> 8;
            const uint32_t newPress = curr & ~last;
            const uint32_t hConflict = ((curr & 0x3u) == 0x3u) ? 0x3u : 0u;
            const uint32_t vConflict = ((curr & 0xCu) == 0xCu) ? 0xCu : 0u;
            const uint32_t conflict = vConflict | hConflict;
            const uint32_t updateMask = (newPress & conflict) ? ~0u : 0u;
            const uint32_t newPriority = (priority & ~(conflict & updateMask)) | (newPress & conflict & updateMask);
            const uint32_t activePriority = newPriority & curr;
            m_snapState = static_cast<uint16_t>((curr & 0xFFu) | ((activePriority & 0xFFu) << 8));
            finalInput = (curr & ~conflict) | (activePriority & conflict);
        }

        const uint8_t lutResult = MoveLUT[finalInput & 0xF];
        m_inputMaskFast = (m_inputMaskFast & 0xFF0Fu) | (static_cast<uint16_t>(lutResult) & 0x00F0u);
    }

    void MelonPrimeCore::ProcessMoveInput(QBitArray& mask)
    {
        ProcessMoveInputFast();
        mask.setBit(INPUT_UP, (m_inputMaskFast >> INPUT_UP) & 1);
        mask.setBit(INPUT_DOWN, (m_inputMaskFast >> INPUT_DOWN) & 1);
        mask.setBit(INPUT_LEFT, (m_inputMaskFast >> INPUT_LEFT) & 1);
        mask.setBit(INPUT_RIGHT, (m_inputMaskFast >> INPUT_RIGHT) & 1);
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
        if (m_isAimDisabled) return;

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

            if (deltaX == 0 && deltaY == 0) return;

            float rawScaledX = (deltaX * m_aimSensiFactor);
            float rawScaledY = (deltaY * m_aimCombinedY);

            float adjX = rawScaledX;
            float adjY = rawScaledY;
            ApplyAimAdjustBranchless(adjX, adjY);

            int16_t outX = static_cast<int16_t>(adjX);
            int16_t outY = static_cast<int16_t>(adjY);

            if (outX == 0 && outY == 0) return;

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
