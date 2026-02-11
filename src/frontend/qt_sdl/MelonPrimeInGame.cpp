#include "MelonPrimeInternal.h"
#include "MelonPrimeGameSettings.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "main.h"
#include "Screen.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeGameRomAddrTable.h"

namespace MelonPrime {

    // =========================================================================
    // HandleInGameLogic — per-frame in-game update
    // Optimized with Hot/Cold splitting to minimize instruction cache pressure.
    // =========================================================================
    HOT_FUNCTION void MelonPrimeCore::HandleInGameLogic()
    {
        PREFETCH_READ(m_ptrs.isAltForm);

        // --- Rare Actions (Morph, Weapon Switch) ---
        if (UNLIKELY(IsPressed(IB_MORPH))) {
            HandleRareMorph();
        }

        if (UNLIKELY(ProcessWeaponSwitch())) {
            HandleRareWeaponSwitch();
        }

        // --- Adventure Mode ---
        if (UNLIKELY(m_flags.test(StateFlags::BIT_IN_ADVENTURE))) {
            const bool isPaused = (*m_ptrs.isMapOrUserActionPaused) == 0x1;
            m_flags.assign(StateFlags::BIT_PAUSED, isPaused);

            if (IsAnyPressed(IB_SCAN_VISOR | IB_UI_ANY)) {
                HandleAdventureMode();
            }
        }

        // --- Weapon Check ---
        if (IsDown(IB_WEAPON_CHECK)) {
            if (!m_isWeaponCheckActive) {
                HandleRareWeaponCheckStart();
            }
            using namespace Consts::UI;
            emuInstance->getNDS()->TouchScreen(WEAPON_CHECK_START.x(), WEAPON_CHECK_START.y());
        }
        else if (UNLIKELY(m_isWeaponCheckActive)) {
            HandleRareWeaponCheckEnd();
        }

        // --- Movement & Buttons (Hot Path) ---
        ProcessMoveInputFast();

        // OPT: Branchless batched button mask update.
        //
        // Previous code: 3 conditional ternaries → 3 separate RMW on m_inputMaskFast.
        //   mask |= (!IsDown(IB_JUMP))  ? (1u << INPUT_B) : 0;
        //   mask |= (!IsDown(IB_SHOOT)) ? (1u << INPUT_L) : 0;
        //   mask |= (!IsDown(IB_ZOOM))  ? (1u << INPUT_R) : 0;
        //
        // Each ternary generates a test+cmov or test+branch. Even with good prediction,
        // the 3 sequential RMW ops create a dependency chain on m_inputMaskFast.
        //
        // New code: extract inverted bits from m_input.down (already in L1),
        // shift each to its target position, OR together in one write.
        // Generates: NOT + 3×(SHR+AND+SHL) + 3×OR + 1 masked store.
        // Zero branches, single write to m_inputMaskFast, no dependency chain.
        {
            constexpr uint16_t kModBits = (1u << INPUT_B) | (1u << INPUT_L) | (1u << INPUT_R);
            const uint64_t nd = ~m_input.down;
            // IB_JUMP  = bit 0 → INPUT_B = bit 1
            // IB_SHOOT = bit 1 → INPUT_L = bit 9
            // IB_ZOOM  = bit 2 → INPUT_R = bit 8
            const uint16_t bBit = static_cast<uint16_t>(((nd >> 0) & 1u) << INPUT_B);
            const uint16_t lBit = static_cast<uint16_t>(((nd >> 1) & 1u) << INPUT_L);
            const uint16_t rBit = static_cast<uint16_t>(((nd >> 2) & 1u) << INPUT_R);
            m_inputMaskFast = (m_inputMaskFast & ~kModBits) | bBit | lBit | rBit;
        }

        // --- Morph Boost & Aim (Hot Path) ---
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

    // =========================================================================
    // Outlined Cold Paths
    // =========================================================================

    COLD_FUNCTION void MelonPrimeCore::HandleRareMorph()
    {
        if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
        auto* nds = emuInstance->getNDS();
        nds->ReleaseScreen();
        FrameAdvanceTwice();
        using namespace Consts::UI;
        nds->TouchScreen(MORPH_START.x(), MORPH_START.y());
        FrameAdvanceTwice();
        nds->ReleaseScreen();
        FrameAdvanceTwice();
    }

    COLD_FUNCTION void MelonPrimeCore::HandleRareWeaponSwitch()
    {
        if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
    }

    COLD_FUNCTION void MelonPrimeCore::HandleRareWeaponCheckStart()
    {
        if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);
        m_isWeaponCheckActive = true;
        SetAimBlockBranchless(AIMBLK_CHECK_WEAPON, true);
        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();
    }

    COLD_FUNCTION void MelonPrimeCore::HandleRareWeaponCheckEnd()
    {
        m_isWeaponCheckActive = false;
        emuInstance->getNDS()->ReleaseScreen();
        SetAimBlockBranchless(AIMBLK_CHECK_WEAPON, false);
        FrameAdvanceTwice();
    }

    COLD_FUNCTION void MelonPrimeCore::HandleAdventureMode()
    {
        auto* nds = emuInstance->getNDS();

        if (IsPressed(IB_SCAN_VISOR)) {
            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

            nds->ReleaseScreen();
            FrameAdvanceTwice();
            using namespace Consts::UI;
            nds->TouchScreen(SCAN_VISOR_BUTTON.x(), SCAN_VISOR_BUTTON.y());

            if ((*m_ptrs.isInVisorOrMap) == 0x1) {
                FrameAdvanceTwice();
            }
            else {
                for (int i = 0; i < 30; ++i) {
                    UpdateInputState();
                    ProcessMoveInputFast();
                    nds->SetKeyMask(m_inputMaskFast);
                    FrameAdvanceOnce();
                }
            }
            nds->ReleaseScreen();
            FrameAdvanceTwice();
        }

        // UI touch buttons
        if (IsAnyPressed(IB_UI_ANY)) {
#define TOUCH_IF_PRESSED(BIT, POINT) \
            if (IsPressed(BIT)) { \
                nds->ReleaseScreen(); \
                FrameAdvanceTwice(); \
                nds->TouchScreen(POINT.x(), POINT.y()); \
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
    }

    // =========================================================================
    // Hot Helpers
    // =========================================================================

    HOT_FUNCTION bool MelonPrimeCore::HandleMorphBallBoost()
    {
        if (!m_flags.test(StateFlags::BIT_IS_SAMUS)) return false;

        if (IsDown(IB_MORPH_BOOST)) {
            const bool isAltForm = (*m_ptrs.isAltForm) == 0x02;
            m_flags.assign(StateFlags::BIT_IS_ALT_FORM, isAltForm);

            if (isAltForm) {
                const uint8_t boostGauge = *m_ptrs.boostGauge;
                const bool isBoosting = (*m_ptrs.isBoosting) != 0x00;
                const bool gaugeEnough = boostGauge > 0x0A;

                SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, true);

                if (!IsDown(IB_WEAPON_CHECK)) {
                    emuInstance->getNDS()->ReleaseScreen();
                }

                InputSetBranchless(INPUT_R, !isBoosting && gaugeEnough);

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

        auto* nds = emuInstance->getNDS();

        if (!(m_appliedFlags & APPLIED_HEADPHONE)) {
            MelonPrimeGameSettings::ApplyHeadphoneOnce(
                nds, localCfg, m_currentRom.operationAndSound, m_appliedFlags, APPLIED_HEADPHONE);
        }

        MelonPrimeGameSettings::ApplyMphSensitivity(
            nds, localCfg, m_currentRom.sensitivity,
            m_currentRom.baseInGameSensi, m_flags.test(StateFlags::BIT_IN_GAME_INIT));

        if (!(m_appliedFlags & APPLIED_UNLOCK)) {
            MelonPrimeGameSettings::ApplyUnlockHuntersMaps(
                nds, localCfg, m_appliedFlags, APPLIED_UNLOCK,
                m_currentRom.unlockMapsHunters, m_currentRom.unlockMapsHunters2,
                m_currentRom.unlockMapsHunters3, m_currentRom.unlockMapsHunters4,
                m_currentRom.unlockMapsHunters5);
        }

        MelonPrimeGameSettings::UseDsName(nds, localCfg, m_currentRom.dsNameFlagAndMicVolume);
        MelonPrimeGameSettings::ApplySelectedHunterStrict(nds, localCfg, m_currentRom.mainHunter);
        MelonPrimeGameSettings::ApplyLicenseColorStrict(nds, localCfg, m_currentRom.rankColor);

        if (!(m_appliedFlags & APPLIED_VOL_SFX)) {
            MelonPrimeGameSettings::ApplySfxVolumeOnce(
                nds, localCfg, m_currentRom.volSfx8Bit, m_appliedFlags, APPLIED_VOL_SFX);
        }
        if (!(m_appliedFlags & APPLIED_VOL_MUSIC)) {
            MelonPrimeGameSettings::ApplyMusicVolumeOnce(
                nds, localCfg, m_currentRom.volMusic8Bit, m_appliedFlags, APPLIED_VOL_MUSIC);
        }
    }

} // namespace MelonPrime
