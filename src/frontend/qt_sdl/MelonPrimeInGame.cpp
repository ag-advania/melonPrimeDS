#include "MelonPrimeInternal.h"
#include "MelonPrimeGameSettings.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "main.h"
#include "Screen.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeGameRomAddrTable.h" // 追加

namespace MelonPrime {

    HOT_FUNCTION void MelonPrimeCore::HandleInGameLogic()
    {
        PREFETCH_READ(m_ptrs.isAltForm);

        if (UNLIKELY(IsPressed(IB_MORPH))) {
            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
            emuInstance->getNDS()->ReleaseScreen();
            FrameAdvanceTwice();
            using namespace Consts::UI;
            emuInstance->getNDS()->TouchScreen(MORPH_START.x(), MORPH_START.y());
            FrameAdvanceTwice();
            emuInstance->getNDS()->ReleaseScreen();
            FrameAdvanceTwice();
        }

        if (UNLIKELY(ProcessWeaponSwitch())) {
            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
        }

        const bool weaponCheckDown = IsDown(IB_WEAPON_CHECK);

        if (weaponCheckDown) {
            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
            if (!m_isWeaponCheckActive) {
                m_isWeaponCheckActive = true;
                SetAimBlockBranchless(AIMBLK_CHECK_WEAPON, true);
                emuInstance->getNDS()->ReleaseScreen();
                FrameAdvanceTwice();
            }
            using namespace Consts::UI;
            emuInstance->getNDS()->TouchScreen(WEAPON_CHECK_START.x(), WEAPON_CHECK_START.y());
        }
        else if (UNLIKELY(m_isWeaponCheckActive)) {
            m_isWeaponCheckActive = false;
            emuInstance->getNDS()->ReleaseScreen();
            SetAimBlockBranchless(AIMBLK_CHECK_WEAPON, false);
            FrameAdvanceTwice();
        }

        if (UNLIKELY(m_flags.test(StateFlags::BIT_IN_ADVENTURE))) {
            HandleAdventureMode();
        }

        ProcessMoveInputFast();

        InputSetBranchless(INPUT_B, !IsDown(IB_JUMP));
        InputSetBranchless(INPUT_L, !IsDown(IB_SHOOT));
        InputSetBranchless(INPUT_R, !IsDown(IB_ZOOM));

        HandleMorphBallBoost();

        if (isStylusMode) {
            if (!m_flags.test(StateFlags::BIT_BLOCK_STYLUS)) {
                ProcessAimInputStylus();
            }
        }
        else {
            ProcessAimInputMouse();
            if (!m_flags.test(StateFlags::BIT_LAST_FOCUSED) || !m_isAimDisabled) {
                using namespace Consts::UI;
                emuInstance->getNDS()->TouchScreen(CENTER_RESET.x(), CENTER_RESET.y());
            }
        }
    }

    void MelonPrimeCore::HandleAdventureMode()
    {
        const bool isPaused = (*m_ptrs.isMapOrUserActionPaused) == 0x1;
        m_flags.assign(StateFlags::BIT_PAUSED, isPaused);

        if (IsPressed(IB_SCAN_VISOR)) {
            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

            emuInstance->getNDS()->ReleaseScreen();
            FrameAdvanceTwice();
            using namespace Consts::UI;
            emuInstance->getNDS()->TouchScreen(SCAN_VISOR_BUTTON.x(), SCAN_VISOR_BUTTON.y());

            if ((*m_ptrs.isInVisorOrMap) == 0x1) {
                FrameAdvanceTwice();
            }
            else {
                for (int i = 0; i < 30; i++) {
                    UpdateInputState();
                    ProcessMoveInputFast();
                    emuInstance->getNDS()->SetKeyMask(m_inputMaskFast);
                    FrameAdvanceOnce();
                }
            }
            emuInstance->getNDS()->ReleaseScreen();
            FrameAdvanceTwice();
        }

#define TOUCH_IF_PRESSED(BIT, POINT) \
        if (IsPressed(BIT)) { \
            emuInstance->getNDS()->ReleaseScreen(); \
            FrameAdvanceTwice(); \
            emuInstance->getNDS()->TouchScreen(POINT.x(), POINT.y()); \
            FrameAdvanceTwice(); \
        }

        using namespace Consts::UI;
        TOUCH_IF_PRESSED(IB_UI_OK, OK)
            TOUCH_IF_PRESSED(IB_UI_LEFT, LEFT)
            TOUCH_IF_PRESSED(IB_UI_RIGHT, RIGHT)
            TOUCH_IF_PRESSED(IB_UI_YES, YES)
            TOUCH_IF_PRESSED(IB_UI_NO, NO)
#undef TOUCH_IF_PRESSED
    }

    HOT_FUNCTION bool MelonPrimeCore::HandleMorphBallBoost()
    {
        if (!m_flags.test(StateFlags::BIT_IS_SAMUS)) return false;

        if (IsDown(IB_MORPH_BOOST)) {
            const bool isAltForm = (*m_ptrs.isAltForm) == 0x02;
            m_flags.assign(StateFlags::BIT_IS_ALT_FORM, isAltForm);

            if (isAltForm) {
                const uint8_t boostGaugeValue = *m_ptrs.boostGauge;
                const bool isBoosting = (*m_ptrs.isBoosting) != 0x00;
                const bool isBoostGaugeEnough = boostGaugeValue > 0x0A;

                SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, true);

                if (!IsDown(IB_WEAPON_CHECK)) {
                    emuInstance->getNDS()->ReleaseScreen();
                }

                InputSetBranchless(INPUT_R, !isBoosting && isBoostGaugeEnough);

                if (isBoosting) {
                    SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, false);
                }
                return true;
            }
        }
        else {
            SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, false);
        }
        return false;
    }

    COLD_FUNCTION void MelonPrimeCore::ApplyGameSettingsOnce()
    {
        InputSetBranchless(INPUT_L, !IsPressed(IB_UI_LEFT));
        InputSetBranchless(INPUT_R, !IsPressed(IB_UI_RIGHT));

        // ★以下、m_addrCold を m_currentRom-> に変更

        if (!(m_appliedFlags & APPLIED_HEADPHONE)) {
            MelonPrimeGameSettings::ApplyHeadphoneOnce(emuInstance->getNDS(), localCfg, m_currentRom->operationAndSound, m_appliedFlags, APPLIED_HEADPHONE);
        }

        // Sensitivity: 旧 m_addrCold.inGameSensi は P1ベースのアドレスとみなして baseInGameSensi を使用
        MelonPrimeGameSettings::ApplyMphSensitivity(emuInstance->getNDS(), localCfg, m_currentRom->sensitivity, m_currentRom->baseInGameSensi, m_flags.test(StateFlags::BIT_IN_GAME_INIT));

        if (!(m_appliedFlags & APPLIED_UNLOCK)) {
            MelonPrimeGameSettings::ApplyUnlockHuntersMaps(emuInstance->getNDS(), localCfg, m_appliedFlags, APPLIED_UNLOCK,
                m_currentRom->unlockMapsHunters, m_currentRom->unlockMapsHunters2, m_currentRom->unlockMapsHunters3,
                m_currentRom->unlockMapsHunters4, m_currentRom->unlockMapsHunters5);
        }

        MelonPrimeGameSettings::UseDsName(emuInstance->getNDS(), localCfg, m_currentRom->dsNameFlagAndMicVolume);
        MelonPrimeGameSettings::ApplySelectedHunterStrict(emuInstance->getNDS(), localCfg, m_currentRom->mainHunter);
        MelonPrimeGameSettings::ApplyLicenseColorStrict(emuInstance->getNDS(), localCfg, m_currentRom->rankColor);

        if (!(m_appliedFlags & APPLIED_VOL_SFX)) {
            MelonPrimeGameSettings::ApplySfxVolumeOnce(emuInstance->getNDS(), localCfg, m_currentRom->volSfx8Bit, m_appliedFlags, APPLIED_VOL_SFX);
        }
        if (!(m_appliedFlags & APPLIED_VOL_MUSIC)) {
            MelonPrimeGameSettings::ApplyMusicVolumeOnce(emuInstance->getNDS(), localCfg, m_currentRom->volMusic8Bit, m_appliedFlags, APPLIED_VOL_MUSIC);
        }
    }

} // namespace MelonPrime