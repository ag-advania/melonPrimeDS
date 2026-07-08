#include "MelonPrimeInternal.h"
#include "MelonPrimeGameSettings.h"
#include "MelonPrimeRuntimeConfig.h"
#include "EmuInstance.h"
#include "Screen.h"
#include "MelonPrimeDef.h"
#include "MelonPrimePlatformInput.h"
#include "MelonPrimeArm9Hook.h"

#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudRender.h"
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

#if MELONPRIME_PLATFORM_RAW_FILTER_ENABLED
    MelonPrimeCore::~MelonPrimeCore()
    {
        PlatformInput_ReleaseRawFilter(m_platformRawFilter);
    }
#else
    MelonPrimeCore::~MelonPrimeCore() = default;
#endif

    void MelonPrimeCore::ApplyRuntimeConfigSnapshot(const RuntimeConfigSnapshot& s)
    {
        m_flags.assign(StateFlags::BIT_JOY2KEY, s.joy2Key);
        m_flags.assign(StateFlags::BIT_SNAP_TAP, s.snapTap);
        m_flags.assign(StateFlags::BIT_STYLUS_MODE, s.stylusMode);
        isStylusMode    = m_flags.test(StateFlags::BIT_STYLUS_MODE);
        m_snapTapMode   = m_flags.test(StateFlags::BIT_SNAP_TAP);

        m_disableMphAimSmoothing = s.disableMphAimSmoothing;
        m_enableAimAccumulator = s.aimAccumulator;
        m_nativeAimHookMode = s.nativeAimHookMode;
        m_enableNativeAimDeltaHook = s.enableNativeAimDeltaHook;
        m_lowLatencyAimMode = s.lowLatencyAimMode;
        m_moonLikeAimNormalStepQ12 = s.moonLikeAimNormalStepQ12;
        m_moonLikeAimFastStepQ12 = s.moonLikeAimFastStepQ12;
        m_moonLikeAimFastThresholdQ12 = s.moonLikeAimFastThresholdQ12;
        m_enableImmediateInputEdgeOverlay = s.immediateInputEdgeOverlay;
        m_enableDirectAltFormTransform = s.directAltFormTransform;
        if (!m_enableDirectAltFormTransform)
            m_directTransformPendingFrames = 0;
        m_enableNativeBipedFire = s.nativeBipedFire;
        if (!m_enableNativeBipedFire) {
            m_nativeBipedFirePending = false;
            m_nativeBipedFireDirectActive = false;
        }
        m_enableNewZoomInputMethod = s.newZoomInputMethod;
        m_enableNativeZoomToggle = s.nativeZoomToggle;
        m_zoomAimScaleQ14 = s.zoomAimScaleQ14;
        m_enableZoomAimScale = s.zoomAimScaleEnable;
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
        m_enableNativeWeaponSwitch = s.nativeWeaponSwitch;
        if (!m_enableNativeWeaponSwitch)
            m_weaponSwitchPending.Clear();
#endif

        screenSyncMode = s.screenSyncMode;
    }

    void MelonPrimeCore::ReloadConfigFlags()
    {
        const RuntimeConfigSnapshot snapshot =
            LoadRuntimeConfigSnapshot(localCfg);

        ApplyRuntimeConfigSnapshot(snapshot);
        ReloadDamageNotifyPurpleConfig();
    }

    void MelonPrimeCore::Initialize()
    {
        ReloadConfigFlags();
        RecalcAimSensitivityCache(localCfg);
        ApplyAimAdjustSetting(localCfg);

#ifdef _WIN32
        SetupRawInput();
#elif MELONPRIME_PLATFORM_RAW_FILTER_ENABLED
        if (PlatformInput_ShouldAcquireRawFilter() && !m_platformRawFilter)
            m_platformRawFilter = PlatformInput_AcquireRawFilter();
#endif
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

    void MelonPrimeCore::OnEmuPause() {}

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

} // namespace MelonPrime
