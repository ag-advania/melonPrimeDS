#include "MelonPrimeInternal.h"
#include "MelonPrimeGameSettings.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "NDS.h"
#include "GPU.h"
#include "main.h"
#include "Screen.h"
#include "Platform.h"
#include "MelonPrimeDef.h"
#include "MelonPrimePlatformInput.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeBattleFlowState.h"

#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudRender.h"
#endif
#if defined(MELONPRIME_CUSTOM_HUD) || defined(MELONPRIME_DS)
#include "MelonPrimePatch.h"
#endif

#include <cmath>
#include <algorithm>
#include <QCoreApplication>

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

    [[nodiscard]] static int NormalizeScreenSyncMode(int mode) noexcept
    {
#ifdef _WIN32
        return (mode >= 0 && mode <= 2) ? mode : 0;
#else
        return (mode == 1) ? 1 : 0;
#endif
    }

    MelonPrimeCore::MelonPrimeCore(EmuInstance* instance)
        : emuInstance(instance)
        , localCfg(instance->getLocalConfig())
        , globalCfg(instance->getGlobalConfig())
    {
        m_flags.packed = 0;
    }

#if defined(__APPLE__) || defined(__linux__)
    MelonPrimeCore::~MelonPrimeCore()
    {
        PlatformInput_ReleaseRawFilter(m_platformRawFilter);
    }

#if defined(__linux__)
    bool MelonPrimeCore::IsLinuxRawAimActive() const
    {
        // hasReceivedMotion guards against sessions where XI2 selection
        // succeeds but raw events are never delivered (XWayland). Until the
        // first real raw delta arrives, the Qt fallback path owns aim.
        return PlatformInput_IsRawAimActive(m_platformRawFilter);
    }
#endif
#else
    MelonPrimeCore::~MelonPrimeCore() = default;
#endif

    // =========================================================================
    // Runtime config and platform input setup
    // =========================================================================

    void MelonPrimeCore::ReloadConfigFlags()
    {
        m_flags.assign(StateFlags::BIT_JOY2KEY, localCfg.GetBool(CfgKey::Joy2Key));
        m_flags.assign(StateFlags::BIT_SNAP_TAP, localCfg.GetBool(CfgKey::SnapTap));
        m_flags.assign(StateFlags::BIT_STYLUS_MODE, localCfg.GetBool(CfgKey::StylusMode));
        isStylusMode    = m_flags.test(StateFlags::BIT_STYLUS_MODE);
        m_snapTapMode   = m_flags.test(StateFlags::BIT_SNAP_TAP);

        m_disableMphAimSmoothing = localCfg.GetBool(CfgKey::DisableMphAimSmoothing);
        m_enableAimAccumulator = localCfg.GetBool(CfgKey::AimAccumulator);
        m_nativeAimHookMode = static_cast<int8_t>(localCfg.GetInt(CfgKey::NativeAimHookMode));
#ifndef MELONPRIME_ENABLE_DEVELOPER_FEATURES
        m_nativeAimHookMode = 0;
#endif
        m_enableNativeAimDeltaHook = (m_nativeAimHookMode != 0);
        int lowLatencyAimMode =
            std::clamp(localCfg.GetInt(CfgKey::LowLatencyAimMode),
                       LowLatencyAimMode::Off,
                       LowLatencyAimMode::InstantAimFollow);
        if (lowLatencyAimMode == LowLatencyAimMode::Off
            && localCfg.GetBool(CfgKey::InstantAimFollow))
            lowLatencyAimMode = LowLatencyAimMode::ImmediateSync;
#ifndef MELONPRIME_ENABLE_DEVELOPER_FEATURES
        if (lowLatencyAimMode == LowLatencyAimMode::InstantAimFollow)
            lowLatencyAimMode = LowLatencyAimMode::ImmediateSync;
#endif
        m_lowLatencyAimMode = static_cast<int8_t>(lowLatencyAimMode);
        if (!m_disableMphAimSmoothing)
            m_lowLatencyAimMode = LowLatencyAimMode::Off;
        m_moonLikeAimNormalStepQ12 = std::clamp(
            localCfg.GetInt(CfgKey::MoonLikeAimNormalStepQ12), 1, 8192);
        m_moonLikeAimFastStepQ12 = std::clamp(
            localCfg.GetInt(CfgKey::MoonLikeAimFastStepQ12), 1, 8192);
        m_moonLikeAimFastThresholdQ12 = std::clamp(
            localCfg.GetInt(CfgKey::MoonLikeAimFastThresholdQ12), 1, 8192);
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
        m_enableImmediateInputEdgeOverlay = localCfg.GetBool(CfgKey::ImmediateInputEdgeOverlay);
#else
        m_enableImmediateInputEdgeOverlay = false;
#endif
        m_enableDirectAltFormTransform    = localCfg.GetBool(CfgKey::DirectAltFormTransform);
        if (!m_enableDirectAltFormTransform)
            m_directTransformPendingFrames = 0;
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
        m_enableNativeBipedFire =
            localCfg.GetInt(CfgKey::BipedFireMethod) != BipedFireMethod::LegacyInput;
#else
        m_enableNativeBipedFire = false;
#endif
        if (!m_enableNativeBipedFire) {
            m_nativeBipedFirePending = false;
            m_nativeBipedFireDirectActive = false;
        }
        const int zoomInputMethod = localCfg.GetInt(CfgKey::ZoomInputMethod);
        m_enableNewZoomInputMethod =
            zoomInputMethod == ZoomInputMethod::NewPresetBinding;
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
        m_enableNativeZoomToggle =
            zoomInputMethod == ZoomInputMethod::NewNativeToggle;
#else
        m_enableNativeZoomToggle = false;
#endif
        const int zoomAimScalePct = std::clamp(localCfg.GetInt(CfgKey::ZoomAimScalePct), 10, 300);
        m_zoomAimScaleQ14 = static_cast<uint32_t>(
            (static_cast<int64_t>(zoomAimScalePct) * AIM_ONE_FP + 50) / 100);
        m_enableZoomAimScale =
            localCfg.GetBool(CfgKey::ZoomAimScaleEnable)
            && m_zoomAimScaleQ14 != static_cast<uint32_t>(AIM_ONE_FP);
        if (!m_enableZoomAimScale) {
            if (m_activeZoomAimScaleQ14 != static_cast<uint32_t>(AIM_ONE_FP)) {
                m_activeZoomAimScaleQ14 = static_cast<uint32_t>(AIM_ONE_FP);
                RecalcAimEffectiveFixedScale();
                m_aimResidualX = 0;
                m_aimResidualY = 0;
            }
        } else if (m_activeZoomAimScaleQ14 != static_cast<uint32_t>(AIM_ONE_FP)
                   && m_activeZoomAimScaleQ14 != m_zoomAimScaleQ14) {
            m_activeZoomAimScaleQ14 = m_zoomAimScaleQ14;
            RecalcAimEffectiveFixedScale();
            m_aimResidualX = 0;
            m_aimResidualY = 0;
        }

        if (!m_enableNativeZoomToggle) {
            m_nativeZoomTogglePrevDown = false;
#ifdef MELONPRIME_DS
            m_nativeZoomPending.Clear();
#endif
        }

#ifdef MELONPRIME_DS
        m_enableNativeWeaponSwitch =
            localCfg.GetInt(CfgKey::WeaponSwitchMethod) != WeaponSwitchMethod::LegacyTouch;
        if (!m_enableNativeWeaponSwitch)
            m_weaponSwitchPending.Clear();
#endif

        screenSyncMode = NormalizeScreenSyncMode(localCfg.GetInt(CfgKey::ScreenSyncMode));

        ReloadDamageNotifyPurpleConfig();
    }

    void MelonPrimeCore::Initialize()
    {
        ReloadConfigFlags();
        RecalcAimSensitivityCache(localCfg);
        ApplyAimAdjustSetting(localCfg);

#ifdef _WIN32
        SetupRawInput();
#elif defined(__APPLE__) || defined(__linux__)
        if (PlatformInput_ShouldAcquireRawFilter() && !m_platformRawFilter)
            m_platformRawFilter = PlatformInput_AcquireRawFilter();
#endif
    }

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

        if (auto* mw = emuInstance->getMainWindow()) {
            m_cachedHwnd = reinterpret_cast<HWND>(mw->winId());
        }
        m_rawFilter.reset(
            RawInputWinFilter::Acquire(m_flags.test(StateFlags::BIT_JOY2KEY), m_cachedHwnd));

        ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));
        BindMetroidHotkeysFromConfig(m_rawFilter.get(), emuInstance->getInstanceID());
#endif
    }

    // =========================================================================
    // REFACTORED: ApplyJoy2KeySupportAndQtFilter
    //
    // Changed: `static bool s_isInstalled` -> member `m_isNativeFilterInstalled`.
    //
    // The static local was shared across all MelonPrimeCore instances.
    // While currently only one instance exists, this was a latent bug:
    //   - Multiple instances would corrupt each other's filter install state
    //   - Not resilient to future multi-instance support
    //   - Static locals in member functions are a code smell
    //
    // Member variable is initialised to false in the header, reset in OnEmuStart.
    // =========================================================================
    void MelonPrimeCore::ApplyJoy2KeySupportAndQtFilter(bool enable, bool doReset)
    {
#ifdef _WIN32
        if (!m_rawFilter) return;
        QCoreApplication* app = QCoreApplication::instance();
        if (!app) return;

        if (auto* mw = emuInstance->getMainWindow()) {
            m_cachedHwnd = reinterpret_cast<HWND>(mw->winId());
        }

        m_rawFilter->setRawInputTarget(static_cast<HWND>(m_cachedHwnd));
        m_flags.assign(StateFlags::BIT_JOY2KEY, enable);

        m_rawFilter->setJoy2KeySupport(enable);

        if (enable != m_isNativeFilterInstalled) {
            if (enable) {
                app->installNativeEventFilter(m_rawFilter.get());
            }
            else {
                app->removeNativeEventFilter(m_rawFilter.get());
            }
            m_isNativeFilterInstalled = enable;
        }

        if (doReset) {
            // P-9 / resetAll: combined reset keeps the same semantics with one fence.
            m_rawFilter->resetAll();
            m_rawFilter->resetHotkeyEdges();
        }
#endif
    }

    void MelonPrimeCore::RecalcAimSensitivityCache(Config::Table& cfg) {
        const float sens = static_cast<float>(cfg.GetInt(CfgKey::AimSens));
        const float yScale = static_cast<float>(cfg.GetDouble(CfgKey::AimYScale));
        m_aimSensiFactor = sens * 0.01f;
        m_aimCombinedY = m_aimSensiFactor * yScale;
        RecalcAimFixedPoint();
    }

    void MelonPrimeCore::ApplyAimAdjustSetting(Config::Table& cfg) {
        const double v = cfg.GetDouble(CfgKey::AimAdjust);
        m_aimAdjust = static_cast<float>(std::max(0.0, std::isnan(v) ? 0.0 : v));
        RecalcAimFixedPoint();
    }

    void MelonPrimeCore::RecalcAimFixedPoint()
    {
        m_aimFixedScaleX = static_cast<int32_t>(m_aimSensiFactor * AIM_ONE_FP + 0.5f);
        m_aimFixedScaleY = static_cast<int32_t>(m_aimCombinedY * AIM_ONE_FP + 0.5f);
        RecalcAimEffectiveFixedScale();

        if (m_aimAdjust > 0.0f) {
            m_aimFixedAdjust = static_cast<int64_t>(m_aimAdjust * AIM_ONE_FP + 0.5f);
            m_aimFixedSnapThresh = AIM_ONE_FP;
        }
        else {
            m_aimFixedAdjust = 0;
            m_aimFixedSnapThresh = 0;
        }

        // P-17: Reset sub-pixel accumulators when scale changes.
        // Old residuals were computed with previous scale factors.
        m_aimResidualX = 0;
        m_aimResidualY = 0;
        m_nativeAimDeltaX = 0;
        m_nativeAimDeltaY = 0;
    }

    // =========================================================================
    // Emulator lifecycle resets
    // =========================================================================

    void MelonPrimeCore::OnEmuStart()
    {
        m_flags.packed = 0;
        m_isLayoutChangePending = true;
        m_isWeaponCheckActive = false;
#ifdef _WIN32
        m_isNativeFilterInstalled = false;
#endif

#ifdef MELONPRIME_CUSTOM_HUD
        CustomHud_ResetPatchState();
#endif
#ifdef MELONPRIME_DS
        m_weaponSwitchPending.Clear();
        ARM9Hook_Uninstall(emuInstance->getNDS(), emuInstance);
        {
            const PatchCtx ctx{ emuInstance->getNDS(), emuInstance, localCfg, m_currentRom };
            Patches_RestoreOnStop(ctx);
        }
        Patches_ResetAll();
        ARM9Hook_ResetPatchState();
#endif

        ReloadConfigFlags();
        ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));
        InputReset();
        // Intentional historical asymmetry: this startup path leaves
        // TR_BipedFire untouched. Other lifecycle sites reset it at game/boot
        // boundaries; changing this would alter input reset timing and needs a
        // dedicated S7/S8 behavior pass.
        // weaponSwitchPending is cleared above (before ARM9Hook_Uninstall) where
        // ordering matters, so it is not part of this cluster call.
        ResetTransientInputState(
            TR_AimResiduals | TR_OverlayHeld | TR_DirectTransform);

        // P-3: Cache panel pointer (avoids 3-level pointer chase every frame)
        if (auto* mw = emuInstance->getMainWindow())
            m_cachedPanel = mw->panel;
    }

    void MelonPrimeCore::ResetRuntimeStateForBoot()
    {
        m_flags.packed = 0;
        m_isLayoutChangePending = true;
        m_isWeaponCheckActive = false;
        m_aimBlockBits = 0;
#ifdef MELONPRIME_CUSTOM_HUD
        CustomHud_ResetPatchState();
#endif
#ifdef MELONPRIME_DS
        m_weaponSwitchPending.Clear();
        ARM9Hook_Uninstall(emuInstance->getNDS(), emuInstance);
        // boot reset: state only, no RAM restore (emu memory is being re-initialized)
        Patches_ResetAll();
        ARM9Hook_ResetPatchState();
#endif

        InputReset();
        // weaponSwitchPending cleared above (before ARM9Hook_Uninstall) where
        // ordering matters, so it is not part of this cluster call.
        ResetTransientInputState(
            TR_AimResiduals | TR_OverlayHeld | TR_DirectTransform | TR_BipedFire);
    }

    void MelonPrimeCore::OnEmuStop()
    {
        m_flags.clear(StateFlags::BIT_IN_GAME);
        // Intentional historical asymmetry: stop clears transform/fire
        // transients but leaves aim residuals and overlay-held state alone.
        // OnEmuStart/boot perform broader resets; changing this stop-time
        // subset would need a dedicated S7/S8 behavior pass.
        // weaponSwitchPending is cleared in the DS block below (before
        // ARM9Hook_Uninstall).
        ResetTransientInputState(TR_DirectTransform | TR_BipedFire);
#ifdef MELONPRIME_CUSTOM_HUD
        if (m_flags.test(StateFlags::BIT_ROM_DETECTED)) {
            CustomHud_EnsurePatchRestored(
                emuInstance, localCfg, m_currentRom, m_playerPosition, false);
        }
        CustomHud_ResetPatchState();
#endif
#ifdef MELONPRIME_DS
        m_weaponSwitchPending.Clear();
        ARM9Hook_Uninstall(emuInstance->getNDS(), emuInstance);
        {
            const PatchCtx ctx{ emuInstance->getNDS(), emuInstance, localCfg, m_currentRom };
            Patches_RestoreOnStop(ctx);
        }
        Patches_ResetAll();
        ARM9Hook_ResetPatchState();
#endif
    }

    // P-3: Moved from header -- requires complete EmuInstance type
    void MelonPrimeCore::NotifyLayoutChange()
    {
        m_isLayoutChangePending = true;
        if (auto* mw = emuInstance->getMainWindow())
            m_cachedPanel = mw->panel;
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
            m_rawFilter->DeferredDrain();
        }
#endif
    }

    void MelonPrimeCore::OnEmuPause() {}

    void MelonPrimeCore::NotifyConfigChanged()
    {
        m_configReloadPending.store(true, std::memory_order_release);
    }

    COLD_FUNCTION void MelonPrimeCore::ApplyConfigReload()
    {
        const bool oldJoy2Key = m_flags.test(StateFlags::BIT_JOY2KEY);
        ReloadConfigFlags();
        const bool newJoy2Key = m_flags.test(StateFlags::BIT_JOY2KEY);
        ApplyJoy2KeySupportAndQtFilter(newJoy2Key, oldJoy2Key != newJoy2Key);

        RecalcAimSensitivityCache(localCfg);
        ApplyAimAdjustSetting(localCfg);

#ifdef _WIN32
        if (m_rawFilter) {
            BindMetroidHotkeysFromConfig(m_rawFilter.get(), emuInstance->getInstanceID());
            m_rawFilter->resetHotkeyEdges();
        }
#endif

#ifdef MELONPRIME_DS
        if (m_flags.test(StateFlags::BIT_ROM_DETECTED)
                && m_flags.test(StateFlags::BIT_BATTLE_RUNTIME_MODE)) {
            melonDS::NDS* const nds = emuInstance->getNDS();
            ARM9Hook_SetMatchHooksActive(
                nds,
                localCfg,
                m_currentRom.romGroupIndex,
                this,
                true,
                emuInstance);
            const PatchCtx ctx{ nds, emuInstance, localCfg, m_currentRom };
            Patches_Apply(PatchSite_ConfigReload, ctx);
        }
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

        RecalcAimSensitivityCache(localCfg);
        ApplyAimAdjustSetting(localCfg);

#ifdef _WIN32
        if (m_rawFilter) {
            // Reload VK bindings from config, then re-sync edge state.
            BindMetroidHotkeysFromConfig(m_rawFilter.get(), emuInstance->getInstanceID());
            m_rawFilter->resetHotkeyEdges();
        }
#endif

        if (m_flags.test(StateFlags::BIT_IN_GAME)) {
            m_flags.clear(StateFlags::BIT_IN_GAME_INIT);
            m_flags.clear(StateFlags::BIT_END_OF_GAME_PATCH_RESTORED);
            m_flags.clear(StateFlags::BIT_BATTLE_RUNTIME_MODE);
        }
    }

    void MelonPrimeCore::OnReset() { OnEmuStart(); }

    bool MelonPrimeCore::ShouldForceSoftwareRenderer() const
    {
        return m_flags.test(StateFlags::BIT_ROM_DETECTED) && !m_flags.test(StateFlags::BIT_IN_GAME);
    }

    // =========================================================================
    // Per-frame hook and global hotkeys
    // =========================================================================

    // 99%+ frames hit the early return (2 bit tests + branch).
    FORCE_INLINE void MelonPrimeCore::HandleGlobalHotkeys()
    {
        constexpr uint64_t kSensiUpBit   = 1ULL << HK_MetroidIngameSensiUp;
        constexpr uint64_t kSensiDownBit = 1ULL << HK_MetroidIngameSensiDown;
        const uint64_t released = emuInstance->hotkeyRelease &
                                  (kSensiUpBit | kSensiDownBit);
        if (LIKELY(!released)) return;

        const int change = (released & kSensiUpBit) ? 1 : -1;
        const int cur = localCfg.GetInt(CfgKey::AimSens);
        const int next = cur + change;

        if (next < 1) {
            emuInstance->osdAddMessage(0, "AimSensi cannot be decreased below 1");
        }
        else if (next != cur) {
            localCfg.SetInt(CfgKey::AimSens, next);
            Config::Save();
            RecalcAimSensitivityCache(localCfg);
            emuInstance->osdAddMessage(0, "AimSensi Updated: %d->%d", cur, next);
        }
    }

    HOT_FUNCTION void MelonPrimeCore::RunFrameHook()
    {
        // mainRAM removed - HandleGameJoinInit self-fetches (cold path only)

        if (UNLIKELY(m_isRunningHook)) {
            // Re-entrant path (called during FrameAdvanceOnce within weapon switch, morph, etc.)
            // Use the lean updater: no press-map scan, no wheel fetch.
            const bool focused = isFocused.load(std::memory_order_acquire);
            UpdateInputStateReentrant(focused);
            ProcessMoveAndButtonsFastFromReset();
            ApplyBipedFireInput();
            ApplyZoomBindingInput();

            const bool isStylusMode = this->isStylusMode;
            if (isStylusMode) {
                if (emuInstance->isTouching && !m_flags.test(StateFlags::BIT_BLOCK_STYLUS)) {
                    emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
                }
            }
            else {
                ProcessAimInputMouse();
            }
            return;
        }

        if (UNLIKELY(m_configReloadPending.exchange(false, std::memory_order_acq_rel))) {
            ApplyConfigReload();
        }

        m_isRunningHook = true;

        // P-43: Cache isFocused in local variable.
        // After UpdateInputState / HandleGlobalHotkeys / HandleInGameLogic
        // (member function calls), the compiler must assume any member could
        // have changed, forcing a reload from memory. isFocused is written by
        // the GUI thread, so load once and use that value consistently for this
        // frame's input snapshot and focus transition.
        const bool focused = isFocused.load(std::memory_order_acquire);

        // Poll moved into UpdateInputState via PollAndSnapshot
        UpdateInputState(focused);
        InputReset();
        m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);

        HandleGlobalHotkeys();

        if (UNLIKELY(!m_flags.test(StateFlags::BIT_ROM_DETECTED))) {
            DetectRomAndSetAddresses();
        }

        if (LIKELY(m_flags.test(StateFlags::BIT_ROM_DETECTED))) {
            const bool isInGame = (*m_ptrs.inGame) == 0x0001;
            const bool wasInGame = m_flags.test(StateFlags::BIT_IN_GAME);
            m_flags.assign(StateFlags::BIT_IN_GAME, isInGame);

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
            //   post-latch live match: battleFlowState only
            const uint32_t matchLifecycleFlags = m_flags.packed;
            if (UNLIKELY((matchLifecycleFlags & StateFlags::BIT_IN_GAME_INIT)
                    && !(matchLifecycleFlags & StateFlags::BIT_END_OF_GAME_PATCH_RESTORED))) {
                if (!(matchLifecycleFlags & StateFlags::BIT_BATTLE_RUNTIME_MODE)) {
                    const uint8_t mode = *m_ptrs.currentMode;
                    if (mode == BattleFlow::MODE_BATTLE_RUNTIME) {
                        const uint8_t flowState = *m_ptrs.battleFlowState;
                        if (flowState == BattleFlow::FLOW_ACTIVE_MATCH)
                            HandleBattleRuntimeEnter();
                    }
                } else {
                    const uint8_t flowState = *m_ptrs.battleFlowState;
                    if (flowState != BattleFlow::FLOW_ACTIVE_MATCH) {
                        melonDS::NDS* const nds = emuInstance->getNDS();
                        const PatchCtx ctx{ nds, emuInstance, localCfg, m_currentRom };
                        Patches_RestoreOnLeave(ctx);
                        ARM9Hook_SetMatchHooksActive(
                            nds, localCfg, m_currentRom.romGroupIndex, this, false, emuInstance);
                        m_flags.set(StateFlags::BIT_END_OF_GAME_PATCH_RESTORED);
                    }
                }
            }
#endif

            if (LIKELY(isInGame)) {
                if (m_flags.test(StateFlags::BIT_BATTLE_RUNTIME_MODE)) {
                    // Per-frame re-evaluation — bypasses registry; gated to skip lobby RAM work.
                    OsdColor_ApplyOnce(emuInstance, localCfg, m_currentRom);
                }
#ifdef MELONPRIME_CUSTOM_HUD
                // Before RunFrame: hold the helmet layers off across the spawn
                // window. The game's own clamp (patched helmet site) early-outs
                // during spawn states, so init writers can briefly restore the
                // BG1-3 layers and flash the native visor for a frame.
                CustomHud_ClampHelmetLayersPreFrame(
                    emuInstance, m_currentRom, m_playerPosition);
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
#ifdef MELONPRIME_DS
                ARM9Hook_SetMatchHooksActive(
                    emuInstance->getNDS(),
                    localCfg,
                    m_currentRom.romGroupIndex,
                    this,
                    false,
                    emuInstance);
#endif
                // weaponSwitchPending cleared in the DS block below where ordering matters.
                ResetTransientInputState(
                    TR_OverlayHeld | TR_DirectTransform | TR_BipedFire);
#ifdef MELONPRIME_CUSTOM_HUD
                CustomHud_EnsurePatchRestored(
                    emuInstance, localCfg, m_currentRom, m_playerPosition, false);
#endif
#ifdef MELONPRIME_DS
                m_weaponSwitchPending.Clear();
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
                    {
                        // Per-frame menu site (cold path): a tight masked loop
                        // over the registry; matching entries self-guard.
                        const PatchCtx ctx{ emuInstance->getNDS(), emuInstance, localCfg, m_currentRom };
                        Patches_Apply(PatchSite_OutOfGameFrame, ctx);
                    }
#endif
                    // Out-of-game screens (e.g. the Adventure planet/region map)
                    // still accept WASD movement so the player can navigate.
                    // Movement only — fire/jump/aim stay released and cursor mode
                    // keeps driving the touch screen for menu selection.
                    //
                    // Order matters: ProcessMovementOnlyFromReset() does a full
                    // m_inputMaskFast assignment (releases every non-D-pad button),
                    // so it must run BEFORE ApplyGameSettingsOnce(), which applies
                    // the UI Left/Right buttons (License L/R, Adventure left/right)
                    // on top via single-bit InputSetBranchless. Running it after
                    // wiped those bits and broke the Hunter License L/R navigation.
                    ProcessMovementOnlyFromReset();
                    ApplyGameSettingsOnce();
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
                InputSetBranchless(INPUT_START, !IsDown(IB_MENU));
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
                    ResetTransientInputState(TR_DirectTransform | TR_BipedFire);
#ifdef _WIN32
                    // P-9: Single call replaces resetAllKeys + resetMouseButtons
                    // (one fence instead of two)
                    if (m_rawFilter) {
                        m_rawFilter->resetAll();
                    }
#elif defined(__APPLE__) || defined(__linux__)
                    // Drop raw deltas accumulated while unfocused so refocus
                    // cannot produce an aim jump (same intent as FIX-3).
                    PlatformInput_ResetRawFilter(m_platformRawFilter);
#endif
#ifdef MELONPRIME_DS
                    m_weaponSwitchPending.Clear();
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
#endif
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
        m_flags.set(StateFlags::BIT_IN_GAME_INIT);
        ResetTransientInputState(
            TR_OverlayHeld | TR_DirectTransform | TR_BipedFire | TR_WeaponSwitchPending);
        m_playerPosition = Read8(mainRAM, m_currentRom.playerPos);

        const uint32_t offP = static_cast<uint32_t>(m_playerPosition) * Consts::PLAYER_ADDR_INC;
        const uint32_t playerBase = m_currentRom.playerStructStart + offP;

        auto readBinding16 = [mainRAM](uint32_t address) -> uint16_t {
            if (address < 0x02000000u || address > 0x023FFFFEu)
                return 0;
            return Read16(mainRAM, address);
        };
        m_bindingMoveL = readBinding16(playerBase + 0x368u);
        m_bindingMoveR = readBinding16(playerBase + 0x36Cu);
        m_bindingMoveF = readBinding16(playerBase + 0x370u);
        m_bindingMoveB = readBinding16(playerBase + 0x374u);
        m_bindingFire  = readBinding16(playerBase + 0x398u);
        m_bindingJump  = readBinding16(playerBase + 0x39Cu);
        m_bindingZoom  = readBinding16(playerBase + 0x3E0u);

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
            nds, localCfg, m_currentRom.sensitivity, m_addrHot.inGameSensi, true);

        MelonPrimeGameSettings::ApplyAimSmoothingPatch(nds, m_currentRom, m_disableMphAimSmoothing);

#ifdef MELONPRIME_DS
        // Game-join patches only (aspect ratio). Battle-runtime patches/hooks wait for mode 0x0E.
        Patches_Apply(PatchSite_GameJoin, PatchCtx{ nds, emuInstance, localCfg, m_currentRom });
#endif
#ifdef MELONPRIME_CUSTOM_HUD
        // Cache battle settings for HUD display
        CustomHud_OnMatchJoin(mainRAM, m_currentRom);
#endif
    }

    COLD_FUNCTION void MelonPrimeCore::HandleBattleRuntimeEnter()
    {
        m_flags.set(StateFlags::BIT_BATTLE_RUNTIME_MODE);
#ifdef MELONPRIME_DS
        melonDS::NDS* const nds = emuInstance->getNDS();
        const PatchCtx ctx{ nds, emuInstance, localCfg, m_currentRom };
        Patches_Apply(PatchSite_BattleRuntime, ctx);
        ARM9Hook_SetMatchHooksActive(
            nds, localCfg, m_currentRom.romGroupIndex, this, true, emuInstance);
        if (m_enableNativeWeaponSwitch)
            (void)WeaponSwitchHook_IsSiteValid(nds, m_currentRom.romGroupIndex);
#endif
    }

    void MelonPrimeCore::ShowCursor(bool show)
    {
        auto* panel = emuInstance->getMainWindow()->panel;
        if (!panel) return;

#if !defined(_WIN32)
        QPoint center;
        bool hasCenter = false;

        if (!show) {
            center = GetAdjustedCenter();
            m_aimData.centerX = center.x();
            m_aimData.centerY = center.y();
            hasCenter = true;

#if defined(__APPLE__) || defined(__linux__)
            PlatformInput_ResetRawFilter(m_platformRawFilter);
#endif
        }
#endif

        QMetaObject::invokeMethod(panel,
#if !defined(_WIN32)
            [panel, show, center, hasCenter]() {
#else
            [panel, show]() {
#endif
                panel->setCursor(show ? Qt::ArrowCursor : Qt::BlankCursor);
                if (show) panel->unclip();
                else {
                    panel->clipCursorCenter1px();
#if !defined(_WIN32)
                    if (hasCenter) {
#if defined(__linux__)
                        // Drop the fallback's prev-position baseline first so
                        // this warp is never counted as motion.
                        panel->resetAimMouseDelta();
#endif
                        PlatformInput_WarpCursor(center.x(), center.y());
                    }
#endif
                }
            },
            Qt::ConnectionType::QueuedConnection);
    }

    void MelonPrimeCore::FrameAdvanceCustom() { m_frameAdvanceFunc(); }

    void MelonPrimeCore::FrameAdvanceDefault()
    {
        emuInstance->inputProcess();
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
        auto* panel = emuInstance->getMainWindow()->panel;
        if (!panel) return QPoint(0, 0);
        const QRect r = panel->geometry();
        return panel->mapToGlobal(QPoint(r.width() / 2, r.height() / 2));
    }

} // namespace MelonPrime
