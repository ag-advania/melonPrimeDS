/*
    Copyright 2016-2025 melonDS team
    (MelonPrime specific configuration extension)
*/

#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSlider>
#include <QTimer>
#include <QVariant>

#include "MelonPrimeInputConfig.h"
#include "ui_MelonPrimeInputConfig.h"
#include "Config.h"
#include "MelonPrimeDef.h"
#include "../MelonPrimeHudPropSchema.inc"
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudConfigState.h"
#endif
#ifdef MELONPRIME_DS
#include "MelonPrime.h"
#include "MelonPrimePatch.h"
#endif

using namespace melonDS;

namespace {
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    constexpr bool kDeveloperOnlyFeaturesEnabled = true;
#else
    constexpr bool kDeveloperOnlyFeaturesEnabled = false;
#endif
}

void MelonPrimeInputConfig::saveConfig()
{
    Config::Table& instcfg = emuInstance->getLocalConfig();

#ifdef MELONPRIME_DS
    // GUI settings are an explicit user commit and therefore supersede any
    // older PageUp/PageDown persistence request or pending debounce timer.
    if (auto* thread = emuInstance->getEmuThread()) {
        if (auto* core = thread->GetMelonPrimeCore())
            core->ThreadBridge().DiscardPersistRequestsFromGui();
    }
    if (auto* primary = emuInstance->getMainWindow()) {
        if (primary->panel)
            primary->panel->cancelMelonPrimeDeferredConfigSave();
    }
#endif

    const bool oldClipCursorToBottomScreenWhenNotInGame =
        instcfg.GetBool(MP_HUD_PROP_KEY_ClipCursorToBottomScreenWhenNotInGame);
    const bool oldInGameTopScreenOnly =
        instcfg.GetBool(MP_HUD_PROP_KEY_InGameTopScreenOnly);
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

    if (m_comboMenuLanguage)
        instcfg.SetInt(MelonPrime::CfgKey::MenuLanguage, m_comboMenuLanguage->currentData().toInt());

    for (int i = 0; i < kMetroidHotkeyCount; ++i)
    {
        const char* btn = EmuInstance::hotkeyNames[kMetroidHotkeys[i].id];
        keycfg.SetInt(btn, addonsMetroidKeyMap[i]);
        joycfg.SetInt(btn, addonsMetroidJoyMap[i]);
    }

    for (int i = 0; i < kMetroidHotkey2Count; ++i)
    {
        const char* btn = EmuInstance::hotkeyNames[kMetroidHotkeys2[i].id];
        keycfg.SetInt(btn, addonsMetroid2KeyMap[i]);
        joycfg.SetInt(btn, addonsMetroid2JoyMap[i]);
    }

    // Phase 5b: all symmetric simple non-HUD settings (sensitivities, toggles,
    // bug fixes, game features, pickup toggles, hunter license, volume, screen
    // sync, in-game scaling, low-HP thresholds) are saved here from the same
    // binding table used to load them. Keys are disjoint so order is irrelevant.
    saveBindings(instcfg);
    for (int i = 0; i < emuInstance->getNumWindows(); ++i) {
        MainWindow* win = emuInstance->getWindow(i);
        if (win && win->panel)
            win->panel->refreshTopScreenTouchSetting();
    }

    int lowLatencyAimMode = m_comboMetroidLowLatencyAimMode
        ? m_comboMetroidLowLatencyAimMode->currentData().toInt()
        : MelonPrime::LowLatencyAimMode::Off;
    instcfg.SetInt(MelonPrime::CfgKey::LowLatencyAimMode, lowLatencyAimMode);
    int nativeAimHookMode = 0;
    if constexpr (kDeveloperOnlyFeaturesEnabled) {
        if (ui->cbMetroidEnableNativeAimRegisterInjection->checkState() == Qt::Checked)
            nativeAimHookMode = 1;
        else if (ui->cbMetroidEnableNativeAimPostFoldWrite->checkState() == Qt::Checked)
            nativeAimHookMode = 2;
    }
    instcfg.SetInt(MelonPrime::CfgKey::NativeAimHookMode, nativeAimHookMode);
    instcfg.SetBool(
        MelonPrime::CfgKey::ImmediateInputEdgeOverlay,
        kDeveloperOnlyFeaturesEnabled
            && ui->cbMetroidEnableImmediateInputEdgeOverlay->checkState() == Qt::Checked);
    instcfg.SetBool(
        MelonPrime::CfgKey::DirectAltFormTransform,
        m_cbMetroidUseNewTransformMethod
            ? m_cbMetroidUseNewTransformMethod->isChecked()
            : (ui->cbMetroidEnableDirectAltFormTransform->checkState() == Qt::Checked));
    if (m_cbMetroidUseNewWeaponSwitchMethod) {
        instcfg.SetInt(
            MelonPrime::CfgKey::WeaponSwitchMethod,
            m_cbMetroidUseNewWeaponSwitchMethod->isChecked()
                ? MelonPrime::WeaponSwitchMethod::NewNative
                : MelonPrime::WeaponSwitchMethod::LegacyTouch);
    }
    if (m_cbMetroidUseNewBipedFireMethod) {
        instcfg.SetInt(
            MelonPrime::CfgKey::BipedFireMethod,
            kDeveloperOnlyFeaturesEnabled && m_cbMetroidUseNewBipedFireMethod->isChecked()
                ? MelonPrime::BipedFireMethod::NewNativeEdge
                : MelonPrime::BipedFireMethod::LegacyInput);
    }
    if (m_cbMetroidFpsCameraLock) {
        instcfg.SetBool(
            MelonPrime::CfgKey::FpsCameraLock,
            m_cbMetroidFpsCameraLock->isChecked());
    }
    if (m_cbMetroidUseNewZoomMethod2) {
        // Writing this back also normalizes the retired NewPresetBinding value
        // out of older configs: it now behaves exactly like LegacyFixedR.
        instcfg.SetInt(
            MelonPrime::CfgKey::ZoomInputMethod,
            kDeveloperOnlyFeaturesEnabled && m_cbMetroidUseNewZoomMethod2->isChecked()
                ? MelonPrime::ZoomInputMethod::NewNativeToggle
                : MelonPrime::ZoomInputMethod::LegacyFixedR);
    }
    // Legacy key migration. Keep until the first post-V3 release gives old
    // configs a save cycle; see the Phase 4 migration ledger.
    // Do not add new reads.
    // Keep the old key mirrored for configs from before the independent
    // FpsCameraLock key was introduced.
    instcfg.SetBool(
        MelonPrime::CfgKey::InstantAimFollow,
        m_cbMetroidFpsCameraLock
            && m_cbMetroidFpsCameraLock->isChecked());

    // Screen Sync Mode, In-game scaling, and Low HP warning thresholds are all
    // saved via saveBindings() above (binding table). Clip/TopScreen stay below
    // because their save is coupled to an old!=new invalidate.
    const bool clipCursorToBottomScreenWhenNotInGame =
        (ui->cbMetroidClipCursorToBottomScreenWhenNotInGame->checkState() == Qt::Checked);
    instcfg.SetBool(MP_HUD_PROP_KEY_ClipCursorToBottomScreenWhenNotInGame, clipCursorToBottomScreenWhenNotInGame);
    const bool inGameTopScreenOnly =
        (ui->cbMetroidInGameTopScreenOnly->checkState() == Qt::Checked);
    instcfg.SetBool(MP_HUD_PROP_KEY_InGameTopScreenOnly, inGameTopScreenOnly);
    if (oldClipCursorToBottomScreenWhenNotInGame != clipCursorToBottomScreenWhenNotInGame) {
        for (int i = 0; i < emuInstance->getNumWindows(); ++i) {
            MainWindow* win = emuInstance->getWindow(i);
            if (win && win->panel) {
                QTimer::singleShot(0, win->panel, [panel = win->panel]() {
                    panel->updateClipIfNeeded();
                });
            }
        }
    }
    if (oldInGameTopScreenOnly != inGameTopScreenOnly) {
        for (int i = 0; i < emuInstance->getNumWindows(); ++i) {
            MainWindow* win = emuInstance->getWindow(i);
            if (win && win->panel) {
                QMetaObject::invokeMethod(win->panel, "onScreenLayoutChanged", Qt::QueuedConnection);
            }
        }
    }

    // Custom HUD
    instcfg.SetBool(MP_HUD_PROP_KEY_CustomHUD, ui->cbMetroidEnableCustomHud->checkState() == Qt::Checked);
    instcfg.SetBool(MP_HUD_PROP_KEY_HudCrosshairHighRes,
        ui->cbMetroidHudCrosshairHighRes->checkState() == Qt::Checked);
    instcfg.SetBool(MP_HUD_PROP_KEY_HudCrosshairDeadbandEnable,
        ui->cbMetroidHudCrosshairDeadbandEnable->checkState() == Qt::Checked);
    instcfg.SetDouble(MP_HUD_PROP_KEY_HudCrosshairDeadband,
        ui->dsbMetroidHudCrosshairDeadband->value());
    instcfg.SetInt(
        MelonPrime::CfgKey::OnScreenEditStyle,
        static_cast<int>(MelonPrime::NormalizeOnScreenEditStyle(
            ui->comboMetroidOnScreenEditStyle->currentData().toInt())));

    // Save all programmatic HUD widgets
    for (auto& [key, widget] : m_hudWidgets) {
        if (!widget)
            continue;
        if (auto* cb = qobject_cast<QCheckBox*>(widget.data()))
            instcfg.SetBool(key, cb->isChecked());
        else if (auto* slider = qobject_cast<QSlider*>(widget.data()))
            instcfg.SetDouble(key, slider->value() / 100.0);
        else if (auto* sb = qobject_cast<QSpinBox*>(widget.data()))
            instcfg.SetInt(key, sb->value());
        else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(widget.data()))
            instcfg.SetDouble(key, dsb->value());
        else if (auto* le = qobject_cast<QLineEdit*>(widget.data()))
            instcfg.SetString(key, le->text().toStdString());
        else if (auto* fc = qobject_cast<QFontComboBox*>(widget.data()))   // before QComboBox: stores family string
            instcfg.SetString(key, fc->currentFont().family().toStdString());
        else if (auto* combo = qobject_cast<QComboBox*>(widget.data()))
            instcfg.SetInt(key, combo->currentIndex());
    }

    // Section toggle states (existing UI sections)
    instcfg.SetBool(MelonPrime::CfgKey::SectionInputSettings,  ui->btnToggleInputSettings->isChecked());
    if (m_btnToggleInputMethod)
        instcfg.SetBool(MelonPrime::CfgKey::SectionInputMethod, m_btnToggleInputMethod->isChecked());
    instcfg.SetBool(MelonPrime::CfgKey::SectionScreenSync,     ui->btnToggleScreenSync->isChecked());
    instcfg.SetBool(MelonPrime::CfgKey::SectionCursorClipSettings,  ui->btnToggleCursorClipSettings->isChecked());
    instcfg.SetBool(MelonPrime::CfgKey::SectionInGameApply,  ui->btnToggleInGameApply->isChecked());
    instcfg.SetBool(MelonPrime::CfgKey::SectionInGameAspectRatio,  ui->btnToggleInGameAspectRatio->isChecked());
    instcfg.SetBool(MelonPrime::CfgKey::SectionSensitivity,    ui->btnToggleSensitivity->isChecked());
    instcfg.SetBool(MelonPrime::CfgKey::SectionBugFix,         ui->btnToggleBugFix->isChecked());
    instcfg.SetBool(MelonPrime::CfgKey::SectionGameFeature,    ui->btnToggleGameFeature->isChecked());
    instcfg.SetBool(MelonPrime::CfgKey::SectionDisableFeatures, ui->btnToggleDisableFeatures->isChecked());
    instcfg.SetBool(
        MelonPrime::CfgKey::SectionPowerUpPickupEffects,
        ui->btnToggleDisablePickingUpSpecificItems->isChecked());
    instcfg.SetBool(MelonPrime::CfgKey::SectionGameplay,       ui->btnToggleGameplay->isChecked());
    instcfg.SetBool(MelonPrime::CfgKey::SectionVideo,          ui->btnToggleVideo->isChecked());
    instcfg.SetBool(MelonPrime::CfgKey::SectionVolume,         ui->btnToggleVolume->isChecked());
    instcfg.SetBool(MelonPrime::CfgKey::SectionLicense,        ui->btnToggleLicense->isChecked());
    // Restore note: remove this entry if the DEVELOPER ONLY section is removed.
    instcfg.SetBool(MelonPrime::CfgKey::SectionDeveloperOnly,  ui->btnToggleDeveloperOnly->isChecked());

    // HUD section toggle states (programmatic sections)
    for (auto& [btn, cfgKey] : m_hudToggles)
        instcfg.SetBool(cfgKey, btn->isChecked());

    // P-3: Invalidate cached config so next frame re-reads all values
#ifdef MELONPRIME_DS
    if (auto* thread = emuInstance->getEmuThread()) {
        if (auto* core = thread->GetMelonPrimeCore())
        {
            MelonPrime::CustomHud_InvalidateConfigCache(core->HudConfigState());
            core->NotifyConfigChanged();
        }
    }
#endif
}
