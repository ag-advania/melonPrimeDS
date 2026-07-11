#include "MelonPrimeInternal.h"
#include "MelonPrimeGameSettings.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "main.h"
#include "Screen.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeGameRomAddrTable.h"
#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#endif

namespace MelonPrime {

    // =========================================================================
    // HandleInGameLogic - per-frame in-game update
    // Optimized with Hot/Cold splitting to minimize instruction cache pressure.
    // =========================================================================
    HOT_FUNCTION void MelonPrimeCore::HandleInGameLogic()
    {
        PREFETCH_READ(m_ptrs.isAltForm);
        const bool isStylusMode = this->isStylusMode;
        // Early prefetch of aim pointers - gives ~50-100 instructions of
        // lead time before ProcessAimInputMouse reads them, hiding potential L2 miss.
        if (LIKELY(!isStylusMode)) {
            PREFETCH_WRITE(m_ptrs.aimX);
            PREFETCH_WRITE(m_ptrs.aimY);
        }
        // Cache NDS pointer - avoids repeated emuInstance->getNDS() pointer chase.
        auto* const nds = emuInstance->getNDS();

        // --- Rare Actions (Morph, Weapon Switch) ---
        if (UNLIKELY(IsPressed(IB_MORPH))) {
            HandleRareMorph();
        }

        // Combined weapon input gate.
        //   Single bitmask test + wheelDelta check skips call entirely on 99%+ frames.
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

            // During the map / user-action pause, the Mouse-Left ShootScan key must
            // not fire (a left click stays touch-only there). Only the V-default
            // ScanShoot key triggers shoot/scan/map-expand. IB_SHOOT is the OR of
            // both keys in ProjectDownState, so rebuild it from the V key alone.
            if (isPaused) {
                m_input.down = (m_input.down & ~IB_SHOOT)
                    | (m_scanShootKeyDown ? IB_SHOOT : 0ULL);
            }

            if (IsAnyPressed(IB_SCAN_VISOR | IB_UI_ANY)) {
                HandleAdventureMode();
            }
        }

        // --- Weapon Check ---
        if (IsDown(IB_WEAPON_CHECK)) {
            const bool isOmegaCannonFlagActive =
                ((*m_ptrs.havingWeapons & WeaponMask::OmegaCannon) != 0);

            if (UNLIKELY(isOmegaCannonFlagActive)) {
                if (UNLIKELY(m_isWeaponCheckActive)) {
                    HandleRareWeaponCheckEnd();
                }
                if (IsPressed(IB_WEAPON_CHECK)) {
                    emuInstance->osdAddMessage(0, "Weapon Check is unavailable while Omega Cannon is active!");
                }
            }
            else {
                if (!m_isWeaponCheckActive) {
                    HandleRareWeaponCheckStart();
                }
                using namespace Consts::UI;
                nds->TouchScreen(WEAPON_CHECK_START.x(), WEAPON_CHECK_START.y());
            }
        }
        else if (UNLIKELY(m_isWeaponCheckActive)) {
            HandleRareWeaponCheckEnd();
        }

        // --- Movement & Buttons (Hot Path) ---
        ProcessMoveAndButtonsFastFromReset();
        ApplyBipedFireInput();
        ApplyZoomBindingInput();

        // --- Morph Boost & Aim (Hot Path) ---
        // Boost no longer blocks mouse aim: the aim + center-touch path below must
        // keep running so the Morph Ball steering direction updates during both the
        // charge and the roll.
        HandleMorphBallBoost();

        if (isStylusMode) {
            if (!m_flags.test(StateFlags::BIT_BLOCK_STYLUS)) {
                ProcessAimInputStylus(nds);
            }
        }
        else {
#ifdef _WIN32
            // P-47: LateLatch only matters when FrameAdvance was called since
            // PollAndSnapshot (morph: ~96 ms, weapon: ~32 ms).  On normal frames
            // the window is ~40–100 ns → kernel buffer is empty.
            // Skipping processRawInputBatched saves ~500–2000 cyc/frame.
            if (m_rawFilter && m_didFrameAdvanceSinceSnapshot)
                m_rawFilter->LateLatchMouseDelta(
                    m_rawInputSubscription, m_input.mouseX, m_input.mouseY);
#endif
            ProcessAimInputMouse();
            // m_aimBlockBits replaces m_isAimDisabled (same semantics: != 0)
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

        if (m_enableDirectAltFormTransform) {
            // TransformGateHook redirects Gate A/B into the game's native
            // TransformRequest path. Keep a short pending window so a press is
            // not lost if the game reaches the transform gate a few frames late.
            m_directTransformPendingFrames = 10;
            return;
        }

        // Legacy touch-simulation approach.
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
    // REFACTORED: Replaced TOUCH_IF_PRESSED preprocessor macro with a
    // constexpr table + loop. Benefits:
    //   - Type-safe: no macro expansion surprises
    //   - Debuggable: breakpoints work on individual iterations
    //   - Maintainable: adding a new UI button is a single table entry
    //   - Same codegen: compiler unrolls small constexpr loops
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
                // Loop body reduced to bare FrameAdvanceOnce.
                // Re-entrant path handles all input.
                for (int i = 0; i < 30; ++i) {
                    FrameAdvanceOnce();
                }
            }
            nds->ReleaseScreen();
            FrameAdvanceTwice();
        }

        // --- UI Touch Buttons (data-driven) ---
        if (IsAnyPressed(IB_UI_ANY)) {
            struct UIAction {
                uint64_t bit;
                QPoint   point;
            };
            // constexpr array replaces 5 TOUCH_IF_PRESSED macro invocations.
            // Compiler unrolls this loop since the array size is known at compile time.
            static constexpr UIAction kUIActions[] = {
                { IB_UI_OK,    Consts::UI::OK    },
                { IB_UI_LEFT,  Consts::UI::LEFT  },
                { IB_UI_RIGHT, Consts::UI::RIGHT },
                { IB_UI_YES,   Consts::UI::YES   },
                { IB_UI_NO,    Consts::UI::NO    },
            };

            for (const auto& action : kUIActions) {
                if (IsPressed(action.bit)) {
                    nds->ReleaseScreen();
                    FrameAdvanceTwice();
                    nds->TouchScreen(action.point.x(), action.point.y());
                    FrameAdvanceTwice();
                }
            }
        }
    }

    // =========================================================================
    // Hot Helpers
    // =========================================================================

    HOT_FUNCTION bool MelonPrimeCore::HandleMorphBallBoost()
    {
        // Boost is Samus-only; bail for every other hunter on the cheapest check.
        if (LIKELY(!m_flags.test(StateFlags::BIT_IS_SAMUS))) {
            return false;
        }

        const bool isAltForm = (*m_ptrs.isAltForm) == 0x02;
        m_flags.assign(StateFlags::BIT_IS_ALT_FORM, isAltForm);

        // Boost path arbitration — MOUSE MODE ONLY. In mouse mode MelonPrime holds
        // a static center touch every frame for aim, which pins the game's "touch
        // active" state on, so the boost gate (around 020235C8) can no longer tell a
        // button boost from a swipe boost on its own. The gate routes to the R
        // hold-charge path only when CanTouchBoost (CPlayer +0x4C4 bit27) is clear;
        // when it is set it takes the touch/aim branch, which boosts when the steer
        // delta magnitude clears the game's threshold (a fast aim flick = swipe
        // boost). We emulate the real-hardware discriminator (stylus down vs up):
        //  - swiping (steer delta over the game's threshold) → leave CanTouchBoost
        //    set so the swipe boost fires.
        //  - not swiping, but a button boost is wanted (R held, or a charge is
        //    already building, or the Shift auto-cycle) → clear it so the R path
        //    runs. boostGauge (player+0x148) keeps it cleared through the release
        //    frame so the boost actually fires.
        //  - otherwise → set it (re-arm) so the next fast flick swipe-boosts.
        // This makes "hold R + swipe + release R" produce both a swipe boost and an
        // R boost, like real hardware, instead of R suppressing the swipe.
        //
        // Stylus mode is excluded entirely: the real stylus drives the touch state,
        // so the game arbitrates correctly with no help from us.
        if (isAltForm && !isStylusMode && m_ptrs.flags1) {
            bool swiping = false;
            if (m_ptrs.altSteerDelta) {
                // Previous-frame steer delta (input +0x2A/+0x2C, s16); for a held
                // swipe it tracks the current gesture. 0x1FA4 is the game's own
                // touch-boost magnitude threshold (sum of squares).
                const int32_t sdx = m_ptrs.altSteerDelta[0];
                const int32_t sdy = m_ptrs.altSteerDelta[1];
                swiping = (sdx * sdx + sdy * sdy) > 0x1FA4;
            }
            const bool buttonBoost = !swiping
                && (IsDown(IB_ZOOM) || IsDown(IB_MORPH_BOOST) || (*m_ptrs.boostGauge != 0));
            if (buttonBoost)
                *m_ptrs.flags1 &= ~0x08000000u;
            else
                *m_ptrs.flags1 |= 0x08000000u;
        }

        // Shift hold-to-boost auto-cycle (separate from the manual right-click R
        // boost handled by ApplyZoomBindingInput).
        if (!IsDown(IB_MORPH_BOOST)) {
            return false;
        }

        if (isAltForm) {
            const uint8_t boostGauge = *m_ptrs.boostGauge;
            // NOTE: m_ptrs.isBoosting currently points at player+0x14A, which is
            // the Boost cooldown/busy timer (not player+0x4C4 bit26 Boosting).
            // It is read as a "boost busy" gate for the auto charge/release cycle;
            // keep the same pointer to preserve the existing cycle timing.
            const bool boostCooldownActive = (*m_ptrs.isBoosting) != 0x00;
            const bool gaugeEnough = boostGauge > 0x0A;

            // Do NOT raise AIMBLK_MORPHBALL_BOOST. Boost speed is applied along the
            // current Morph Ball direction vector, which the game only updates while
            // mouse aim + the center touch keep running. Blocking aim here left that
            // direction stale (boost fired but did not move right after morphing, and
            // steering during the roll was lost). ProcessAimInputMouse() and the
            // center-touch reset in HandleInGameLogic now run normally during boost.

            if (!IsDown(IB_WEAPON_CHECK)) {
                emuInstance->getNDS()->ReleaseScreen();
            }

            // ImmediateInputEdgeOverlay rewrites game input struct bits after
            // the game's poll. Mark this synthesized R press so overlay presets
            // that also manage R (for example Zoom) preserve the boost input.
            m_immediateOverlayPreserveMask =
                static_cast<uint16_t>(m_immediateOverlayPreserveMask | (1u << INPUT_R));
            InputSetBranchless(INPUT_R, !boostCooldownActive && gaugeEnough);

            return true;
        }

        return false;
    }

    COLD_FUNCTION void MelonPrimeCore::ApplyGameSettingsOnce()
    {
        InputSetBranchless(INPUT_L, !IsPressed(IB_UI_LEFT));
        InputSetBranchless(INPUT_R, !IsPressed(IB_UI_RIGHT));

        auto* nds = emuInstance->getNDS();

        MelonPrimeGameSettings::ApplyHeadphone(nds, localCfg, m_currentRom.operationAndSound);

        MelonPrimeGameSettings::ApplyMphSensitivity(
            nds, localCfg, m_currentRom.sensitivity,
            m_currentRom.baseInGameSensi, m_flags.test(StateFlags::BIT_IN_GAME_INIT));

        MelonPrimeGameSettings::ApplyUnlockHuntersMaps(
            nds, localCfg,
            m_currentRom.unlockMapsHunters, m_currentRom.unlockMapsHunters2,
            m_currentRom.unlockMapsHunters3, m_currentRom.unlockMapsHunters4,
            m_currentRom.unlockMapsHunters5);

        MelonPrimeGameSettings::UseDsName(nds, localCfg, m_currentRom.dsNameFlagAndMicVolume);
        MelonPrimeGameSettings::ApplySelectedHunterStrict(nds, localCfg, m_currentRom.mainHunter);
        MelonPrimeGameSettings::ApplyLicenseColorStrict(nds, localCfg, m_currentRom.rankColor);

        MelonPrimeGameSettings::ApplySfxVolume(nds, localCfg, m_currentRom.volSfx8Bit);
        MelonPrimeGameSettings::ApplyMusicVolume(nds, localCfg, m_currentRom.volMusic8Bit);
    }

} // namespace MelonPrime
