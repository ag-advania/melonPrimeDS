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
    // HandleInGameLogic â€” per-frame in-game update
    // Optimized with Hot/Cold splitting to minimize instruction cache pressure.
    // =========================================================================
    HOT_FUNCTION void MelonPrimeCore::HandleInGameLogic()
    {
        PREFETCH_READ(m_ptrs.isAltForm);
        // OPT-Z5: Early prefetch of aim pointers — gives ~50-100 instructions of
        //   lead time before ProcessAimInputMouse reads them, hiding potential L2 miss.
        if (LIKELY(!isStylusMode)) {
            PREFETCH_WRITE(m_ptrs.aimX);
            PREFETCH_WRITE(m_ptrs.aimY);
        }
        // OPT-J: Cache NDS pointer â€” avoids repeated emuInstance->getNDS() pointer chase.
        auto* const nds = emuInstance->getNDS();

        // --- Rare Actions (Morph, Weapon Switch) ---
        if (UNLIKELY(IsPressed(IB_MORPH))) {
            HandleRareMorph();
        }

        // OPT-A: Combined weapon input gate.
        //   Old: ProcessWeaponSwitch called every frame, chased panel pointer for wheelDelta.
        //   New: Single bitmask test + wheelDelta check skips call entirely on 99%+ frames.
        //        wheelDelta is now pre-fetched into m_input by UpdateInputState.
        {
            constexpr uint64_t IB_WEAPON_ALL_TRIGGERS =
                IB_WEAPON_ANY | IB_WEAPON_NEXT | IB_WEAPON_PREV;
            const bool hasWeaponInput =
                (m_input.press & IB_WEAPON_ALL_TRIGGERS) || m_input.wheelDelta;
            if (UNLIKELY(hasWeaponInput && ProcessWeaponSwitch())) {
                HandleRareWeaponSwitch();
            }
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
            nds->TouchScreen(WEAPON_CHECK_START.x(), WEAPON_CHECK_START.y());
        }
        else if (UNLIKELY(m_isWeaponCheckActive)) {
            HandleRareWeaponCheckEnd();
        }

        // --- Movement & Buttons (Hot Path) ---
        // OPT-Z2: Unified move + button in single pass, single store to m_inputMaskFast.
        ProcessMoveAndButtonsFast();

        // --- Morph Boost & Aim (Hot Path) ---
        HandleMorphBallBoost();

        if (isStylusMode) {
            if (!m_flags.test(StateFlags::BIT_BLOCK_STYLUS)) {
                ProcessAimInputStylus();
            }
        }
        else {
            ProcessAimInputMouse();
            // OPT-G: m_aimBlockBits replaces m_isAimDisabled (same semantics: != 0)
            if (!m_flags.test(StateFlags::BIT_LAST_FOCUSED) || !m_aimBlockBits) {
                using namespace Consts::UI;
                nds->TouchScreen(CENTER_RESET.x(), CENTER_RESET.y());
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

    // =========================================================================
    // HandleAdventureMode
    //
    // OPT-V: Scan visor loop â€” redundant input calls removed.
    //
    //   Each FrameAdvanceOnce() triggers the re-entrant path in RunFrameHook
    //   which performs: Poll() -> UpdateInputState() -> ProcessMoveInputFast()
    //   -> button mask update -> SetKeyMask(GetInputMaskFast()). The lambda
    //   then calls RunFrame() which reads the freshly-set key mask.
    //
    //   The outer loop's UpdateInputState / ProcessMoveInputFast / SetKeyMask
    //   are immediately overwritten by the re-entrant path, making them pure
    //   waste. At 30 iterations, this saved ~10,000 cyc per visor toggle
    //   (~300 cyc/iter for UpdateInputState + ~25 cyc for ProcessMoveInputFast
    //    + ~5 cyc for SetKeyMask).
    //
    //   Note: During shader compilation, FrameAdvanceOnce skips RunFrameHook
    //   (and RunFrame), so input state doesn't matter in that case either.
    // =========================================================================
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
                // OPT-V: Loop body reduced to bare FrameAdvanceOnce.
                //   Old: UpdateInputState + ProcessMoveInputFast + SetKeyMask + FrameAdvanceOnce
                //   New: FrameAdvanceOnce only (re-entrant path handles all input).
                for (int i = 0; i < 30; ++i) {
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
            // OPT-B: Guard redundant store â€” 99%+ of frames this bit is already 0.
            //   Avoids dirtying the cache line with an identical value.
            if (UNLIKELY(m_aimBlockBits & AIMBLK_MORPHBALL_BOOST)) {
                SetAimBlockBranchless(AIMBLK_MORPHBALL_BOOST, false);
            }
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
