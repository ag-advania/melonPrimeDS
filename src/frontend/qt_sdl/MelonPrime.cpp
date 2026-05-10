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
#include "MelonPrimeGameRomAddrTable.h"

#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudRender.h"
#endif
#if defined(MELONPRIME_CUSTOM_HUD) || defined(MELONPRIME_DS)
#include "MelonPrimePatch.h"
#endif

#include <cmath>
#include <algorithm>
#include <QCoreApplication>
#include <QCursor>

#ifdef _WIN32
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeRawInputState.h"
#include "MelonPrimeRawHotkeyVkBinding.h"

namespace MelonPrime {
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
    }

    MelonPrimeCore::~MelonPrimeCore() = default;

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
        m_lowLatencyAimMode = static_cast<int8_t>(
            std::clamp(localCfg.GetInt(CfgKey::LowLatencyAimMode),
                       LowLatencyAimMode::Off,
                       LowLatencyAimMode::MoonLikeAim));
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

        screenSyncMode = localCfg.GetInt(CfgKey::ScreenSyncMode);

        // Damage Notify Purple — disabled by default; cached so the per-frame
        // tick reads only one bool, never the config table.
        // Parent gate: requires Disable Double Damage Multiplier ON so the
        // 10-frame timer write can never become a real 2x boost. Even when the
        // user/UI tries to enable Notify alone, force it OFF here as a safety
        // net (handles hand-edited config files / mismatched upgrades).
        const bool dnpRequested = localCfg.GetBool("Metroid.GameFeature.DamageNotifyPurple");
        const bool ddMultDisabled = localCfg.GetBool("Metroid.GameFeature.DisableDoubleDamageMultiplier");
        const bool dnp = dnpRequested && ddMultDisabled;
        if (m_damageNotifyPurpleEnabled && !dnp) {
            // Toggled off → drop accumulated baseline so a re-enable starts fresh.
            m_damageNotifyPurpleState = {};
        }
        m_damageNotifyPurpleEnabled = dnp;
    }

    void MelonPrimeCore::Initialize()
    {
        ReloadConfigFlags();
        RecalcAimSensitivityCache(localCfg);
        ApplyAimAdjustSetting(localCfg);

#ifdef _WIN32
        SetupRawInput();
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
        ARM9Hook_Uninstall(emuInstance->getNDS());
        InstantAimFollow_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
        ShowHeadshotOnline_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
        ShowEnemyHpMeterOnline_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
        DisableDoubleDamageMultiplier_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
        NoPickingUpSpecificItems_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
        InGameAspectRatio_ResetPatchState();
        OsdColor_ResetPatchState();
        FixWifi_ResetPatchState();
        UseFirmwareLanguage_ResetPatchState();
        ShowHeadshotOnline_ResetPatchState();
        ShowEnemyHpMeterOnline_ResetPatchState();
        DisableDoubleDamageMultiplier_ResetPatchState();
        NoPickingUpSpecificItems_ResetPatchState();
        InstantAimFollow_ResetPatchState();
        ARM9Hook_ResetPatchState();
#endif

        ReloadConfigFlags();
        ApplyJoy2KeySupportAndQtFilter(m_flags.test(StateFlags::BIT_JOY2KEY));
        InputReset();
        m_aimResidualX = 0;
        m_aimResidualY = 0;
        m_nativeAimDeltaX = 0;
        m_nativeAimDeltaY = 0;
        m_immediateOverlayPrevHeld = 0;
        m_directTransformPendingFrames = 0;

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
        ARM9Hook_Uninstall(emuInstance->getNDS());
        InGameAspectRatio_ResetPatchState();
        OsdColor_ResetPatchState();
        FixWifi_ResetPatchState();
        UseFirmwareLanguage_ResetPatchState();
        ShowHeadshotOnline_ResetPatchState();
        ShowEnemyHpMeterOnline_ResetPatchState();
        DisableDoubleDamageMultiplier_ResetPatchState();
        NoPickingUpSpecificItems_ResetPatchState();
        InstantAimFollow_ResetPatchState();
        ARM9Hook_ResetPatchState();
#endif

        InputReset();
        m_aimResidualX = 0;
        m_aimResidualY = 0;
        m_nativeAimDeltaX = 0;
        m_nativeAimDeltaY = 0;
        m_immediateOverlayPrevHeld = 0;
        m_directTransformPendingFrames = 0;
        m_nativeBipedFirePending = false;
        m_nativeBipedFireDirectActive = false;
    }

    void MelonPrimeCore::OnEmuStop()
    {
        m_flags.clear(StateFlags::BIT_IN_GAME);
        m_directTransformPendingFrames = 0;
        m_nativeBipedFirePending = false;
        m_nativeBipedFireDirectActive = false;
#ifdef MELONPRIME_CUSTOM_HUD
        if (m_flags.test(StateFlags::BIT_ROM_DETECTED)) {
            CustomHud_EnsurePatchRestored(
                emuInstance, localCfg, m_currentRom, m_playerPosition, false);
        }
        CustomHud_ResetPatchState();
#endif
#ifdef MELONPRIME_DS
        m_weaponSwitchPending.Clear();
        ARM9Hook_Uninstall(emuInstance->getNDS());
        InstantAimFollow_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
        ShowHeadshotOnline_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
        ShowEnemyHpMeterOnline_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
        DisableDoubleDamageMultiplier_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
        NoPickingUpSpecificItems_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
        InGameAspectRatio_ResetPatchState();
        OsdColor_ResetPatchState();
        FixWifi_ResetPatchState();
        UseFirmwareLanguage_ResetPatchState();
        ShowHeadshotOnline_ResetPatchState();
        ShowEnemyHpMeterOnline_ResetPatchState();
        DisableDoubleDamageMultiplier_ResetPatchState();
        NoPickingUpSpecificItems_ResetPatchState();
        InstantAimFollow_ResetPatchState();
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

    // P-33: PrePollRawInput implementation removed.
    // P-19 (HiddenWndProc processRawInput) captures all WM_INPUT at dispatch.
    // The function is now an empty inline in MelonPrime.h for source compat.

    // =========================================================================
    // P-22: DeferredDrainInput — drain WM_INPUT queue after RunFrame.
    //
    // With P-19, each dispatched WM_INPUT triggers processRawInput in
    // HiddenWndProc, so draining also captures any straggler events.
    // Runs every frame to keep the queue clean.
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
        if (m_flags.test(StateFlags::BIT_ROM_DETECTED)) {
            melonDS::NDS* const nds = emuInstance->getNDS();
            ARM9Hook_Install(
                nds,
                localCfg,
                m_currentRom.romGroupIndex,
                this);
            InstantAimFollow_ApplyOnce(nds, localCfg, m_currentRom.romGroupIndex);
            ShowHeadshotOnline_ApplyOnce(nds, localCfg, m_currentRom.romGroupIndex);
            ShowEnemyHpMeterOnline_ApplyOnce(nds, localCfg, m_currentRom.romGroupIndex);
            DisableDoubleDamageMultiplier_ApplyOnce(nds, localCfg, m_currentRom.romGroupIndex);
            NoPickingUpSpecificItems_ApplyOnce(nds, localCfg, m_currentRom.romGroupIndex);
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
        }
    }

    void MelonPrimeCore::OnReset() { OnEmuStart(); }

    bool MelonPrimeCore::ShouldForceSoftwareRenderer() const
    {
        return m_flags.test(StateFlags::BIT_ROM_DETECTED) && !m_flags.test(StateFlags::BIT_IN_GAME);
    }

    // =========================================================================
    // Damage Notify Purple — Weavel-aware effective HP watcher v4
    //
    // Spec: 29-Damage-Notify-Purple-NonHook-AI-Implementation-Instructions-v4
    //
    //   When the local player's effective HP drops, write 10 to the local
    //   Double Damage timer (CPlayer +0x4B0). This produces a short purple
    //   flash so opponents can see "this player just took damage". Pair with
    //   Disable Double Damage Multiplier so the flash never becomes a real 2x
    //   boost.
    //
    //   Observed HP metric:
    //     non-Weavel             : observed = mainHp
    //     Weavel proxy inactive  : observed = mainHp
    //     Weavel proxy active    : observed = mainHp + proxyHp (no 199 clamp;
    //                                          mainHp=199 + proxyHp=100 = 299
    //                                          is a legitimate state)
    //
    //   Edge handling (v4 fix):
    //     proxyActive false → true : metric switch (mainHp → mainHp+proxyHp).
    //                                Baseline-only frame, do not compare.
    //     proxyActive true  → false: metric switch (mainHp+proxyHp → mainHp).
    //                                Baseline-only frame; this is what
    //                                prevents the 299 → 199 false-notify on
    //                                Weavel un-transform.
    //
    //   skipCompare cases (also baseline-only):
    //     - proxy pointer not in ARM9 RAM range (still settling)
    //     - proxyHpRaw > 100 (junk read; using mainHp here would create a
    //       fake total drop on the next valid frame)
    //
    //   Threshold = 5: real damage in MPH is ≥10 HP; this filters single-frame
    //   anomalies without missing real hits.
    //
    //   Always overwrites the existing DD timer — do NOT read+protect.
    //   Caller already gates this on isInGame, so ROM/pointer setup is valid.
    // =========================================================================
    HOT_FUNCTION void MelonPrimeCore::DamageNotifyPurpleTick()
    {
        if (!m_damageNotifyPurpleEnabled) {
            if (UNLIKELY(m_damageNotifyPurpleState.initialized))
                m_damageNotifyPurpleState = {};
            return;
        }

        if (UNLIKELY(!m_ptrs.health || !m_ptrs.doubleDamageTimer)) {
            m_damageNotifyPurpleState = {};
            return;
        }

        constexpr uint32_t PROXY_ACTIVE_BIT = 0x20u;        // moreFlags bit5
        constexpr uint32_t PROXY_HP_OFF     = 0xD0u;        // proxy entity +0xD0
        constexpr uint32_t ARM9_RAM_BASE    = 0x02000000u;
        constexpr uint32_t ARM9_RAM_END     = 0x023FFFFEu;  // last addr that fits a u16
        constexpr uint16_t PROXY_HP_MAX     = 100;
        constexpr uint16_t NOTIFY_DURATION  = 10;
        constexpr uint16_t DELTA_THRESHOLD  = 5;

        DamageNotifyPurpleState& s = m_damageNotifyPurpleState;
        const bool isWeavel = m_flags.test(StateFlags::BIT_IS_WEAVEL);

        const uint16_t mainHp = *m_ptrs.health;
        if (LIKELY(!isWeavel)) {
            if (!s.initialized) {
                s.previousObservedHp = mainHp;
                s.previousMainHp = mainHp;
                s.previousProxyHp = 0;
                s.previousProxyActive = false;
                s.initialized = true;
                return;
            }

            const uint16_t prev = s.previousObservedHp;
            s.previousObservedHp = mainHp;
            s.previousMainHp = mainHp;

            if (prev > mainHp) {
                const uint16_t delta = static_cast<uint16_t>(prev - mainHp);
                if (delta > DELTA_THRESHOLD) {
                    *m_ptrs.doubleDamageTimer = NOTIFY_DURATION;
                }
            }
            return;
        }

        uint16_t observedHp = mainHp;
        uint16_t proxyHp = 0;
        bool proxyActive = false;
        bool skipCompare = false;

        if (m_ptrs.moreFlags && m_ptrs.weavelProxyPtr) {
            proxyActive = (*m_ptrs.moreFlags & PROXY_ACTIVE_BIT) != 0;

            if (proxyActive) {
                const uint32_t proxyHpAddr = *m_ptrs.weavelProxyPtr + PROXY_HP_OFF;
                const bool proxyAddrValid =
                    proxyHpAddr >= ARM9_RAM_BASE && proxyHpAddr <= ARM9_RAM_END;

                if (!proxyAddrValid) {
                    skipCompare = true;
                }
                else {
                    melonDS::u8* const mainRAM = emuInstance->getNDS()->MainRAM;
                    if (UNLIKELY(mainRAM == nullptr)) {
                        skipCompare = true;
                    }
                    else {
                        const uint16_t proxyHpRaw = Read16(mainRAM, proxyHpAddr);
                        if (proxyHpRaw > PROXY_HP_MAX) {
                            // Junk read — observedHp stays as mainHp but skip
                            // compare so the next valid frame does not look
                            // like a sudden total drop.
                            skipCompare = true;
                        }
                        else {
                            proxyHp = proxyHpRaw;
                            const uint32_t total =
                                static_cast<uint32_t>(mainHp) +
                                static_cast<uint32_t>(proxyHp);
                            observedHp = (total > 0xFFFFu)
                                ? 0xFFFFu : static_cast<uint16_t>(total);
                        }
                    }
                }
            }
        }

        const bool wasProxyActive = s.previousProxyActive;
        const bool proxyJustAttached = !wasProxyActive && proxyActive;
        const bool proxyJustDetached = wasProxyActive && !proxyActive;

        // Always update baselines except on the compare path.
        auto updateBaselines = [&]() {
            s.previousObservedHp = observedHp;
            s.previousMainHp = mainHp;
            s.previousProxyHp = proxyHp;
            s.previousProxyActive = proxyActive;
        };

        if (!s.initialized) {
            s.initialized = true;
            updateBaselines();
            return;
        }

        if (skipCompare || proxyJustAttached || proxyJustDetached) {
            updateBaselines();
            return;
        }

        const uint16_t prev = s.previousObservedHp;
        updateBaselines();

        if (prev > observedHp) {
            const uint16_t delta = static_cast<uint16_t>(prev - observedHp);
            if (delta > DELTA_THRESHOLD) {
                *m_ptrs.doubleDamageTimer = NOTIFY_DURATION;
            }
        }
    }

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
            m_flags.assign(StateFlags::BIT_IN_GAME, isInGame);

            if (isInGame && !m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
                HandleGameJoinInit();
            }

            if (LIKELY(isInGame)) {
                OsdColor_ApplyOnce(emuInstance, localCfg, m_currentRom);
                // Damage Notify Purple — runs whether or not the window is focused
                // so HP drops during alt-tab still emit the purple flash.
                if (m_damageNotifyPurpleEnabled)
                    DamageNotifyPurpleTick();
            }
            else if (m_flags.test(StateFlags::BIT_IN_GAME_INIT)) {
                m_flags.clear(StateFlags::BIT_IN_GAME_INIT);
                m_immediateOverlayPrevHeld = 0;
                m_directTransformPendingFrames = 0;
                m_nativeBipedFirePending = false;
                m_nativeBipedFireDirectActive = false;
#ifdef MELONPRIME_CUSTOM_HUD
                CustomHud_EnsurePatchRestored(
                    emuInstance, localCfg, m_currentRom, m_playerPosition, false);
#endif
#ifdef MELONPRIME_DS
                m_weaponSwitchPending.Clear();
                InstantAimFollow_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
                ShowHeadshotOnline_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
                ShowEnemyHpMeterOnline_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
                DisableDoubleDamageMultiplier_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
                NoPickingUpSpecificItems_RestoreOnce(emuInstance->getNDS(), m_currentRom.romGroupIndex);
#endif
                OsdColor_RestoreOnce(emuInstance->getNDS(), m_currentRom);
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
                        melonDS::NDS* const nds = emuInstance->getNDS();
                        FixWifi_ApplyOnce(nds, localCfg, m_currentRom.romGroupIndex);
                        UseFirmwareLanguage_ApplyOnce(nds, localCfg, m_currentRom.romGroupIndex, m_currentRom.isInAdventure);
                    }
#endif
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
                    m_directTransformPendingFrames = 0;
                    m_nativeBipedFirePending = false;
                    m_nativeBipedFireDirectActive = false;
#ifdef _WIN32
                    // P-9: Single call replaces resetAllKeys + resetMouseButtons
                    // (one fence instead of two)
                    if (m_rawFilter) {
                        m_rawFilter->resetAll();
                    }
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
        // mainRAM fetched here (cold path) instead of every frame in RunFrameHook
        melonDS::u8* const mainRAM = emuInstance->getNDS()->MainRAM;
        m_flags.set(StateFlags::BIT_IN_GAME_INIT);
        m_immediateOverlayPrevHeld = 0;
        m_directTransformPendingFrames = 0;
        m_nativeBipedFirePending = false;
        m_nativeBipedFireDirectActive = false;
#ifdef MELONPRIME_DS
        m_weaponSwitchPending.Clear();
#endif
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
            emuInstance->getNDS(), localCfg, m_currentRom.sensitivity, m_addrHot.inGameSensi, true);

        MelonPrimeGameSettings::ApplyAimSmoothingPatch(
            emuInstance->getNDS(), m_currentRom, m_disableMphAimSmoothing);

#ifdef MELONPRIME_DS
        // Apply patches that need game-join context (player struct resolved)
        InGameAspectRatio_ApplyOnce(emuInstance, localCfg, m_currentRom);
        OsdColor_ApplyOnce(emuInstance, localCfg, m_currentRom);
        InstantAimFollow_ApplyOnce(
            emuInstance->getNDS(),
            localCfg,
            m_currentRom.romGroupIndex);
        ShowHeadshotOnline_ApplyOnce(
            emuInstance->getNDS(),
            localCfg,
            m_currentRom.romGroupIndex);
        ShowEnemyHpMeterOnline_ApplyOnce(
            emuInstance->getNDS(),
            localCfg,
            m_currentRom.romGroupIndex);
        DisableDoubleDamageMultiplier_ApplyOnce(
            emuInstance->getNDS(),
            localCfg,
            m_currentRom.romGroupIndex);
        NoPickingUpSpecificItems_ApplyOnce(
            emuInstance->getNDS(),
            localCfg,
            m_currentRom.romGroupIndex);
#endif
#ifdef MELONPRIME_CUSTOM_HUD
        // Cache battle settings for HUD display
        CustomHud_OnMatchJoin(mainRAM, m_currentRom);
#endif
    }

    void MelonPrimeCore::ShowCursor(bool show)
    {
        auto* panel = emuInstance->getMainWindow()->panel;
        if (!panel) return;
        QMetaObject::invokeMethod(panel, [panel, show]() {
            panel->setCursor(show ? Qt::ArrowCursor : Qt::BlankCursor);
            if (show) panel->unclip();
            else panel->clipCursorCenter1px();
            }, Qt::ConnectionType::QueuedConnection);
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
