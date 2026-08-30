#include "MelonPrimeInternal.h"
#include "MelonPrimeGameSettings.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "main.h"
#include "Screen.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeGameRomAddrTable.h"
#ifdef MELONPRIME_DS
#include "MelonPrimePatchTouchScreenAimOnly.h"
#endif

#include <algorithm>
#include <cmath>

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
        //   Single bitmask test skips ProcessWeaponSwitch on 99%+ frames.
        //   Next/Prev secondary (default: mouse wheel) already OR into these bits.
        {
            constexpr uint64_t IB_WEAPON_ALL_TRIGGERS =
                IB_WEAPON_ANY | IB_WEAPON_NEXT | IB_WEAPON_PREV;
            const bool hasWeaponInput = (m_input.press & IB_WEAPON_ALL_TRIGGERS) != 0;
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
                // Weapon check opens the weapon radial menu, HUD region ID3,
                // which mirrors with the touch layout like every other in-match
                // rectangle.
                nds->TouchScreen(m_presetBindings.MirrorX(WEAPON_CHECK_START.x()),
                                 WEAPON_CHECK_START.y());
            }
        }
        else if (UNLIKELY(m_isWeaponCheckActive)) {
            HandleRareWeaponCheckEnd();
        }

        // --- Movement & Buttons (Hot Path) ---
        ProcessMoveAndButtonsFastFromReset();
        ApplyPostPollOverlayInput();
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

        // A native method must not fire while the local player is not in play
        // (killed, or not yet spawned), and it does nothing at all rather than
        // quietly falling through to the legacy simulation below. Only the
        // legacy path itself still runs then -- that is the game reacting to
        // its own simulated input.
        const bool localPlayerNotInPlay = !IsLocalPlayerAlive();
        // Spawn invulnerability window: every native method defers to the
        // Standard path below for its whole duration.
        const bool spawnWindow = IsLocalPlayerInSpawnInvulnerability();

#ifdef MELONPRIME_DS
        if (m_enableDirectInvocationTransform && !spawnWindow) {
            if (localPlayerNotInPlay)
                return;
            // "New Method 2": queue one mailbox request for the ARM9
            // DirectInvocation hook. It calls the game's own transform request
            // from the ProcessTouchInput call site -- no synthetic touch and no
            // NoAimInput write -- and raises HUD 0x16 itself on a Morph.
            if (m_flags.test(StateFlags::BIT_BATTLE_RUNTIME_MODE)
                && DirectInvocationHook_IsRomSupported(m_currentRom.romGroupIndex)
                && DirectInvocationHook_IsSiteValid(nds, m_currentRom.romGroupIndex))
            {
                QueueDirectInvocationTransform();
            }
            return;
        }
#endif

        if (m_enableDirectAltFormTransform && !spawnWindow) {
            if (localPlayerNotInPlay)
                return;
            // TransformGateHook redirects Gate A/B into the game's native
            // TransformRequest path. Keep a short pending window so a press is
            // not lost if the game reaches the transform gate a few frames late.
            m_directTransformPendingFrames = 10;
            return;
        }

        // Legacy touch-simulation approach: tap the Morph / Unmorph HUD button,
        // region ID4, centre (232,168) 48x48. Its X mirrors with the touch
        // layout -- the left-handed presets put it at X 0..47 instead of
        // 208..255 -- so the tap has to be mapped through the preset or it only
        // ever lands on Touch R and Dual R.
#ifdef MELONPRIME_DS
        // "Use the Whole Touch Screen for Aiming" neutralises exactly the
        // hit-test this tap depends on, so with that patch active the Standard
        // transform silently stops working. Opt-in: lift the patch for the tap
        // and put it straight back. Same shape as the NoDoubleTapJump patch the
        // legacy weapon path brackets its own touch sequence with.
        const bool liftTouchAimOnly = m_suspendTouchAimOnlyForTransform;
        if (UNLIKELY(liftTouchAimOnly))
            TouchScreenAimOnly_RestoreOnce(m_patchState, nds, m_currentRom.romGroupIndex);
#endif

        nds->ReleaseScreen();
        FrameAdvanceTwice();
        using namespace Consts::UI;
        nds->TouchScreen(m_presetBindings.MirrorX(MORPH_START.x()), MORPH_START.y());
        FrameAdvanceTwice();
        nds->ReleaseScreen();
        FrameAdvanceTwice();

#ifdef MELONPRIME_DS
        // Re-apply through the owning module so it re-reads config and stays
        // the single writer of that patch state.
        if (UNLIKELY(liftTouchAimOnly)) {
            TouchScreenAimOnly_ApplyOnce(
                m_patchState, nds, localCfg, m_currentRom.romGroupIndex);
        }
#endif
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
            ResetMorphBoostSwipePulseState();
            return false;
        }

        const bool isAltForm = (*m_ptrs.isAltForm) == 0x02;
        m_flags.assign(StateFlags::BIT_IS_ALT_FORM, isAltForm);

        const bool shiftAutoCycle = IsDown(IB_MORPH_BOOST);
        const bool manualRBoost = IsDown(IB_ZOOM);
        if (!isAltForm || isStylusMode) {
            ResetMorphBoostSwipePulseState();
        }

        // Mouse-mode Morph Ball swipe arbitration.
        // MELONPRIME_MORPH_BOOST_MODE_CONTROLS_V14
        //
        // Parent OFF:
        //   Disable mouse swipe boost by keeping CanTouchBoost clear. R/right-click
        //   and Shift auto-cycle remain on the native button-charge path.
        // Parent ON + custom OFF (default):
        //   Preserve the game's internal altSteerDelta swipe behavior. The custom
        //   raw movement setting is not consulted.
        // Parent ON + custom ON:
        //   Treat the current-frame raw mouse threshold as authoritative. Open
        //   CanTouchBoost for exactly one accepted pulse and close it on every
        //   unaccepted frame, including same-frame game-generated steer updates.
        if (isAltForm && !isStylusMode && m_ptrs.flags1) {
            constexpr int64_t kGameSwipeThresholdSq = 0x1FA4;
            constexpr int64_t kPromotionTargetSq = kGameSwipeThresholdSq + 0x100;
            constexpr uint32_t kCanTouchBoostBit = 0x08000000u;
            constexpr uint32_t kBoostingBit = 0x04000000u;
            constexpr uint8_t kPulseReady = 0;
            constexpr uint8_t kPulseAwaitBusy = 1;
            constexpr uint8_t kPulseBusyObserved = 2;
            constexpr uint8_t kPulseAcceptTimeoutFrames = 4;
            constexpr uint8_t kShiftGaugeReadyFrames = 0x0A;

            if (!m_enableMorphBoostSwipe) {
                ResetMorphBoostSwipePulseState();
                *m_ptrs.flags1 &= ~kCanTouchBoostBit;
            }
            else if (!m_enableMorphBoostCustomRawThreshold) {
                // Native/internal mode. Do not synthesize or normalize a raw vector.
                // Keep custom pulse state cold so changing modes cannot inherit a
                // stale cadence latch.
                ResetMorphBoostSwipePulseState();

                bool gameWouldSwipe = false;
                if (m_ptrs.altSteerDelta) {
                    const int32_t steerX = m_ptrs.altSteerDelta[0];
                    const int32_t steerY = m_ptrs.altSteerDelta[1];
                    const int64_t steerMagnitudeSq =
                        static_cast<int64_t>(steerX) * steerX
                        + static_cast<int64_t>(steerY) * steerY;
                    gameWouldSwipe = steerMagnitudeSq > kGameSwipeThresholdSq;
                }

                // A visible internal swipe wins for this frame, including while R
                // is held. Otherwise the clear bit exposes the native R charge path.
                const bool buttonBoost = manualRBoost
                    || shiftAutoCycle
                    || (*m_ptrs.boostGauge != 0);
                if (gameWouldSwipe || !buttonBoost)
                    *m_ptrs.flags1 |= kCanTouchBoostBit;
                else
                    *m_ptrs.flags1 &= ~kCanTouchBoostBit;
            }
            else {
                // Custom raw threshold mode. Threshold and direction both use the
                // current frame's m_input.mouseX/Y sample. ProcessAimInputMouse()
                // later consumes the same sample, so aim receives no added delay.
                const int32_t rawDx = m_input.mouseX;
                const int32_t rawDy = m_input.mouseY;
                const int64_t rawMagnitudeSq =
                    static_cast<int64_t>(rawDx) * rawDx
                    + static_cast<int64_t>(rawDy) * rawDy;
                const bool configuredMovementWouldSwipe = rawMagnitudeSq > 0
                    && rawMagnitudeSq
                        >= static_cast<int64_t>(m_morphBoostAssistThresholdSq);

                const bool boostBusy = ((*m_ptrs.flags1 & kBoostingBit) != 0)
                    || (m_ptrs.isBoosting && *m_ptrs.isBoosting != 0);

                if (m_morphBoostSwipePulseState != kPulseReady
                    && m_morphBoostSwipePulseElapsedFrames != 0xFF) {
                    ++m_morphBoostSwipePulseElapsedFrames;
                }

                if (m_morphBoostSwipePulseState == kPulseAwaitBusy) {
                    if (boostBusy) {
                        m_morphBoostSwipePulseState = kPulseBusyObserved;
                    }
                    else if (m_morphBoostSwipePulseElapsedFrames
                             > kPulseAcceptTimeoutFrames) {
                        ResetMorphBoostSwipePulseState();
                    }
                }
                else if (m_morphBoostSwipePulseState == kPulseBusyObserved) {
                    if (!boostBusy
                        && m_morphBoostSwipePulseElapsedFrames
                            > kShiftGaugeReadyFrames) {
                        ResetMorphBoostSwipePulseState();
                    }
                }
                else if (m_morphBoostSwipePulseState != kPulseReady) {
                    ResetMorphBoostSwipePulseState();
                }

                const bool pulseReady =
                    m_morphBoostSwipePulseState == kPulseReady;
                const bool emitCurrentFrameSwipePulse = configuredMovementWouldSwipe
                    && m_ptrs.altSteerDelta
                    && pulseReady
                    && !boostBusy
                    && !shiftAutoCycle;

                if (emitCurrentFrameSwipePulse) {
                    m_morphBoostSwipePulseState = kPulseAwaitBusy;
                    m_morphBoostSwipePulseElapsedFrames = 0;

                    const double scale = std::sqrt(
                        static_cast<double>(kPromotionTargetSq)
                        / static_cast<double>(rawMagnitudeSq));
                    const int32_t promotedX = std::clamp<int32_t>(
                        static_cast<int32_t>(std::lround(static_cast<double>(rawDx) * scale)),
                        -32768, 32767);
                    const int32_t promotedY = std::clamp<int32_t>(
                        static_cast<int32_t>(std::lround(static_cast<double>(rawDy) * scale)),
                        -32768, 32767);
                    m_ptrs.altSteerDelta[0] = static_cast<int16_t>(promotedX);
                    m_ptrs.altSteerDelta[1] = static_cast<int16_t>(promotedY);
                    *m_ptrs.flags1 |= kCanTouchBoostBit;
                }
                else {
                    // Authoritative custom gate. No internal/same-frame swipe may
                    // bypass the configured raw movement threshold.
                    *m_ptrs.flags1 &= ~kCanTouchBoostBit;
                }
            }
        }

        // Shift hold-to-boost auto-cycle remains unchanged and separate from the
        // mouse native-swipe pulse. Manual right-click R remains in
        // ApplyZoomBindingInput().
        if (!shiftAutoCycle) {
            return false;
        }

        if (isAltForm) {
            const uint8_t boostGauge = *m_ptrs.boostGauge;
            // NOTE: m_ptrs.isBoosting currently points at player+0x14A, which is
            // the Boost cooldown/busy timer (not player+0x4C4 bit26 Boosting).
            const bool boostCooldownActive = (*m_ptrs.isBoosting) != 0x00;
            const bool gaugeEnough = boostGauge > 0x0A;

            // Do NOT raise AIMBLK_MORPHBALL_BOOST. Boost speed is applied along the
            // current Morph Ball direction vector, which the game only updates while
            // mouse aim + the center touch keep running.
            if (!IsDown(IB_WEAPON_CHECK)) {
                emuInstance->getNDS()->ReleaseScreen();
            }

            // The boost button is preset-owned (R on the right-handed presets,
            // L on the left-handed ones), and the overlay must leave whatever
            // this synthesizes alone.
            const uint16_t boostMask = m_presetBindings.MorphBoost;
            m_immediateOverlayPreserveMask =
                static_cast<uint16_t>(m_immediateOverlayPreserveMask | boostMask);
            InputSetMaskBranchless(boostMask, !boostCooldownActive && gaugeEnough);

            return true;
        }

        return false;
    }

    void MelonPrimeCore::ReconcileMenuGameSettings()
    {
        InputSetBranchless(INPUT_L, !IsPressed(IB_UI_LEFT));
        InputSetBranchless(INPUT_R, !IsPressed(IB_UI_RIGHT));

        auto* nds = emuInstance->getNDS();

        MelonPrimeGameSettings::ApplyHeadphone(nds, m_menuGameSettings, m_currentRom.operationAndSound);

        MelonPrimeGameSettings::ApplyMphSensitivity(
            nds, m_menuGameSettings, m_currentRom.sensitivity,
            m_currentRom.baseInGameSensi, m_flags.test(StateFlags::BIT_IN_GAME_INIT));

        MelonPrimeGameSettings::ApplyUnlockHuntersMaps(
            nds, m_menuGameSettings,
            m_currentRom.unlockMapsHunters, m_currentRom.unlockMapsHunters2,
            m_currentRom.unlockMapsHunters3, m_currentRom.unlockMapsHunters4,
            m_currentRom.unlockMapsHunters5);

        MelonPrimeGameSettings::UseDsName(nds, m_menuGameSettings, m_currentRom.dsNameFlagAndMicVolume);
        MelonPrimeGameSettings::ApplySelectedHunterStrict(nds, m_menuGameSettings, m_currentRom.mainHunter);
        MelonPrimeGameSettings::ApplyLicenseColorStrict(nds, m_menuGameSettings, m_currentRom.rankColor);

        MelonPrimeGameSettings::ApplySfxVolume(nds, m_menuGameSettings, m_currentRom.volSfx8Bit);
        MelonPrimeGameSettings::ApplyMusicVolume(nds, m_menuGameSettings, m_currentRom.volMusic8Bit);
    }

} // namespace MelonPrime
