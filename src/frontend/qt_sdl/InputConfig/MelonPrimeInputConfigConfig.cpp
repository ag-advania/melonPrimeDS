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
#include <QTimer>
#include <QVariant>

#include "MelonPrimeInputConfig.h"
#include "ui_MelonPrimeInputConfig.h"
#include "Config.h"
#include "MelonPrimeDef.h"
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudRender.h"
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
    const bool oldClipCursorToBottomScreenWhenNotInGame =
        instcfg.GetBool("Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame");
    const bool oldInGameTopScreenOnly =
        instcfg.GetBool("Metroid.Visual.InGameTopScreenOnly");
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

    if (m_comboMenuLanguage)
        instcfg.SetInt("Metroid.UI.MenuLanguage", m_comboMenuLanguage->currentData().toInt());

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

    const int lowLatencyAimMode = m_comboMetroidLowLatencyAimMode
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
    instcfg.SetInt("Metroid.Aim.NativeHookMode", nativeAimHookMode);
    instcfg.SetBool(
        "Metroid.Input.Enable.ImmediateInputEdgeOverlay",
        kDeveloperOnlyFeaturesEnabled
            && ui->cbMetroidEnableImmediateInputEdgeOverlay->checkState() == Qt::Checked);
    instcfg.SetBool(
        "Metroid.Input.Enable.DirectAltFormTransform",
        m_cbMetroidUseNewTransformMethod
            ? m_cbMetroidUseNewTransformMethod->isChecked()
            : (ui->cbMetroidEnableDirectAltFormTransform->checkState() == Qt::Checked));
    if (m_cbMetroidUseNewWeaponSwitchMethod) {
        instcfg.SetInt(
            "Metroid.Input.WeaponSwitchMethod",
            m_cbMetroidUseNewWeaponSwitchMethod->isChecked()
                ? MelonPrime::WeaponSwitchMethod::NewNative
                : MelonPrime::WeaponSwitchMethod::LegacyTouch);
    }
    if (m_cbMetroidUseNewBipedFireMethod) {
        instcfg.SetInt(
            "Metroid.Input.BipedFireMethod",
            kDeveloperOnlyFeaturesEnabled && m_cbMetroidUseNewBipedFireMethod->isChecked()
                ? MelonPrime::BipedFireMethod::NewNativeEdge
                : MelonPrime::BipedFireMethod::LegacyInput);
    }
    if (m_cbMetroidUseNewZoomMethod || m_cbMetroidUseNewZoomMethod2) {
        int zoomMethod = MelonPrime::ZoomInputMethod::LegacyFixedR;
        if (kDeveloperOnlyFeaturesEnabled
            && m_cbMetroidUseNewZoomMethod2
            && m_cbMetroidUseNewZoomMethod2->isChecked())
            zoomMethod = MelonPrime::ZoomInputMethod::NewNativeToggle;
        else if (m_cbMetroidUseNewZoomMethod && m_cbMetroidUseNewZoomMethod->isChecked())
            zoomMethod = MelonPrime::ZoomInputMethod::NewPresetBinding;
        instcfg.SetInt(
            "Metroid.Input.ZoomMethod",
            zoomMethod);
    }
    // Legacy key migration — planned removal after the next release.
    // Do not add new reads.
    // Mirror the migrated mode back into the legacy InstantAimFollow bool so a
    // downgrade to an older build keeps working until the key is dropped.
    instcfg.SetBool(
        MelonPrime::CfgKey::InstantAimFollow,
        lowLatencyAimMode == MelonPrime::LowLatencyAimMode::InstantAimFollow);

    // Screen Sync Mode, In-game scaling, and Low HP warning thresholds are all
    // saved via saveBindings() above (binding table). Clip/TopScreen stay below
    // because their save is coupled to an old!=new invalidate.
    const bool clipCursorToBottomScreenWhenNotInGame =
        (ui->cbMetroidClipCursorToBottomScreenWhenNotInGame->checkState() == Qt::Checked);
    instcfg.SetBool("Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame", clipCursorToBottomScreenWhenNotInGame);
    const bool inGameTopScreenOnly =
        (ui->cbMetroidInGameTopScreenOnly->checkState() == Qt::Checked);
    instcfg.SetBool("Metroid.Visual.InGameTopScreenOnly", inGameTopScreenOnly);
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
    instcfg.SetBool("Metroid.Visual.CustomHUD", ui->cbMetroidEnableCustomHud->checkState() == Qt::Checked);

    // Save all programmatic HUD widgets
    for (auto& [key, widget] : m_hudWidgets) {
        if (auto* cb = qobject_cast<QCheckBox*>(widget))
            instcfg.SetBool(key, cb->isChecked());
        else if (auto* sb = qobject_cast<QSpinBox*>(widget))
            instcfg.SetInt(key, sb->value());
        else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(widget))
            instcfg.SetDouble(key, dsb->value());
        else if (auto* le = qobject_cast<QLineEdit*>(widget))
            instcfg.SetString(key, le->text().toStdString());
        else if (auto* fc = qobject_cast<QFontComboBox*>(widget))   // before QComboBox: stores family string
            instcfg.SetString(key, fc->currentFont().family().toStdString());
        else if (auto* combo = qobject_cast<QComboBox*>(widget))
            instcfg.SetInt(key, combo->currentIndex());
    }

    // Section toggle states (existing UI sections)
    instcfg.SetBool("Metroid.UI.SectionInputSettings",  ui->btnToggleInputSettings->isChecked());
    if (m_btnToggleInputMethod)
        instcfg.SetBool("Metroid.UI.SectionInputMethod", m_btnToggleInputMethod->isChecked());
    instcfg.SetBool("Metroid.UI.SectionScreenSync",     ui->btnToggleScreenSync->isChecked());
    instcfg.SetBool("Metroid.UI.SectionCursorClipSettings",  ui->btnToggleCursorClipSettings->isChecked());
    instcfg.SetBool("Metroid.UI.SectionInGameApply",  ui->btnToggleInGameApply->isChecked());
    instcfg.SetBool("Metroid.UI.SectionInGameAspectRatio",  ui->btnToggleInGameAspectRatio->isChecked());
    instcfg.SetBool("Metroid.UI.SectionSensitivity",    ui->btnToggleSensitivity->isChecked());
    instcfg.SetBool("Metroid.UI.SectionBugFix",         ui->btnToggleBugFix->isChecked());
    instcfg.SetBool("Metroid.UI.SectionGameFeature",    ui->btnToggleGameFeature->isChecked());
    instcfg.SetBool("Metroid.UI.SectionDisableFeatures", ui->btnToggleDisableFeatures->isChecked());
    instcfg.SetBool(
        "Metroid.UI.SectionPowerUpPickupEffects",
        ui->btnToggleDisablePickingUpSpecificItems->isChecked());
    instcfg.SetBool("Metroid.UI.SectionGameplay",       ui->btnToggleGameplay->isChecked());
    instcfg.SetBool("Metroid.UI.SectionVideo",          ui->btnToggleVideo->isChecked());
    instcfg.SetBool("Metroid.UI.SectionVolume",         ui->btnToggleVolume->isChecked());
    instcfg.SetBool("Metroid.UI.SectionLicense",        ui->btnToggleLicense->isChecked());
    // Restore note: remove this entry if the DEVELOPER ONLY section is removed.
    instcfg.SetBool("Metroid.UI.SectionDeveloperOnly",  ui->btnToggleDeveloperOnly->isChecked());

    // HUD section toggle states (programmatic sections)
    for (auto& [btn, cfgKey] : m_hudToggles)
        instcfg.SetBool(cfgKey, btn->isChecked());

    // P-3: Invalidate cached config so next frame re-reads all values
    MelonPrime::CustomHud_InvalidateConfigCache();
#ifdef MELONPRIME_DS
    MelonPrime::OsdColor_InvalidatePatch();
    MelonPrime::ExpandStageMatrix_InvalidatePatch();
    MelonPrime::ShadowFreezeRuntimeHook_NotifyConfigChanged();
    MelonPrime::FixNoxusBladePersistence_NotifyConfigChanged();
    if (auto* thread = emuInstance->getEmuThread()) {
        if (auto* core = thread->GetMelonPrimeCore())
            core->NotifyConfigChanged();
    }
#endif
}

