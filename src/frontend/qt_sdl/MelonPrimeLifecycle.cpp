#include "MelonPrimeInternal.h"
#include "MelonPrimeGameSettings.h"
#include "MelonPrimeRuntimeConfig.h"
#include "MelonPrimePatchLifecycle.h"
#include "EmuInstance.h"
#include "Screen.h"
#include "MelonPrimeDef.h"
#include "MelonPrimePlatformInput.h"
#include "MelonPrimeArm9Hook.h"
#include "MelonPrimePatchUseFirmwareLanguage.h"
#include "MelonPrimeInstanceDiagnostics.h"
#ifdef MELONPRIME_DS
#include "GPU2DNative.h"
#include "MelonPrimeWifiReconnectFix.h"
#endif

#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudPatchLifecycle.h"
#endif
#if defined(MELONPRIME_CUSTOM_HUD) || defined(MELONPRIME_DS)
#include "MelonPrimePatch.h"
#endif

#include <algorithm>

#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawHotkeyVkBinding.h"
#endif

namespace MelonPrime {

    MelonPrimeCore::~MelonPrimeCore()
    {
        InstanceDiagnostics::LogLifecycle(emuInstance, this, "destroying");
        PlatformInputOwnerService::Release(m_inputSubscription);
#ifdef _WIN32
        if (m_rawFilter && m_rawInputSubscription) {
            m_rawFilter->Unsubscribe(m_rawInputSubscription);
            m_rawInputSubscription = nullptr;
        }
#elif MELONPRIME_PLATFORM_RAW_FILTER_ENABLED
        PlatformInput_ReleaseRawFilter(m_platformRawFilter);
#endif
    }

    // Sole top-level RuntimeConfigSnapshot apply transaction. Responsibility
    // owners receive the resolved snapshot by const reference; in particular
    // ApplyAimRuntimeConfig owns Aim-derived fields and invalidation.
    void MelonPrimeCore::ApplyRuntimeConfigSnapshot(const RuntimeConfigSnapshot& s)
    {
        m_flags.assign(StateFlags::BIT_JOY2KEY, s.joy2Key);
        m_flags.assign(StateFlags::BIT_SNAP_TAP, s.snapTap);
        m_flags.assign(StateFlags::BIT_STYLUS_MODE, s.stylusMode);
        isStylusMode    = m_flags.test(StateFlags::BIT_STYLUS_MODE);
        m_snapTapMode   = m_flags.test(StateFlags::BIT_SNAP_TAP);
        m_enableStylusDirectAimWhileTouching =
            s.stylusDirectAimWhileTouching;

        m_enableImmediateInputEdgeOverlay = s.immediateInputEdgeOverlay;
        m_enableDirectAltFormTransform = s.directAltFormTransform;
        m_suspendTouchAimOnlyForTransform = s.touchScreenAimOnlySuspendForTransform;
        m_enableMorphBoostSwipe = s.morphBoostSwipeEnabled; // MELONPRIME_MORPH_BOOST_MODE_CONTROLS_V14
        m_enableMorphBoostCustomRawThreshold = s.morphBoostCustomRawThreshold;
        if (!m_enableDirectAltFormTransform)
            ResetDirectTransformInputState();
        m_enableNativeBipedFire = s.nativeBipedFire;
        if (!m_enableNativeBipedFire)
            ResetNativeBipedFireInputState();
        m_enableNativeZoomToggle = s.nativeZoomToggle;
        m_morphBoostAssistThresholdSq = s.morphBoostAssistThresholdSq;
        ResetMorphBoostSwipePulseState(); // MELONPRIME_MORPH_BOOST_SHIFT_CADENCE_SWIPE_V10

        if (!m_enableNativeZoomToggle) {
#ifdef MELONPRIME_DS
            m_nativeZoomPending.Clear();
#endif
        }
        // The pressed-edge latch and the cached scope state are shared by the
        // native toggle and the DirectInvocation zoom path (they are mutually
        // exclusive), so they only reset when neither is active.
        if (!m_enableNativeZoomToggle && !s.directInvocationZoom) {
            m_nativeZoomTogglePrevDown = false;
            m_nativeZoomLastKnownEnabled = false;
        }

#ifdef MELONPRIME_DS
        m_patchState.outOfGamePatches = s.outOfGamePatches;

        m_enableNativeWeaponSwitch = s.nativeWeaponSwitch;
        if (!m_enableNativeWeaponSwitch)
            m_weaponSwitchPending.Clear();

        m_enableDirectInvocationTransform = s.directInvocationTransform;
        m_enableDirectInvocationWeapon = s.directInvocationWeapon;
        m_enableDirectInvocationZoom = s.directInvocationZoom;
        if (!m_enableDirectInvocationTransform)
            m_directInvocationPending.ClearTransform();
        if (!m_enableDirectInvocationWeapon)
            m_directInvocationPending.ClearWeapon();
        if (!m_enableDirectInvocationZoom)
            m_directInvocationPending.ClearZoom();

        // Resolve the complete ARM9 activation policy once at the cold config
        // boundary. ARM9Hook_Install consumes this plan to select modules and
        // address masks; it never re-reads or reinterprets Config::Table.
        m_arm9HookActivationPlan.nativeAimHookMode =
            s.disableMphAimSmoothing ? s.nativeAimHookMode : 0;
        m_arm9HookActivationPlan.lowLatencyAimMode =
            (s.stylusMode && !s.stylusDirectAimWhileTouching)
                ? LowLatencyAimMode::Off
                : s.lowLatencyAimMode;
        m_arm9HookActivationPlan.immediateInputEdgeOverlay =
            s.immediateInputEdgeOverlay;
        m_arm9HookActivationPlan.nativeZoomToggle = s.nativeZoomToggle;
        m_arm9HookActivationPlan.nativeBipedFire = s.nativeBipedFire;
        m_arm9HookActivationPlan.directAltFormTransform =
            s.directAltFormTransform;
        m_arm9HookActivationPlan.nativeWeaponSwitch = s.nativeWeaponSwitch;
        m_arm9HookActivationPlan.directInvocation =
            s.directInvocationTransform || s.directInvocationWeapon
            || s.directInvocationZoom;
        m_arm9HookActivationPlan.shadowFreeze = s.fixShadowFreeze;
        m_arm9HookActivationPlan.noxusBladePersistence =
            s.fixNoxusBladePersistence;
#endif

        m_menuGameSettings = s.menuGameSettings;

        screenSyncMode = s.screenSyncMode;

        ApplyAimRuntimeConfig(s);
    }

    void MelonPrimeCore::ReloadConfigFlags()
    {
        const RuntimeConfigSnapshot snapshot =
            LoadRuntimeConfigSnapshot(localCfg);

        ApplyRuntimeConfigSnapshot(snapshot);
#ifdef MELONPRIME_DS
        // Firmware language is not a Config::Table value. This is the single
        // owner of its cached target: Initialize() and config reload/unpause
        // resolve the currently installed image, while OnEmuStart() runs
        // after EmuInstance::updateConsole() has installed a replacement
        // firmware during reset, ROM reopen, or a firmware-profile change.
        // There is no supported live firmware mutation outside those
        // emulation-thread boundaries; the menu reconciler only compares the
        // cached target against guest RAM afterward.
        m_patchState.outOfGamePatches.firmwareLanguageBits =
            UseFirmwareLanguage_ResolveTarget(emuInstance->getNDS());
#endif
        ReloadDamageNotifyPurpleConfig();
    }

    void MelonPrimeCore::Initialize()
    {
        ReloadConfigFlags();

#ifdef _WIN32
        SetupRawInput();
#elif MELONPRIME_PLATFORM_RAW_FILTER_ENABLED
        if (PlatformInput_ShouldAcquireRawFilter() && !m_platformRawFilter)
            m_platformRawFilter = PlatformInput_AcquireRawFilter();
#endif
    }

    void MelonPrimeCore::OnEmuStart()
    {
        InstanceDiagnostics::CheckEmuThread(emuInstance, "MelonPrimeCore::OnEmuStart");
        InstanceDiagnostics::LogLifecycle(emuInstance, this, "emu-start");
        const void* hookState = nullptr;
        const void* patchState = nullptr;
        const void* hudState = nullptr;
#ifdef MELONPRIME_DS
        hookState = &m_arm9HookState;
        patchState = &m_patchState;
#endif
#ifdef MELONPRIME_CUSTOM_HUD
        hudState = m_hudConfigState.get();
#endif
        InstanceDiagnostics::LogOwnedStates(
            emuInstance, hookState, patchState, hudState);
        m_flags.packed = 0;
        m_matchBlackWindow = {};
        m_postSavestateReconcilePending = false;
        // A restarted/reopened ROM begins in menu cursor mode. Supersede any
        // hide/capture request left by the previous match before its next GUI pass.
        isCursorMode = true;
        m_threadBridge.ResetCursorPresentationFromEmu();
#ifdef _WIN32
        if (m_rawFilter)
            m_rawFilter->UpdateOwner(m_rawInputSubscription, false);
#else
        PlatformInputOwnerService::Release(m_inputSubscription);
#endif
        m_zoomAimCanZoomCache = {};
        m_isLayoutChangePending = true;
        m_isWeaponCheckActive = false;
#ifdef MELONPRIME_CUSTOM_HUD
        CustomHud_ResetPatchState(*m_hudConfigState);
#endif
#ifdef MELONPRIME_DS
        m_weaponSwitchPending.Clear();
        m_directInvocationPending.Clear();
        PatchLifecycle::ResetForEmuStart(
            emuInstance->getNDS(), emuInstance, localCfg, m_currentRom, this);
#endif

        ReloadConfigFlags();
        ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));
        InputReset();
        // Intentional historical asymmetry: this startup profile leaves the
        // native Biped Fire latch untouched. Other lifecycle sites reset it at game/boot
        // boundaries; changing this would alter input reset timing and needs a
        // dedicated S7/S8 behavior pass.
        // weaponSwitchPending is cleared above (before ARM9Hook_Uninstall) where
        // ordering matters, so it is not part of this cluster call.
        ResetInputForLifecycleBoundary(InputLifecycleBoundary::EmuStart);
        ResetMorphBoostSwipePulseState(); // MELONPRIME_MORPH_BOOST_SHIFT_CADENCE_SWIPE_V10

        m_layoutGenerationSeen = 0;
        PublishUiSnapshot();
    }

    void MelonPrimeCore::ResetRuntimeStateForBoot()
    {
        m_flags.packed = 0;
        m_matchBlackWindow = {};
        m_postSavestateReconcilePending = false;
        isCursorMode = true;
        m_threadBridge.ResetCursorPresentationFromEmu();
        m_zoomAimCanZoomCache = {};
        m_isLayoutChangePending = true;
        m_isWeaponCheckActive = false;
        m_aimBlockBits = 0;
#ifdef MELONPRIME_CUSTOM_HUD
        CustomHud_ResetPatchState(*m_hudConfigState);
#endif
#ifdef MELONPRIME_DS
        m_weaponSwitchPending.Clear();
        m_directInvocationPending.Clear();
        PatchLifecycle::ResetForBoot(emuInstance->getNDS(), emuInstance, this);
#endif

        InputReset();
        // weaponSwitchPending cleared above (before ARM9Hook_Uninstall) where
        // ordering matters, so it is not part of this cluster call.
        ResetInputForLifecycleBoundary(InputLifecycleBoundary::Boot);
        ResetMorphBoostSwipePulseState(); // MELONPRIME_MORPH_BOOST_SHIFT_CADENCE_SWIPE_V10
        PublishUiSnapshot();
    }

    void MelonPrimeCore::OnEmuStop()
    {
        InstanceDiagnostics::CheckEmuThread(emuInstance, "MelonPrimeCore::OnEmuStop");
        InstanceDiagnostics::LogLifecycle(emuInstance, this, "emu-stop");
        m_flags.clear(StateFlags::BIT_IN_GAME);
        m_postSavestateReconcilePending = false;
        isCursorMode = true;
        m_threadBridge.ResetCursorPresentationFromEmu();
#ifdef _WIN32
        if (m_rawFilter)
            m_rawFilter->UpdateOwner(m_rawInputSubscription, false);
#else
        PlatformInputOwnerService::Release(m_inputSubscription);
#endif
        m_zoomAimCanZoomCache = {};
        // Intentional historical asymmetry: the stop profile clears transform/fire
        // transients but leaves aim residuals and overlay-held state alone.
        // OnEmuStart/boot perform broader resets; changing this stop-time
        // subset would need a dedicated S7/S8 behavior pass.
        // weaponSwitchPending is cleared in the DS block below (before
        // ARM9Hook_Uninstall).
        ResetInputForLifecycleBoundary(InputLifecycleBoundary::EmuStop);
        ResetMorphBoostSwipePulseState(); // MELONPRIME_MORPH_BOOST_SHIFT_CADENCE_SWIPE_V10
#ifdef MELONPRIME_CUSTOM_HUD
        if (m_flags.test(StateFlags::BIT_ROM_DETECTED)) {
            CustomHud_EnsurePatchRestored(
                *m_hudConfigState, emuInstance, localCfg, m_currentRom, m_playerPosition, false);
        }
        CustomHud_ResetPatchState(*m_hudConfigState);
#endif
#ifdef MELONPRIME_DS
        m_weaponSwitchPending.Clear();
        m_directInvocationPending.Clear();
        PatchLifecycle::RestoreForEmuStop(
            emuInstance->getNDS(),
            emuInstance,
            localCfg,
            m_currentRom,
            this);
#endif
        PublishUiSnapshot();
    }

    void MelonPrimeCore::OnEmuPause() {}

    void MelonPrimeCore::OnSavestateLoaded()
    {
#ifdef MELONPRIME_DS
        melonDS::GPU2DNative::NotifySavestateLoaded();
#endif
        // A savestate can land anywhere, including mid-match: the pre-match
        // full black this window keys off may already be in the past. Re-arm
        // the bootstrap so the next frame classifies the loaded state.
        m_matchBlackWindow = {};

        // Savestate restores the emulated timeline, not MelonPrime host-owned
        // lifecycle and patch bookkeeping. Invalidate host state now, then
        // let the next normal RunFrameHook rebuild it from loaded RAM. Focus
        // remains a host fact and is intentionally not changed here.
        ++m_timelineGeneration;
        if (m_timelineGeneration == 0)
            m_timelineGeneration = 1;
        m_postSavestateReconcilePending = true;
        m_flags.clear(StateFlags::BIT_IN_GAME_INIT);
        m_flags.clear(StateFlags::BIT_BATTLE_RUNTIME_MODE);
        m_flags.clear(StateFlags::BIT_BATTLE_RUNTIME_SEEN);
        m_flags.clear(StateFlags::BIT_END_OF_GAME_PATCH_RESTORED);
        ResetInputForLifecycleBoundary(InputLifecycleBoundary::SavestateLoad);
        ResetMorphBoostSwipePulseState();
        m_isWeaponCheckActive = false;
        m_nativeZoomTogglePrevDown = false;
        m_nativeZoomLastKnownEnabled = false;
#ifdef MELONPRIME_DS
        m_nativeZoomPending.Clear();
#endif
        m_zoomAimCanZoomCache = {};
        m_input = {};
        InputReset();

#ifdef MELONPRIME_DS
        // Host-only invalidation: do not restore pre-load guest RAM and do not
        // advance a synthetic frame. The next normal frame reads loaded RAM
        // and re-enters the existing join/battle lifecycle gates.
        PatchLifecycle::ReconcileAfterSavestateLoad(
            emuInstance->getNDS(), emuInstance, this);
#endif

#ifdef MELONPRIME_CUSTOM_HUD
        // Savestates replace emulated ARM9 RAM, but the native-HUD patch
        // tracker is host-owned and is not serialized. Invalidate it on the
        // emulation thread, then reconcile the loaded instructions immediately
        // with the CustomHUD settings that are active now. This ordering keeps
        // a stale native-HUD frame from escaping after F8 returns.
        CustomHud_ReconcilePatchAfterSavestateLoad(
            *m_hudConfigState,
            emuInstance,
            localCfg,
            m_currentRom,
            m_playerPosition);
#endif
    }

    // Called by EmuThread on every video-settings (re)apply, i.e. exactly where
    // 3D.ForceSoftwareOutsideMatch is already read. Owns the whole feature
    // boundary transition so the frame loop only ever sees a cached bool.
    COLD_FUNCTION void MelonPrimeCore::SetForceSoftwareOutsideMatchEnabled(bool enabled)
    {
        if (m_forceSoftwareOutsideMatch == enabled)
            return;

        m_forceSoftwareOutsideMatch = enabled;
        // Either direction invalidates the window: while off the state machine
        // was frozen, so its phase describes a moment that has since passed.
        m_matchBlackWindow = {};

        if (!enabled) {
            // Nothing consumes the bit now, and the caller re-resolves the
            // renderer on this same pass; leave it clear so a later re-enable
            // cannot inherit a stale "in match" answer.
            m_flags.clear(StateFlags::BIT_MATCH_BETWEEN_BLACKOUTS);
            return;
        }

        // Off->on: classify the current frame immediately (the bootstrap path
        // handles attaching mid-match) so the caller's renderer decision on
        // this very pass is already correct. Without this, enabling the option
        // during a match would force Software for one frame and then bounce the
        // renderer back on the next edge.
        if (m_flags.test(StateFlags::BIT_ROM_DETECTED)) {
            m_flags.assign(
                StateFlags::BIT_MATCH_BETWEEN_BLACKOUTS,
                BattleFlow::UpdateMatchBetweenBlackouts(
                    m_matchBlackWindow, m_matchTransitionPtrs));
        }
        else {
            m_flags.clear(StateFlags::BIT_MATCH_BETWEEN_BLACKOUTS);
        }
    }

    COLD_FUNCTION void MelonPrimeCore::ApplyConfigReload()
    {
        const bool oldJoy2Key = m_flags.test(StateFlags::BIT_JOY2KEY);
        ReloadConfigFlags();
        const bool newJoy2Key = m_flags.test(StateFlags::BIT_JOY2KEY);
        ApplyJoy2KeySupportAndQtFilter(newJoy2Key, oldJoy2Key != newJoy2Key);

#ifdef _WIN32
        if (m_rawFilter) {
            BindMetroidHotkeysFromConfig(
                m_rawFilter.get(), m_rawInputSubscription, emuInstance->getInstanceID());
            m_rawFilter->resetHotkeyEdges(m_rawInputSubscription);
        }
#endif

#ifdef MELONPRIME_DS
        PatchLifecycle::ReapplyForConfigReload(
            emuInstance->getNDS(),
            emuInstance,
            localCfg,
            m_currentRom,
            this,
            m_flags.test(StateFlags::BIT_ROM_DETECTED),
            m_flags.test(StateFlags::BIT_BATTLE_RUNTIME_MODE));

        // Toggling the reconnect fix takes effect from the next AP association; there
        // is nothing to undo, since the fix only ever restores a boot-fresh value.
        WifiReconnectFix_Publish(
            emuInstance->getNDS(),
            localCfg,
            m_flags.test(StateFlags::BIT_ROM_DETECTED) ? &m_currentRom : nullptr);
#endif
    }

    void MelonPrimeCore::OnEmuUnpause()
    {
        // ApplyJoy2KeySupportAndQtFilter runs with doReset=true (default),
        // executing resetAllKeys + resetMouseButtons + resetHotkeyEdges internally.
        // This clears stale bits from key-up events lost during pause.
        ReloadConfigFlags();
        ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));

        m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);

#ifdef _WIN32
        if (m_rawFilter) {
            // Reload VK bindings from config, then re-sync edge state.
            BindMetroidHotkeysFromConfig(
                m_rawFilter.get(), m_rawInputSubscription, emuInstance->getInstanceID());
            m_rawFilter->resetHotkeyEdges(m_rawInputSubscription);
        }
#endif

        if (m_flags.test(StateFlags::BIT_IN_GAME)) {
            m_flags.clear(StateFlags::BIT_IN_GAME_INIT);
            m_flags.clear(StateFlags::BIT_END_OF_GAME_PATCH_RESTORED);
            m_flags.clear(StateFlags::BIT_BATTLE_RUNTIME_MODE);
            m_flags.clear(StateFlags::BIT_BATTLE_RUNTIME_SEEN);
        }
    }

} // namespace MelonPrime
