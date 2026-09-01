#include "MelonPrimeInternal.h"
#include "MelonPrimeGameSettings.h"
#include "MelonPrimePatchAimSmoothing.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "NDS.h"
#include "GPU.h"
#include "main.h"
#include "Screen.h"
#include "MelonPrimeDef.h"
#include "MelonPrimePlatformInput.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeBattleFlowState.h"
#include "MelonPrimeRuntimeConfig.h"
#include "MelonPrimeInstanceDiagnostics.h"

#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudRuntime.h"
#include "MelonPrimeHudPatchLifecycle.h"
#endif
#if defined(MELONPRIME_CUSTOM_HUD) || defined(MELONPRIME_DS)
#include "MelonPrimePatch.h"
#endif
#ifdef MELONPRIME_DS
#include "MelonPrimePatchLifecycle.h"
#endif

#include <cmath>
#include <algorithm>

#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawHotkeyVkBinding.h"

namespace MelonPrime {
    // RawInputWinFilter is a refcounted singleton: Acquire()/Release() manage
    // the shared instance, so there is no per-pointer delete. This custom
    // deleter lets m_rawFilter (a unique_ptr) own one Acquire/Release pairing —
    // the raw pointer is only a non-owning handle, hence it is intentionally
    // ignored here; the singleton's own Release() does the teardown.
    void FilterDeleter::operator()(RawInputWinFilter* ptr) {
        if (ptr) RawInputWinFilter::Release();
    }
}
#endif

namespace MelonPrime {

    MelonPrimeCore::MelonPrimeCore(EmuInstance* instance)
        : emuInstance(instance)
        , localCfg(instance->getLocalConfig())
        , globalCfg(instance->getGlobalConfig())
    {
        m_flags.packed = 0;
        m_inputSubscription.Initialize(
            static_cast<uint64_t>(emuInstance->getInstanceID()));
#ifdef MELONPRIME_CUSTOM_HUD
        m_hudConfigState = CustomHud_CreateConfigState();
#endif
        InstanceDiagnostics::LogLifecycle(emuInstance, this, "constructed");
    }

#if defined(__APPLE__) || defined(__linux__)
    bool MelonPrimeCore::IsPlatformRawAimActive() const
    {
#if defined(__linux__)
        // hasReceivedMotion guards against sessions where XI2 selection
        // succeeds but raw events are never delivered (XWayland). Until the
        // first real raw delta arrives, the Qt fallback path owns aim.
#endif
        return PlatformInput_IsRawAimActive(m_platformRawFilter);
    }
#endif

#if defined(__APPLE__)
    bool MelonPrimeCore::IsGcMouseAimActive() const
    {
        return PlatformInput_IsGcMouseAimActive(m_platformRawFilter);
    }
#endif

    // =========================================================================
    // Runtime config and platform input setup
    // =========================================================================

    void MelonPrimeCore::SetFrameAdvanceFunc(std::function<void()> func)
    {
        m_frameAdvanceFunc = std::move(func);
        m_fnAdvance = m_frameAdvanceFunc
            ? &MelonPrimeCore::FrameAdvanceCustom
            : &MelonPrimeCore::FrameAdvanceDefault;
    }

    void MelonPrimeCore::SetupRawInput()
    {
#ifdef _WIN32
        if (m_rawFilter) return;

        m_cachedHwnd = reinterpret_cast<void*>(
            m_threadBridge.WindowHandleForEmu());
        m_rawFilter.reset(RawInputWinFilter::Acquire());
        m_rawInputSubscription = m_rawFilter->Subscribe(
            &m_inputSubscription,
            m_flags.test(StateFlags::BIT_JOY2KEY),
            static_cast<HWND>(m_cachedHwnd));

        ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));
        const RawHotkeyOwnership ownership = BindMetroidHotkeysFromConfig(
            m_rawFilter.get(), m_rawInputSubscription, emuInstance->getInstanceID());
        m_rawOwnedGameplayMask = ownership.rawOwnedGameplayMask;
        m_qtFallbackGameplayMask = ownership.qtFallbackGameplayMask;
#endif
    }

    // =========================================================================
    // REFACTORED: ApplyJoy2KeySupportAndQtFilter
    //
    // Qt native-filter installation is coordinated by the process-wide input
    // service according to the active subscription's Joy2Key request.
    // =========================================================================
    void MelonPrimeCore::ApplyJoy2KeySupportAndQtFilter(bool enable, bool doReset)
    {
#ifdef _WIN32
        if (!m_rawFilter) return;
        m_cachedHwnd = reinterpret_cast<void*>(
            m_threadBridge.WindowHandleForEmu());

        m_rawFilter->setRawInputTarget(
            m_rawInputSubscription, static_cast<HWND>(m_cachedHwnd));
        m_flags.assign(StateFlags::BIT_JOY2KEY, enable);

        m_rawFilter->setJoy2KeySupport(m_rawInputSubscription, enable);
        m_rawFilter->setQtFilterRequested(m_rawInputSubscription, enable);
        if (doReset) {
            // P-9 / resetAll: combined reset keeps the same semantics with one fence.
            m_rawFilter->resetAll(m_rawInputSubscription);
            m_rawFilter->resetHotkeyEdges(m_rawInputSubscription);
        }
#endif
    }

    // P-3: Moved from header -- requires complete EmuInstance type
    void MelonPrimeCore::NotifyLayoutChange()
    {
        m_threadBridge.NotifyLayoutChangeFromGui();
    }

    // =========================================================================
    // P-22: DeferredDrainInput — drain WM_INPUT queue after RunFrame.
    //
    // With P-19, each dispatched WM_INPUT triggers processRawInput in
    // HiddenWndProc, so draining also captures any straggler events.
    // Runs every frame to keep the queue clean.
    //
    // P-48: DeferredDrain also runs the stuck-state recovery scans
    // (clearStuckPostFrame) here, off the input→RunFrame latency path.
    // =========================================================================
    void MelonPrimeCore::DeferredDrainInput()
    {
#ifdef _WIN32
        if (m_rawFilter) {
            m_rawFilter->DeferredDrain(m_rawInputSubscription);
        }
#endif
    }

    void MelonPrimeCore::PublishUiSnapshot() noexcept
    {
        m_threadBridge.PublishRuntimeFromEmu(
            isCursorMode,
            isStylusMode,
            m_flags.test(StateFlags::BIT_IN_GAME),
            m_flags.test(StateFlags::BIT_ROM_DETECTED),
            isFastForward,
            m_rawAimActiveThisFrame,
            screenSyncMode);
    }

    void MelonPrimeCore::NotifyConfigChanged()
    {
        m_configReloadPending.store(true, std::memory_order_release);
    }

    void MelonPrimeCore::OnReset() { OnEmuStart(); }

    // =========================================================================
    // Per-frame hook and global hotkeys
    // =========================================================================

    // This projection stays in the RunFrameHook translation unit so the 99%+
    // no-release path remains force-inlined (two bit tests plus one branch).
    // Aim-derived mutation itself is delegated to the GameInput owner.
    FORCE_INLINE void MelonPrimeCore::HandleGlobalHotkeys()
    {
        constexpr uint64_t kSensiUpBit   = 1ULL << HK_MetroidIngameSensiUp;
        constexpr uint64_t kSensiDownBit = 1ULL << HK_MetroidIngameSensiDown;
        const uint64_t released = emuInstance->hotkeyRelease &
                                  (kSensiUpBit | kSensiDownBit);
        if (LIKELY(!released)) return;

        const int change = (released & kSensiUpBit) ? 1 : -1;
        const int cur = m_runtimeAimSensitivity;
        const int next = cur + change;

        if (next < 1) {
            emuInstance->osdAddMessage(0, "AimSensi cannot be decreased below 1");
        }
        else if (next != cur) {
            ApplyRuntimeAimSensitivity(next);
            m_threadBridge.RequestAimSensitivityPersistFromEmu(next);
            emuInstance->osdAddMessage(0, "AimSensi Updated: %d->%d", cur, next);
        }
    }

    HOT_FUNCTION void MelonPrimeCore::RunFrameHook()
    {
        // mainRAM removed - HandleGameJoinInit self-fetches (cold path only)

        if (UNLIKELY(m_isRunningHook)) {
            // Re-entrant path (called during FrameAdvanceOnce within weapon switch, morph, etc.)
            // Use the lean updater: no press-map scan, no wheel fetch.
            const auto guiPolicy =
                m_threadBridge.ReadGuiInputPolicyForEmu();
            UpdateInputStateReentrant(guiPolicy);
            ProcessMoveAndButtonsFastFromReset();
            ApplyPostPollOverlayInput();
            ApplyZoomBindingInput();

            const bool isStylusMode = this->isStylusMode;
            if (isStylusMode) {
                if (!m_flags.test(StateFlags::BIT_BLOCK_STYLUS)) {
                    if (m_enableStylusDirectAimWhileTouching
                        && m_stylusTouchKeyDown)
                    {
                        ProcessAimInputMouse();
                    }
                    else {
                        ProcessAimInputStylus(emuInstance->getNDS());
                    }
                }
            }
            else {
                ProcessAimInputMouse();
            }
            return;
        }

        // Config publication is a coalesced command. If the GUI stores true
        // after the relaxed empty check, the next normal frame applies it;
        // steady state avoids a locked exchange on every frame.
        if (UNLIKELY(m_configReloadPending.load(std::memory_order_relaxed))
            && m_configReloadPending.exchange(false, std::memory_order_acq_rel)) {
            ApplyConfigReload();
        }

        m_isRunningHook = true;

        // P-43: Cache isFocused in local variable.
        // After UpdateInputState / HandleGlobalHotkeys / HandleInGameLogic
        // (member function calls), the compiler must assume any member could
        // have changed, forcing a reload from memory. isFocused is written by
        // the GUI thread, so load once and use that value consistently for this
        // frame's input snapshot and focus transition.
        const auto guiPolicy = m_threadBridge.ReadGuiInputPolicyForEmu();
        const bool focused = guiPolicy.focused;

        // Input polling moved into UpdateInputState via UpdateOwnerAndSnapshot.
        UpdateInputState(guiPolicy);
        InputReset();
        m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);

        HandleGlobalHotkeys();

        if (UNLIKELY(!m_flags.test(StateFlags::BIT_ROM_DETECTED))) {
            DetectRomAndSetAddresses();
        }

        if (LIKELY(m_flags.test(StateFlags::BIT_ROM_DETECTED))) {
            if (UNLIKELY(m_postSavestateReconcilePending)) {
                // OnSavestateLoaded already invalidated host bookkeeping. This
                // normal-frame boundary now consumes the marker immediately
                // before loaded lifecycle reads; NDS::RunFrame has not started.
                m_postSavestateReconcilePending = false;
            }
            const bool isInGame = (*m_ptrs.inGame) == 0x0001;
            const bool wasInGame = m_flags.test(StateFlags::BIT_IN_GAME);
            m_flags.assign(StateFlags::BIT_IN_GAME, isInGame);

            // Match window: pre-match full black lifts -> ... -> post-match
            // fade reaches full black. One state-machine step per emulated
            // frame. The pointer bundle is resolved at ROM detect and the
            // update reads lazily; the steady state costs one u8 read.
            //
            // Gated on the feature that consumes it: with
            // 3D.ForceSoftwareOutsideMatch off nothing reads the window, so the
            // state machine (and its transitionType read) is skipped. The
            // window is re-bootstrapped on the off->on edge, so turning the
            // feature on mid-match still attaches correctly.
            if (UNLIKELY(m_forceSoftwareOutsideMatch)) {
                m_flags.assign(
                    StateFlags::BIT_MATCH_BETWEEN_BLACKOUTS,
                    BattleFlow::UpdateMatchBetweenBlackouts(
                        m_matchBlackWindow, m_matchTransitionPtrs));
            }

            // Join / rematch: legacy inGame rising edge, or unpause clearing INIT.
            // Rising edge always re-inits (lobby / rematch) even if RESTORED was left set.
            // Unpause re-init only while not in the post-match scoreboard window.
            if (isInGame && !m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
                if (!wasInGame
                    || !m_flags.test(StateFlags::BIT_END_OF_GAME_PATCH_RESTORED)) {
                    HandleGameJoinInit();
                }
            }

#ifdef MELONPRIME_DS
            // Cold: battle-runtime latch + match-end poll. One flags load; RAM reads:
            //   pre-latch lobby: currentMode only (skip flow until mode==0x0E)
            //   pre-latch, stage 1 already seen: local HP only
            //   post-latch live match: battleFlowState only
            const uint32_t matchLifecycleFlags = m_flags.packed;
            if (UNLIKELY((matchLifecycleFlags & StateFlags::BIT_IN_GAME_INIT)
                    && !(matchLifecycleFlags & StateFlags::BIT_END_OF_GAME_PATCH_RESTORED))) {
                if (!(matchLifecycleFlags & StateFlags::BIT_BATTLE_RUNTIME_MODE)) {
                    // Two stages, and they need not complete on the same frame.
                    //
                    // Stage 1 is sticky: the mode/flow pair only says the match
                    // is live, not that the local player exists in it yet, and
                    // it can stop reporting one again while the player is still
                    // waiting to spawn. Once seen, it stays seen.
                    if (!(matchLifecycleFlags & StateFlags::BIT_BATTLE_RUNTIME_SEEN)) {
                        const uint8_t mode = *m_ptrs.currentMode;
                        if (mode == BattleFlow::MODE_BATTLE_RUNTIME
                            && *m_ptrs.battleFlowState == BattleFlow::FLOW_ACTIVE_MATCH)
                        {
                            m_flags.set(StateFlags::BIT_BATTLE_RUNTIME_SEEN);
                        }
                    }
                    // Stage 2 completes the latch on the first frame the local
                    // player is in play, so the battle-runtime registry patches,
                    // the match ARM9 hooks and the guest trampolines they author
                    // all land on a spawned player. An unresolved HP pointer
                    // reads as in play, so this can never stall on a cold cache.
                    if (m_flags.test(StateFlags::BIT_BATTLE_RUNTIME_SEEN)
                        && IsLocalPlayerAlive())
                    {
                        HandleBattleRuntimeEnter();
                    }
                } else {
                    const uint8_t flowState = *m_ptrs.battleFlowState;
                    if (flowState != BattleFlow::FLOW_ACTIVE_MATCH) {
                        // PatchLifecycle Step 3 / Site A — see
                        // melonprime_patch_lifecycle_gateway_step3_plan.md.
                        // The RESTORED flag write stays here (frame-state
                        // ownership), not in PatchLifecycle.
                        PatchLifecycle::RestoreOnMatchEnd(
                            emuInstance->getNDS(), emuInstance, localCfg, m_currentRom, this);
                        m_flags.set(StateFlags::BIT_END_OF_GAME_PATCH_RESTORED);
                    }
                }
            }
#endif

            if (LIKELY(isInGame)) {
                if (m_flags.test(StateFlags::BIT_BATTLE_RUNTIME_MODE)) {
                    // PatchLifecycle Site C — explicit non-goal (pattern B: the game
                    // overwrites the RAM this patch owns, so it must be re-applied
                    // every in-game frame). Bypasses the registry/gateway on purpose;
                    // see melonprime-srp-performance-contract.md's "Never mix" list
                    // and melonprime-srp-v3-completion-summary.md's Deferred table.
                    OsdColor_ApplyOnce(m_patchState, emuInstance, localCfg, m_currentRom);
                }
#ifdef MELONPRIME_CUSTOM_HUD
                // Before RunFrame: hold the helmet layers off across the spawn
                // window. The game's own clamp (patched helmet site) early-outs
                // during spawn states, so init writers can briefly restore the
                // BG1-3 layers and flash the native visor for a frame.
                CustomHud_ClampHelmetLayersPreFrame(
                    *m_hudConfigState,
                    emuInstance,
                    m_currentRom,
                    m_playerPosition);
#endif
                // Damage Notify Purple — runs whether or not the window is focused
                // so HP drops during alt-tab still emit the purple flash.
                if (m_damageNotifyPurpleEnabled)
                    DamageNotifyPurpleTick();
            }
            else if (!isInGame
                && (m_flags.test(StateFlags::BIT_IN_GAME_INIT)
                    || m_flags.test(StateFlags::BIT_END_OF_GAME_PATCH_RESTORED))) {
                m_flags.clear(StateFlags::BIT_IN_GAME_INIT);
                m_flags.clear(StateFlags::BIT_END_OF_GAME_PATCH_RESTORED);
                m_flags.clear(StateFlags::BIT_BATTLE_RUNTIME_MODE);
                m_flags.clear(StateFlags::BIT_BATTLE_RUNTIME_SEEN);
#ifdef MELONPRIME_DS
                // PatchLifecycle Step 3 / Site D — hook deactivation only.
                // Flag clears and the transient-input / HUD / weapon-switch
                // cleanup stay in RunFrameHook because they are frame-state /
                // per-subsystem ownership.
                PatchLifecycle::DeactivateHooksOnLeaveInGame(
                    emuInstance->getNDS(), emuInstance, localCfg, m_currentRom, this);
#endif
                // weaponSwitchPending cleared in the DS block below where ordering matters.
                ResetInputForLifecycleBoundary(InputLifecycleBoundary::GameLeave);
                ResetMorphBoostSwipePulseState(); // MELONPRIME_MORPH_BOOST_SHIFT_CADENCE_SWIPE_V10
#ifdef MELONPRIME_CUSTOM_HUD
                CustomHud_EnsurePatchRestored(
                    *m_hudConfigState, emuInstance, localCfg, m_currentRom, m_playerPosition, false);
#endif
#ifdef MELONPRIME_DS
                m_weaponSwitchPending.Clear();
                m_directInvocationPending.Clear();
#endif
            }

            if (focused) {
                if (LIKELY(isInGame)) {
                    if (UNLIKELY(m_aimBlockBits & AIMBLK_NOT_IN_GAME)) {
                        SetAimBlockBranchless(AIMBLK_NOT_IN_GAME, false);
                    }
                    HandleInGameLogic();
                }
                else {
                    m_flags.clear(StateFlags::BIT_IN_ADVENTURE);
                    SetAimBlockBranchless(AIMBLK_NOT_IN_GAME, true);
#ifdef MELONPRIME_DS
                    // Per-frame menu site: direct out-of-game dispatch keeps
                    // unrelated registry entries off this focused hot path.
                    // PatchLifecycle Step 3 / Site E — see
                    // melonprime_patch_lifecycle_gateway_step3_plan.md.
                    PatchLifecycle::ApplyOutOfGameFrame(
                        emuInstance->getNDS(), emuInstance, localCfg, m_currentRom, this);
#endif
                    // Out-of-game screens (e.g. the Adventure planet/region map)
                    // still accept WASD movement so the player can navigate.
                    // Movement only — fire/jump/aim stay released and cursor mode
                    // keeps driving the touch screen for menu selection.
                    //
                    // Order matters: ProcessMovementOnlyFromReset() does a full
                    // m_inputMaskFast assignment (releases every non-D-pad button),
                    // so it must run BEFORE ReconcileMenuGameSettings(), which applies
                    // the UI Left/Right buttons (License L/R, Adventure left/right)
                    // on top via single-bit InputSetBranchless. Running it after
                    // wiped those bits and broke the Hunter License L/R navigation.
                    ProcessMovementOnlyFromReset();
                    ReconcileMenuGameSettings();
                }

                const bool isAdventure = m_flags.test(StateFlags::BIT_IN_ADVENTURE);
                const bool isPaused = m_flags.test(StateFlags::BIT_PAUSED);
                const bool shouldBeCursorMode = !isInGame || (isAdventure && isPaused);

                if (UNLIKELY(shouldBeCursorMode != isCursorMode)) {
                    isCursorMode = shouldBeCursorMode;
                    SetAimBlockBranchless(AIMBLK_CURSOR_MODE, isCursorMode);
                    if (!isStylusMode) ShowCursor(isCursorMode);
                }

                if (isCursorMode) {
                    if (emuInstance->isTouching)
                        emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
                    else
                        emuInstance->getNDS()->ReleaseScreen();
                }
                InputSetBranchless(INPUT_START, !IsMetroidMenuHeld());
            }

            // Focus transition: reset input state + raw input layer.
            // Multi-layer defense with FIX-2 (UpdateInputState) and FIX-1 (HiddenWndProc).
            if (UNLIKELY(m_flags.test(StateFlags::BIT_LAST_FOCUSED) != focused)) {
                m_flags.assign(StateFlags::BIT_LAST_FOCUSED, focused);
                if (!focused) {
                    m_input.down = 0;
                    m_input.press = 0;
                    m_input.moveIndex = 0;
                    // weaponSwitchPending cleared in the DS block below.
                    ResetInputForLifecycleBoundary(InputLifecycleBoundary::FocusLoss);
                    ResetMorphBoostSwipePulseState(); // MELONPRIME_MORPH_BOOST_SHIFT_CADENCE_SWIPE_V10
#ifdef _WIN32
                    // P-9: Single call replaces resetAllKeys + resetMouseButtons
                    // (one fence instead of two)
                    if (m_rawFilter) {
                        m_rawFilter->resetAll(m_rawInputSubscription);
                    }
#elif defined(__APPLE__) || defined(__linux__)
                    // Drop raw deltas accumulated while unfocused so refocus
                    // cannot produce an aim jump (same intent as FIX-3).
                    PlatformInput_ResetRawFilter(
                        m_platformRawFilter, m_inputSubscription);
#endif
#ifdef MELONPRIME_DS
                    m_weaponSwitchPending.Clear();
                    m_directInvocationPending.Clear();
#endif
                }
            }
        }
        if (m_directTransformPendingFrames != 0) {
            if (!focused
                || !m_flags.test(StateFlags::BIT_IN_GAME)
                || !m_enableDirectAltFormTransform)
            {
                m_directTransformPendingFrames = 0;
            }
            else {
                --m_directTransformPendingFrames;
            }
        }
#ifdef MELONPRIME_DS
        if (m_weaponSwitchPending.IsValid()) {
            if (!focused || !m_flags.test(StateFlags::BIT_IN_GAME)) {
                m_weaponSwitchPending.Clear();
            }
            else if (m_weaponSwitchPending.FallbackFrames != 0) {
                --m_weaponSwitchPending.FallbackFrames;
            }
            else {
                m_weaponSwitchPending.Clear();
            }
        }
        // "New Method 2" mailbox TTL. The hook site is skipped while the
        // player's +0x84E gate is set, so a request is held for a few frames
        // and then dropped rather than firing after a respawn or a menu.
        if (m_directInvocationPending.IsValid()) {
            const bool alive = focused && m_flags.test(StateFlags::BIT_IN_GAME);
            if (!alive || !m_enableDirectInvocationTransform) {
                m_directInvocationPending.ClearTransform();
            }
            else if (m_directInvocationPending.TransformFrames != 0
                     && --m_directInvocationPending.TransformFrames == 0) {
                m_directInvocationPending.ClearTransform();
            }
            if (!alive || !m_enableDirectInvocationWeapon) {
                m_directInvocationPending.ClearWeapon();
            }
            else if (m_directInvocationPending.WeaponFrames != 0
                     && --m_directInvocationPending.WeaponFrames == 0) {
                m_directInvocationPending.ClearWeapon();
            }
            if (!alive || !m_enableDirectInvocationZoom) {
                m_directInvocationPending.ClearZoom();
            }
            else if (m_directInvocationPending.ZoomFrames != 0
                     && --m_directInvocationPending.ZoomFrames == 0) {
                m_directInvocationPending.ClearZoom();
            }
        }
#endif
        PublishUiSnapshot();
        m_isRunningHook = false;
    }

    // =========================================================================
    // HandleGameJoinInit - outlined from RunFrameHook
    //
    // Executes once per game-join (every ~tens of seconds).
    // COLD_FUNCTION ensures separate text section, no register pollution.
    // =========================================================================
    COLD_FUNCTION void MelonPrimeCore::HandleGameJoinInit()
    {
        melonDS::NDS* const nds = emuInstance->getNDS();
        melonDS::u8* const mainRAM = nds->MainRAM;
        m_flags.clear(StateFlags::BIT_END_OF_GAME_PATCH_RESTORED);
        m_flags.clear(StateFlags::BIT_BATTLE_RUNTIME_MODE);
        m_flags.clear(StateFlags::BIT_BATTLE_RUNTIME_SEEN);
        m_flags.set(StateFlags::BIT_IN_GAME_INIT);
        ResetInputForLifecycleBoundary(InputLifecycleBoundary::GameJoin);
        ResetMorphBoostSwipePulseState(); // MELONPRIME_MORPH_BOOST_SHIFT_CADENCE_SWIPE_V10
        m_playerPosition = Read8(mainRAM, m_currentRom.playerPos);

        const uint32_t offP = static_cast<uint32_t>(m_playerPosition) * Consts::PLAYER_ADDR_INC;
        const uint32_t playerBase = m_currentRom.playerStructStart + offP;

        // Control-preset snapshot. Read once here, per game join: these are
        // preset assignments, not per-frame state. Everything MelonPrime
        // synthesizes into KEYINPUT or overlays into player+0x464 derives from
        // this, so the left-handed and Dual presets work as well as Touch R.
        //
        // Resolved from the ROM's own upstream source rather than from the
        // player struct. player+0x364 is *downstream* state: player init calls
        // 0200CC7C(player, id), which expands ControlPresetTable[id] into it.
        // Reading the player struct here races that init, and a pre-init read
        // returns zeros -- which PickButton() would then silently resolve to
        // the Touch R defaults, i.e. exactly the bug this is meant to fix.
        //
        //   ControlTypeArray[slot]      u8 preset id, 0..3 human, 4 BOT
        //     -> ControlPresetTable[id] static 0x9C record, same layout the
        //                               game copies to player+0x364
        //
        // The id array is what the ROM's own runtime reader consults and what
        // the WiFi slot-state packet writes, so it is correct for a client too,
        // not only for the match host.
        // The caller's only job is to pick *which* record; the record layout
        // itself belongs to PresetButtonBindings.
        using PresetRec = PresetButtonBindings;
        uint32_t recordBase = playerBase + 0x364u;  // fallback: the expanded copy
        int presetId = -1;
        {
            const uint32_t idAddr = m_currentRom.controlTypeArray
                                  + static_cast<uint32_t>(m_playerPosition);
            if (idAddr >= 0x02000000u && idAddr <= 0x023FFFFFu) {
                const uint8_t id = Read8(mainRAM, idAddr);
                // 4 is BOT, which is also what an unused slot still carries, so
                // it is never the local human player's preset.
                if (id < 4u) {
                    presetId = static_cast<int>(id);
                    recordBase = m_currentRom.controlPresetTable
                               + static_cast<uint32_t>(id) * PresetRec::RecordSize;
                }
            }
        }
        m_presetBindings.BuildFromRecord(mainRAM, recordBase);

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
        // One line per match join, so the resolved preset is verifiable on the
        // real machine instead of inferred. Every button MelonPrime synthesizes
        // is in here; if a preset ever reads back as the Touch R defaults when
        // it should not, the snapshot is what to look at first.
        //   Touch R  move 0020/0010/0040/0080  jump 0002  fire 0200  zoom 0100
        //   Touch L  move 0800/0001/0400/0002  jump 0010  fire 0100  zoom 0200
        //   Dual R   move 0020/0010/0040/0080  jump 0100  fire 0200  zoom 0004
        //   Dual L   move 0800/0001/0400/0002  jump 0200  fire 0100  zoom 0004
        emuInstance->osdAddMessage(
            0,
            "preset id %d %s %s move %04X/%04X/%04X/%04X jump %04X fire %04X zoom %04X boost %04X",
            presetId,
            m_presetBindings.MirrorTouchX ? "mirrored" : "normal",
            m_presetBindings.UsesTouchAim ? "touch-aim" : "dual-aim",
            static_cast<unsigned>(m_presetBindings.MoveL),
            static_cast<unsigned>(m_presetBindings.MoveR),
            static_cast<unsigned>(m_presetBindings.MoveF),
            static_cast<unsigned>(m_presetBindings.MoveB),
            static_cast<unsigned>(m_presetBindings.Jump),
            static_cast<unsigned>(m_presetBindings.Fire),
            static_cast<unsigned>(m_presetBindings.Zoom),
            static_cast<unsigned>(m_presetBindings.MorphBoost));
#endif

        const uint32_t offA = static_cast<uint32_t>(m_playerPosition) * Consts::AIM_ADDR_INC;

        m_addrHot.isAltForm = m_currentRom.baseIsAltForm + offP;
        m_addrHot.loadedSpecialWeapon = m_currentRom.baseLoadedSpecialWeapon + offP;
        m_addrHot.weaponChange = m_currentRom.baseWeaponChange + offP;
        m_addrHot.selectedWeapon = m_currentRom.baseSelectedWeapon + offP;
        m_addrHot.jumpFlag = m_currentRom.baseJumpFlag + offP;
        m_addrHot.currentWeapon = m_currentRom.baseCurrentWeapon + offP;
        m_addrHot.havingWeapons = m_currentRom.baseHavingWeapons + offP;
        m_addrHot.weaponAmmo = m_currentRom.baseWeaponAmmo + offP;
        m_addrHot.boostGauge = m_currentRom.boostGauge + offP;
        m_addrHot.isBoosting = m_currentRom.isBoosting + offP;
        m_addrHot.isInVisorOrMap = m_currentRom.isInVisorOrMap + offP;
        m_addrHot.isMapOrUserActionPaused = m_currentRom.isMapOrUserActionPaused;

        m_addrHot.aimX = m_currentRom.baseAimX + offA;
        m_addrHot.aimY = m_currentRom.baseAimY + offA;

        m_addrHot.chosenHunter = m_currentRom.baseChosenHunter + m_playerPosition * 0x01u;
        m_addrHot.inGameSensi = m_currentRom.baseInGameSensi + m_playerPosition * 0x04u;

        m_ptrs.isAltForm = GetRamPointer<uint8_t>(mainRAM, m_addrHot.isAltForm);
        m_ptrs.jumpFlag = GetRamPointer<uint8_t>(mainRAM, m_addrHot.jumpFlag);
        m_ptrs.weaponChange = GetRamPointer<uint8_t>(mainRAM, m_addrHot.weaponChange);
        m_ptrs.selectedWeapon = GetRamPointer<uint8_t>(mainRAM, m_addrHot.selectedWeapon);
        m_ptrs.currentWeapon = GetRamPointer<uint8_t>(mainRAM, m_addrHot.currentWeapon);
        m_ptrs.havingWeapons = GetRamPointer<uint16_t>(mainRAM, m_addrHot.havingWeapons);
        m_ptrs.weaponAmmo = GetRamPointer<uint32_t>(mainRAM, m_addrHot.weaponAmmo);
        m_ptrs.boostGauge = GetRamPointer<uint8_t>(mainRAM, m_addrHot.boostGauge);
        m_ptrs.isBoosting = GetRamPointer<uint8_t>(mainRAM, m_addrHot.isBoosting);
        m_ptrs.loadedSpecialWeapon = GetRamPointer<uint8_t>(mainRAM, m_addrHot.loadedSpecialWeapon);
        m_ptrs.aimX = GetRamPointer<uint16_t>(mainRAM, m_addrHot.aimX);
        m_ptrs.aimY = GetRamPointer<uint16_t>(mainRAM, m_addrHot.aimY);
        // Pick the aim field the active control preset's path actually reads.
        // A Dual preset skips the touch producer entirely, so aimX/aimY would
        // be written into a chain nothing consumes; see WriteAimDelta().
        if (m_presetBindings.UsesTouchAim) {
            m_ptrs.dualAim = nullptr;
            m_ptrs.aimSens = nullptr;
        }
        else {
            m_ptrs.dualAim = GetRamPointer<int32_t>(mainRAM, playerBase + 0xE4u);
            m_ptrs.aimSens = GetRamPointer<int32_t>(mainRAM, playerBase + 0x3F8u);
        }
        m_ptrs.isInVisorOrMap = GetRamPointer<uint8_t>(mainRAM, m_addrHot.isInVisorOrMap);
        m_ptrs.isMapOrUserActionPaused = GetRamPointer<uint8_t>(mainRAM, m_addrHot.isMapOrUserActionPaused);
        // Damage Notify Purple: cache local-player HP and Double Damage timer pointers.
        m_ptrs.health = GetRamPointer<uint16_t>(mainRAM, m_currentRom.playerHP + offP);
        m_ptrs.doubleDamageTimer = GetRamPointer<uint16_t>(mainRAM, m_currentRom.playerDoubleDamageTimer + offP);
        // Weavel-only effective HP — these are read every frame but only consulted
        // when BIT_IS_WEAVEL is set, so resolving them unconditionally is fine.
        m_ptrs.flags1         = GetRamPointer<uint32_t>(mainRAM, playerBase + 0x4C4u);
        m_ptrs.altSteerDelta  = GetRamPointer<int16_t>(mainRAM, playerBase + 0x464u + 0x2Au);
        m_ptrs.moreFlags      = GetRamPointer<uint32_t>(mainRAM, playerBase + 0x4C8u);
        m_ptrs.weavelProxyPtr = GetRamPointer<uint32_t>(mainRAM, playerBase + 0xF24u);
        m_damageNotifyPurpleState = {};

        const uint8_t hunterID = Read8(mainRAM, m_addrHot.chosenHunter);
        m_hunterID = (hunterID <= 6) ? hunterID : 0;
        const bool isAdventure = Read8(mainRAM, m_currentRom.isInAdventure) == 0x02;
        // In Adventure mode the player is always Samus regardless of the stored multiplayer hunter ID.
        m_flags.assign(StateFlags::BIT_IS_SAMUS, isAdventure || hunterID == 0x00);
        m_flags.assign(StateFlags::BIT_IS_WEAVEL, !isAdventure && hunterID == 0x06);
        m_flags.assign(StateFlags::BIT_IN_ADVENTURE, isAdventure);

        MelonPrimeGameSettings::ApplyMphSensitivity(
            nds, m_menuGameSettings, m_currentRom.sensitivity, m_addrHot.inGameSensi, true);

        AimSmoothing_ApplyOrRestore(nds, m_currentRom, m_disableMphAimSmoothing);

#ifdef MELONPRIME_DS
        // Game-join patches only (aspect ratio). Battle-runtime patches/hooks wait for mode 0x0E.
        Patches_Apply(PatchSite_GameJoin,
                      PatchCtx{ nds, emuInstance, localCfg, m_currentRom, m_patchState });
#endif
#ifdef MELONPRIME_CUSTOM_HUD
        // Cache battle settings for HUD display
        CustomHud_OnMatchJoin(*m_hudConfigState, mainRAM, m_currentRom);
#endif
    }

    COLD_FUNCTION void MelonPrimeCore::HandleBattleRuntimeEnter()
    {
        m_flags.set(StateFlags::BIT_BATTLE_RUNTIME_MODE);
#ifdef MELONPRIME_DS
        // PatchLifecycle Step 3 / Site B — see
        // melonprime_patch_lifecycle_gateway_step3_plan.md.
        // Anything queued before the match actually started is dropped here:
        // the queue gates below only accept requests once this flag is set, and
        // a stale one must not fire on the first battle-runtime frame.
        m_directInvocationPending.Clear();
        PatchLifecycle::ApplyOnBattleRuntimeEnter(
            emuInstance->getNDS(), emuInstance, localCfg, m_currentRom, this,
            m_enableNativeWeaponSwitch,
            m_enableDirectInvocationTransform || m_enableDirectInvocationWeapon
                || m_enableDirectInvocationZoom);
#endif
    }

    void MelonPrimeCore::ShowCursor(bool show)
    {
        if (!show) {
#if defined(__APPLE__) || defined(__linux__)
            PlatformInput_ResetRawFilter(
                m_platformRawFilter, m_inputSubscription);
#endif
        }
        const uint32_t additionalRequests = show
            ? MelonPrimeThreadBridge::GuiRequestNone
            : (MelonPrimeThreadBridge::GuiRequestRecenter
               | MelonPrimeThreadBridge::GuiRequestRefreshCapture);
        m_threadBridge.RequestCursorVisibilityFromEmu(
            show, additionalRequests);
    }

    void MelonPrimeCore::FrameAdvanceCustom() { m_frameAdvanceFunc(); }

    void MelonPrimeCore::FrameAdvanceDefault()
    {
        emuInstance->inputProcess(true);
        if (emuInstance->usesOpenGL()) emuInstance->makeCurrentGL();

        auto& renderer = emuInstance->getNDS()->GPU.GetRenderer();
        if (renderer.NeedsShaderCompile()) {
            int cur, total;
            renderer.ShaderCompileStep(cur, total);
        }
        else {
            emuInstance->getNDS()->RunFrame();
        }

        if (emuInstance->usesOpenGL()) emuInstance->drawScreen();
    }

    void MelonPrimeCore::FrameAdvanceTwice()
    {
        FrameAdvanceOnce();
        FrameAdvanceOnce();
    }

    QPoint MelonPrimeCore::GetAdjustedCenter()
    {
        int x = 0;
        int y = 0;
        m_threadBridge.ReadCenterForEmu(x, y);
        return QPoint(x, y);
    }

} // namespace MelonPrime
