/*
    Copyright 2016-2025 melonDS team
    (MelonPrime specific configuration extension)
*/

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>

#include "MelonPrimeInputConfig.h"
#include "ui_MelonPrimeInputConfig.h"
#include "Config.h"
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeCustomHud.h"
#endif

using namespace melonDS;

namespace {
constexpr int kHudPositionPresetCount = 8;
constexpr int kHudPositionCustomIndex = kHudPositionPresetCount;
} // namespace

void MelonPrimeInputConfig::saveConfig()
{
    Config::Table& instcfg = emuInstance->getLocalConfig();
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

    int i = 0;
    for (int hotkey : hk_tabAddonsMetroid)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        keycfg.SetInt(btn, addonsMetroidKeyMap[i]);
        joycfg.SetInt(btn, addonsMetroidJoyMap[i]);
        i++;
    }

    i = 0;
    for (int hotkey : hk_tabAddonsMetroid2)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        keycfg.SetInt(btn, addonsMetroid2KeyMap[i]);
        joycfg.SetInt(btn, addonsMetroid2JoyMap[i]);
        i++;
    }

    // Sensitivities
    instcfg.SetInt("Metroid.Sensitivity.Aim", ui->metroidAimSensitvitySpinBox->value());
    instcfg.SetDouble("Metroid.Sensitivity.Mph", ui->metroidMphSensitvitySpinBox->value());
    instcfg.SetDouble("Metroid.Sensitivity.AimYAxisScale", ui->metroidAimYAxisScaleSpinBox->value());
    instcfg.SetDouble("Metroid.Aim.Adjust", ui->metroidAimAdjustSpinBox->value());

    // SnapTap
    instcfg.SetBool("Metroid.Operation.SnapTap", ui->cbMetroidEnableSnapTap->checkState() == Qt::Checked);
    instcfg.SetBool("Metroid.Data.Unlock", ui->cbMetroidUnlockAll->checkState() == Qt::Checked);
    instcfg.SetBool("Metroid.Apply.Headphone", ui->cbMetroidApplyHeadphone->checkState() == Qt::Checked);
    instcfg.SetBool("Metroid.Use.Firmware.Name", ui->cbMetroidUseFirmwareName->checkState() == Qt::Checked);

    // Hunter license
    instcfg.SetBool("Metroid.HunterLicense.Hunter.Apply", ui->cbMetroidApplyHunter->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.HunterLicense.Hunter.Selected", ui->comboMetroidSelectedHunter->currentIndex());

    instcfg.SetBool("Metroid.HunterLicense.Color.Apply", ui->cbMetroidApplyColor->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.HunterLicense.Color.Selected", ui->comboMetroidSelectedColor->currentIndex());

    // Volume
    instcfg.SetBool("Metroid.Apply.SfxVolume", ui->cbMetroidApplySfxVolume->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Volume.SFX", ui->spinMetroidVolumeSFX->value());
    instcfg.SetBool("Metroid.Apply.MusicVolume", ui->cbMetroidApplyMusicVolume->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Volume.Music", ui->spinMetroidVolumeMusic->value());

    // Other Metroid Settings 2 Tab
    instcfg.SetBool("Metroid.Apply.joy2KeySupport", ui->cbMetroidApplyJoy2KeySupport->checkState() == Qt::Checked);
    instcfg.SetBool("Metroid.Enable.stylusMode", ui->cbMetroidEnableStylusMode->checkState() == Qt::Checked);
    instcfg.SetBool("Metroid.Aim.Disable.MphAimSmoothing", ui->cbMetroidDisableMphAimSmoothing->checkState() == Qt::Checked);
    instcfg.SetBool("Metroid.Aim.Enable.Accumulator", ui->cbMetroidEnableAimAccumulator->checkState() == Qt::Checked);

    // Screen Sync Mode
    instcfg.SetInt("Metroid.Screen.SyncMode", ui->comboMetroidScreenSyncMode->currentIndex());

    // In-game scaling
    instcfg.SetBool("Metroid.Visual.InGameAspectRatio", ui->cbMetroidInGameAspectRatio->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.InGameAspectRatioMode", ui->comboMetroidInGameAspectRatioMode->currentIndex());
    instcfg.SetBool("Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame", ui->cbMetroidClipCursorToBottomScreenWhenNotInGame->checkState() == Qt::Checked);

    // Battle HUD
    instcfg.SetBool("Metroid.Visual.HudMatchStatusShow", ui->cbMetroidHudMatchStatusShow->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudMatchStatusX", ui->spinMetroidHudMatchStatusX->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusY", ui->spinMetroidHudMatchStatusY->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusLabelOfsX", ui->spinMetroidHudMatchStatusLabelOfsX->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusLabelOfsY", ui->spinMetroidHudMatchStatusLabelOfsY->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusLabelPos", ui->comboMetroidHudMatchStatusLabelPos->currentIndex());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelPoints", ui->leMetroidHudMatchStatusLabelPoints->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelOctoliths", ui->leMetroidHudMatchStatusLabelOctoliths->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelLives", ui->leMetroidHudMatchStatusLabelLives->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelRingTime", ui->leMetroidHudMatchStatusLabelRingTime->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelPrimeTime", ui->leMetroidHudMatchStatusLabelPrimeTime->text().toStdString());
    // Color values are already in config (set by color button handlers)

    // Rank & Time HUD
    instcfg.SetBool("Metroid.Visual.HudRankShow",        ui->cbMetroidHudRankShow->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudRankX",            ui->spinMetroidHudRankX->value());
    instcfg.SetInt("Metroid.Visual.HudRankY",            ui->spinMetroidHudRankY->value());
    instcfg.SetInt("Metroid.Visual.HudRankAlign",        ui->comboMetroidHudRankAlign->currentIndex());
    instcfg.SetString("Metroid.Visual.HudRankPrefix",    ui->leMetroidHudRankPrefix->text().toStdString());
    instcfg.SetBool("Metroid.Visual.HudRankShowOrdinal", ui->cbMetroidHudRankShowOrdinal->checkState() == Qt::Checked);
    instcfg.SetString("Metroid.Visual.HudRankSuffix",    ui->leMetroidHudRankSuffix->text().toStdString());
    instcfg.SetInt("Metroid.Visual.HudRankColorR",       ui->spinMetroidHudRankColorR->value());
    instcfg.SetInt("Metroid.Visual.HudRankColorG",       ui->spinMetroidHudRankColorG->value());
    instcfg.SetInt("Metroid.Visual.HudRankColorB",       ui->spinMetroidHudRankColorB->value());
    instcfg.SetBool("Metroid.Visual.HudTimeLeftShow",    ui->cbMetroidHudTimeLeftShow->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudTimeLeftX",        ui->spinMetroidHudTimeLeftX->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLeftY",        ui->spinMetroidHudTimeLeftY->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLeftAlign",    ui->comboMetroidHudTimeLeftAlign->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudTimeLeftColorR",   ui->spinMetroidHudTimeLeftColorR->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLeftColorG",   ui->spinMetroidHudTimeLeftColorG->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLeftColorB",   ui->spinMetroidHudTimeLeftColorB->value());
    instcfg.SetBool("Metroid.Visual.HudTimeLimitShow",   ui->cbMetroidHudTimeLimitShow->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudTimeLimitX",       ui->spinMetroidHudTimeLimitX->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLimitY",       ui->spinMetroidHudTimeLimitY->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLimitAlign",   ui->comboMetroidHudTimeLimitAlign->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudTimeLimitColorR",  ui->spinMetroidHudTimeLimitColorR->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLimitColorG",  ui->spinMetroidHudTimeLimitColorG->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLimitColorB",  ui->spinMetroidHudTimeLimitColorB->value());
    instcfg.SetBool("Metroid.Visual.HudBombLeftShow",    ui->cbMetroidHudBombLeftShow->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudBombLeftX",        ui->spinMetroidHudBombLeftX->value());
    instcfg.SetInt("Metroid.Visual.HudBombLeftY",        ui->spinMetroidHudBombLeftY->value());
    instcfg.SetInt("Metroid.Visual.HudBombLeftAlign",    ui->comboMetroidHudBombLeftAlign->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudBombLeftColorR",   ui->spinMetroidHudBombLeftColorR->value());
    instcfg.SetInt("Metroid.Visual.HudBombLeftColorG",   ui->spinMetroidHudBombLeftColorG->value());
    instcfg.SetInt("Metroid.Visual.HudBombLeftColorB",   ui->spinMetroidHudBombLeftColorB->value());
    instcfg.SetString("Metroid.Visual.HudBombLeftPrefix", ui->leMetroidHudBombLeftPrefix->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudBombLeftSuffix", ui->leMetroidHudBombLeftSuffix->text().toStdString());

    // Custom HUD
    instcfg.SetBool("Metroid.Visual.CustomHUD", ui->cbMetroidEnableCustomHud->checkState() == Qt::Checked);

    // Section toggle states
    instcfg.SetBool("Metroid.UI.SectionCrosshair",      ui->btnToggleCrosshair->isChecked());
    instcfg.SetBool("Metroid.UI.SectionInner",          ui->btnToggleInner->isChecked());
    instcfg.SetBool("Metroid.UI.SectionOuter",          ui->btnToggleOuter->isChecked());
    instcfg.SetBool("Metroid.UI.SectionHpAmmo",          ui->btnToggleHpAmmo->isChecked());
    instcfg.SetBool("Metroid.UI.SectionHpPos",          ui->btnToggleHpPos->isChecked());
    instcfg.SetBool("Metroid.UI.SectionWpnPos",         ui->btnToggleWpnPos->isChecked());
    instcfg.SetBool("Metroid.UI.SectionWpnIcon",        ui->btnToggleWpnIcon->isChecked());
    instcfg.SetBool("Metroid.UI.SectionHpGauge",        ui->btnToggleHpGauge->isChecked());
    instcfg.SetBool("Metroid.UI.SectionAmmoGauge",      ui->btnToggleAmmoGauge->isChecked());
    instcfg.SetBool("Metroid.UI.SectionMatchStatus",      ui->btnToggleMatchStatus->isChecked());
    instcfg.SetBool("Metroid.UI.SectionMatchStatusScore", ui->btnToggleMatchStatusScore->isChecked());
    instcfg.SetBool("Metroid.UI.SectionRankTime",         ui->btnToggleRankTime->isChecked());
    instcfg.SetBool("Metroid.UI.SectionRankHud",        ui->btnToggleRankHud->isChecked());
    instcfg.SetBool("Metroid.UI.SectionTimeLeftHud",    ui->btnToggleTimeLeftHud->isChecked());
    instcfg.SetBool("Metroid.UI.SectionTimeLimitHud",   ui->btnToggleTimeLimitHud->isChecked());
    instcfg.SetBool("Metroid.UI.SectionBombLeft",       ui->btnToggleBombLeft->isChecked());
    instcfg.SetBool("Metroid.UI.SectionHudRadar",       ui->btnToggleHudRadar->isChecked());
    instcfg.SetBool("Metroid.UI.SectionInputSettings",  ui->btnToggleInputSettings->isChecked());
    instcfg.SetBool("Metroid.UI.SectionScreenSync",     ui->btnToggleScreenSync->isChecked());
    instcfg.SetBool("Metroid.UI.SectionCursorClipSettings",  ui->btnToggleCursorClipSettings->isChecked());
    instcfg.SetBool("Metroid.UI.SectionInGameAspectRatio",  ui->btnToggleInGameAspectRatio->isChecked());
    instcfg.SetBool("Metroid.UI.SectionSensitivity",    ui->btnToggleSensitivity->isChecked());
    instcfg.SetBool("Metroid.UI.SectionGameplay",       ui->btnToggleGameplay->isChecked());
    instcfg.SetBool("Metroid.UI.SectionVideo",          ui->btnToggleVideo->isChecked());
    instcfg.SetBool("Metroid.UI.SectionVolume",         ui->btnToggleVolume->isChecked());
    instcfg.SetBool("Metroid.UI.SectionLicense",        ui->btnToggleLicense->isChecked());

    instcfg.SetInt("Metroid.Visual.HudMatchStatusColorR", ui->spinMetroidHudMatchStatusColorR->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusColorG", ui->spinMetroidHudMatchStatusColorG->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusColorB", ui->spinMetroidHudMatchStatusColorB->value());
    instcfg.SetBool("Metroid.Visual.HudMatchStatusLabelColorOverall", ui->comboMetroidHudMatchStatusLabelColor->currentIndex() == 0);
    instcfg.SetInt("Metroid.Visual.HudMatchStatusLabelColorR", ui->spinMetroidHudMatchStatusLabelColorR->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusLabelColorG", ui->spinMetroidHudMatchStatusLabelColorG->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusLabelColorB", ui->spinMetroidHudMatchStatusLabelColorB->value());
    instcfg.SetBool("Metroid.Visual.HudMatchStatusValueColorOverall", ui->comboMetroidHudMatchStatusValueColor->currentIndex() == 0);
    instcfg.SetInt("Metroid.Visual.HudMatchStatusValueColorR", ui->spinMetroidHudMatchStatusValueColorR->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusValueColorG", ui->spinMetroidHudMatchStatusValueColorG->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusValueColorB", ui->spinMetroidHudMatchStatusValueColorB->value());
    instcfg.SetBool("Metroid.Visual.HudMatchStatusSepColorOverall", ui->comboMetroidHudMatchStatusSepColor->currentIndex() == 0);
    instcfg.SetInt("Metroid.Visual.HudMatchStatusSepColorR", ui->spinMetroidHudMatchStatusSepColorR->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusSepColorG", ui->spinMetroidHudMatchStatusSepColorG->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusSepColorB", ui->spinMetroidHudMatchStatusSepColorB->value());
    instcfg.SetBool("Metroid.Visual.HudMatchStatusGoalColorOverall", ui->comboMetroidHudMatchStatusGoalColor->currentIndex() == 0);
    instcfg.SetInt("Metroid.Visual.HudMatchStatusGoalColorR", ui->spinMetroidHudMatchStatusGoalColorR->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusGoalColorG", ui->spinMetroidHudMatchStatusGoalColorG->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusGoalColorB", ui->spinMetroidHudMatchStatusGoalColorB->value());

    // HUD element positions
    instcfg.SetInt("Metroid.Visual.HudHpX", ui->spinMetroidHudHpX->value());
    instcfg.SetInt("Metroid.Visual.HudHpY", ui->spinMetroidHudHpY->value());
    instcfg.SetString("Metroid.Visual.HudHpPrefix", ui->leMetroidHudHpPrefix->text().toStdString());
    instcfg.SetInt("Metroid.Visual.HudHpAlign", ui->comboMetroidHudHpAlign->currentIndex());
    instcfg.SetBool("Metroid.Visual.HudHpTextAutoColor", ui->cbMetroidHudHpTextAutoColor->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudHpTextColorR", ui->spinMetroidHudHpTextColorR->value());
    instcfg.SetInt("Metroid.Visual.HudHpTextColorG", ui->spinMetroidHudHpTextColorG->value());
    instcfg.SetInt("Metroid.Visual.HudHpTextColorB", ui->spinMetroidHudHpTextColorB->value());
    instcfg.SetInt("Metroid.Visual.HudWeaponX", ui->spinMetroidHudWeaponX->value());
    instcfg.SetInt("Metroid.Visual.HudWeaponY", ui->spinMetroidHudWeaponY->value());
    instcfg.SetString("Metroid.Visual.HudAmmoPrefix", ui->leMetroidHudAmmoPrefix->text().toStdString());
    instcfg.SetInt("Metroid.Visual.HudAmmoAlign", ui->comboMetroidHudAmmoAlign->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudAmmoTextColorR", ui->spinMetroidHudAmmoTextColorR->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoTextColorG", ui->spinMetroidHudAmmoTextColorG->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoTextColorB", ui->spinMetroidHudAmmoTextColorB->value());
    instcfg.SetBool("Metroid.Visual.HudWeaponIconShow", ui->cbMetroidHudWeaponIconShow->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudWeaponIconMode", ui->comboMetroidHudWeaponIconMode->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudWeaponIconOffsetX", ui->spinMetroidHudWeaponIconOffsetX->value());
    instcfg.SetInt("Metroid.Visual.HudWeaponIconOffsetY", ui->spinMetroidHudWeaponIconOffsetY->value());
    instcfg.SetInt("Metroid.Visual.HudWeaponIconPosX",    ui->spinMetroidHudWeaponIconPosX->value());
    instcfg.SetInt("Metroid.Visual.HudWeaponIconPosY",    ui->spinMetroidHudWeaponIconPosY->value());
    instcfg.SetInt("Metroid.Visual.HudWeaponIconAnchorX", ui->comboMetroidHudWeaponIconAnchorX->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudWeaponIconAnchorY", ui->comboMetroidHudWeaponIconAnchorY->currentIndex());
    instcfg.SetBool("Metroid.Visual.HudWeaponIconColorOverlay", ui->cbMetroidHudWeaponIconColorOverlay->checkState() == Qt::Checked);

    // Gauge settings - HP
    instcfg.SetBool("Metroid.Visual.HudHpGauge", ui->cbMetroidHudHpGauge->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudHpGaugeOrientation", ui->comboMetroidHudHpGaugeOrientation->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeLength", ui->spinMetroidHudHpGaugeLength->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeWidth", ui->spinMetroidHudHpGaugeWidth->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeOffsetX", ui->spinMetroidHudHpGaugeOffsetX->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeOffsetY", ui->spinMetroidHudHpGaugeOffsetY->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeAnchor", ui->comboMetroidHudHpGaugeAnchor->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudHpGaugePosMode", ui->comboMetroidHudHpGaugePosMode->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudHpGaugePosX", ui->spinMetroidHudHpGaugePosX->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugePosY", ui->spinMetroidHudHpGaugePosY->value());
    instcfg.SetBool("Metroid.Visual.HudHpGaugeAutoColor", ui->cbMetroidHudHpGaugeAutoColor->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudHpGaugeColorR", ui->spinMetroidHudHpGaugeColorR->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeColorG", ui->spinMetroidHudHpGaugeColorG->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeColorB", ui->spinMetroidHudHpGaugeColorB->value());

    // Gauge settings - Ammo
    instcfg.SetBool("Metroid.Visual.HudAmmoGauge", ui->cbMetroidHudAmmoGauge->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeOrientation", ui->comboMetroidHudAmmoGaugeOrientation->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeLength", ui->spinMetroidHudAmmoGaugeLength->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeWidth", ui->spinMetroidHudAmmoGaugeWidth->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeOffsetX", ui->spinMetroidHudAmmoGaugeOffsetX->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeOffsetY", ui->spinMetroidHudAmmoGaugeOffsetY->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeAnchor", ui->comboMetroidHudAmmoGaugeAnchor->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugePosMode", ui->comboMetroidHudAmmoGaugePosMode->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugePosX", ui->spinMetroidHudAmmoGaugePosX->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugePosY", ui->spinMetroidHudAmmoGaugePosY->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorR", ui->spinMetroidHudAmmoGaugeColorR->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorG", ui->spinMetroidHudAmmoGaugeColorG->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorB", ui->spinMetroidHudAmmoGaugeColorB->value());
    instcfg.SetInt("Metroid.Visual.CrosshairColorR", ui->spinMetroidCrosshairR->value());
    instcfg.SetInt("Metroid.Visual.CrosshairColorG", ui->spinMetroidCrosshairG->value());
    instcfg.SetInt("Metroid.Visual.CrosshairColorB", ui->spinMetroidCrosshairB->value());

    // HUD Radar

    // HUD Radar

    // Crosshair - General
    instcfg.SetBool("Metroid.Visual.CrosshairOutline", ui->cbMetroidCrosshairOutline->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairOutlineOpacity", ui->spinMetroidCrosshairOutlineOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOutlineThickness", ui->spinMetroidCrosshairOutlineThickness->value());
    instcfg.SetBool("Metroid.Visual.CrosshairCenterDot", ui->cbMetroidCrosshairCenterDot->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairDotOpacity", ui->spinMetroidCrosshairDotOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairDotThickness", ui->spinMetroidCrosshairDotThickness->value());
    instcfg.SetBool("Metroid.Visual.CrosshairTStyle", ui->cbMetroidCrosshairTStyle->checkState() == Qt::Checked);

    // HUD Radar

    // HUD Radar

    // Crosshair - Inner Lines
    instcfg.SetBool("Metroid.Visual.CrosshairInnerShow", ui->cbMetroidCrosshairInnerShow->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairInnerOpacity", ui->spinMetroidCrosshairInnerOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerLengthX", ui->spinMetroidCrosshairInnerLengthX->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerLengthY", ui->spinMetroidCrosshairInnerLengthY->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerThickness", ui->spinMetroidCrosshairInnerThickness->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerOffset", ui->spinMetroidCrosshairInnerOffset->value());
    instcfg.SetBool("Metroid.Visual.CrosshairInnerLinkXY", ui->cbMetroidCrosshairInnerLinkXY->checkState() == Qt::Checked);

    // HUD Radar

    // HUD Radar

    // Crosshair - Outer Lines
    instcfg.SetBool("Metroid.Visual.CrosshairOuterShow", ui->cbMetroidCrosshairOuterShow->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairOuterOpacity", ui->spinMetroidCrosshairOuterOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOuterLengthX", ui->spinMetroidCrosshairOuterLengthX->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOuterLengthY", ui->spinMetroidCrosshairOuterLengthY->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOuterThickness", ui->spinMetroidCrosshairOuterThickness->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOuterOffset", ui->spinMetroidCrosshairOuterOffset->value());
    instcfg.SetBool("Metroid.Visual.CrosshairOuterLinkXY", ui->cbMetroidCrosshairOuterLinkXY->checkState() == Qt::Checked);

    // Bottom Screen Overlay
    instcfg.SetBool  ("Metroid.Visual.BtmOverlayEnable",     ui->cbMetroidBtmOverlayEnable->checkState() == Qt::Checked);
    instcfg.SetInt   ("Metroid.Visual.BtmOverlayDstX",       ui->spinMetroidBtmOverlayDstX->value());
    instcfg.SetInt   ("Metroid.Visual.BtmOverlayDstY",       ui->spinMetroidBtmOverlayDstY->value());
    instcfg.SetInt   ("Metroid.Visual.BtmOverlayDstSize",    ui->spinMetroidBtmOverlayDstSize->value());
    instcfg.SetDouble("Metroid.Visual.BtmOverlayOpacity",    ui->spinMetroidBtmOverlayOpacity->value());
    instcfg.SetInt   ("Metroid.Visual.BtmOverlaySrcRadius",  ui->spinMetroidBtmOverlaySrcRadius->value());

    // P-3: Invalidate cached config so next frame re-reads all values
    MelonPrime::CustomHud_InvalidateConfigCache();
}

void MelonPrimeInputConfig::resetCrosshairDefaults()
{
    // Color: Red (#FF0000)
    Config::Table& instcfg = emuInstance->getLocalConfig();
    instcfg.SetInt("Metroid.Visual.CrosshairColorR", 255);
    instcfg.SetInt("Metroid.Visual.CrosshairColorG", 0);
    instcfg.SetInt("Metroid.Visual.CrosshairColorB", 0);
    ui->btnMetroidCrosshairColor->setStyleSheet("background-color: #FF0000;");

    // General
    ui->cbMetroidCrosshairOutline->setChecked(true);
    ui->spinMetroidCrosshairOutlineOpacity->setValue(0.50);
    ui->spinMetroidCrosshairOutlineThickness->setValue(1);
    ui->cbMetroidCrosshairCenterDot->setChecked(true);
    ui->spinMetroidCrosshairDotOpacity->setValue(1.00);
    ui->spinMetroidCrosshairDotThickness->setValue(1);
    ui->cbMetroidCrosshairTStyle->setChecked(true);

    // Inner
    ui->cbMetroidCrosshairInnerShow->setChecked(true);
    ui->spinMetroidCrosshairInnerOpacity->setValue(0.80);
    ui->spinMetroidCrosshairInnerLengthX->setValue(2);
    ui->spinMetroidCrosshairInnerLengthY->setValue(2);
    ui->cbMetroidCrosshairInnerLinkXY->setChecked(true);
    ui->spinMetroidCrosshairInnerThickness->setValue(1);
    ui->spinMetroidCrosshairInnerOffset->setValue(2);

    // Outer
    ui->cbMetroidCrosshairOuterShow->setChecked(true);
    ui->spinMetroidCrosshairOuterOpacity->setValue(0.40);
    ui->spinMetroidCrosshairOuterLengthX->setValue(1);
    ui->spinMetroidCrosshairOuterLengthY->setValue(1);
    ui->cbMetroidCrosshairOuterLinkXY->setChecked(true);
    ui->spinMetroidCrosshairOuterThickness->setValue(1);
    ui->spinMetroidCrosshairOuterOffset->setValue(4);

    // Font size
}

void MelonPrimeInputConfig::resetHpAmmoDefaults()
{
    auto setSlider = [this](QSlider* sl, QSpinBox* input, QLabel* lbl, int v) {
        sl->setValue(v);
        if (input)
            input->setValue(v);
        if (lbl) lbl->setText(QString::number(v));
    };

    // HP Position
    ui->comboMetroidHudHpPosition->setCurrentIndex(kHudPositionCustomIndex); // Custom
    setSlider(ui->spinMetroidHudHpX, ui->inputMetroidHudHpX, ui->labelMetroidHudHpX, 45);
    setSlider(ui->spinMetroidHudHpY, ui->inputMetroidHudHpY, ui->labelMetroidHudHpY, 99);
    ui->leMetroidHudHpPrefix->setText("");
    ui->comboMetroidHudHpAlign->setCurrentIndex(2); // Right
    ui->cbMetroidHudHpTextAutoColor->setChecked(true);
    ui->comboMetroidHudHpTextColor->setCurrentIndex(0);
    ui->spinMetroidHudHpTextColorR->setValue(255);
    ui->spinMetroidHudHpTextColorG->setValue(255);
    ui->spinMetroidHudHpTextColorB->setValue(255);
    ui->leMetroidHudHpTextColorCode->setText("#FFFFFF");
    ui->btnMetroidHudHpTextColor->setStyleSheet("background-color: #FFFFFF;");
    {
        Config::Table& instcfg = emuInstance->getLocalConfig();
        instcfg.SetInt("Metroid.Visual.HudHpTextColorR", 255);
        instcfg.SetInt("Metroid.Visual.HudHpTextColorG", 255);
        instcfg.SetInt("Metroid.Visual.HudHpTextColorB", 255);
    }

    // Weapon Position
    ui->comboMetroidHudWeaponPosition->setCurrentIndex(kHudPositionCustomIndex); // Custom
    setSlider(ui->spinMetroidHudWeaponX, ui->inputMetroidHudWeaponX, ui->labelMetroidHudWeaponX, 230);
    setSlider(ui->spinMetroidHudWeaponY, ui->inputMetroidHudWeaponY, ui->labelMetroidHudWeaponY, 99);
    ui->leMetroidHudAmmoPrefix->setText("");
    ui->comboMetroidHudAmmoAlign->setCurrentIndex(2); // Right
    ui->comboMetroidHudAmmoTextColor->setCurrentIndex(0);
    ui->spinMetroidHudAmmoTextColorR->setValue(255);
    ui->spinMetroidHudAmmoTextColorG->setValue(255);
    ui->spinMetroidHudAmmoTextColorB->setValue(255);
    ui->leMetroidHudAmmoTextColorCode->setText("#FFFFFF");
    ui->btnMetroidHudAmmoTextColor->setStyleSheet("background-color: #FFFFFF;");
    {
        Config::Table& instcfg = emuInstance->getLocalConfig();
        instcfg.SetInt("Metroid.Visual.HudAmmoTextColorR", 255);
        instcfg.SetInt("Metroid.Visual.HudAmmoTextColorG", 255);
        instcfg.SetInt("Metroid.Visual.HudAmmoTextColorB", 255);
    }

    // Weapon Icon
    ui->cbMetroidHudWeaponIconShow->setChecked(true);
    ui->comboMetroidHudWeaponIconMode->setCurrentIndex(1); // Independent
    setSlider(ui->spinMetroidHudWeaponIconOffsetX, ui->inputMetroidHudWeaponIconOffsetX, ui->labelMetroidHudWeaponIconOffsetX, 0);
    setSlider(ui->spinMetroidHudWeaponIconOffsetY, ui->inputMetroidHudWeaponIconOffsetY, ui->labelMetroidHudWeaponIconOffsetY, 10);
    ui->comboMetroidHudWeaponIconPosition->setCurrentIndex(kHudPositionCustomIndex); // Custom
    setSlider(ui->spinMetroidHudWeaponIconPosX, ui->inputMetroidHudWeaponIconPosX, ui->labelMetroidHudWeaponIconPosX, 239);
    setSlider(ui->spinMetroidHudWeaponIconPosY, ui->inputMetroidHudWeaponIconPosY, ui->labelMetroidHudWeaponIconPosY, 149);
    ui->comboMetroidHudWeaponIconAnchorX->setCurrentIndex(1);
    ui->comboMetroidHudWeaponIconAnchorY->setCurrentIndex(1);
    ui->cbMetroidHudWeaponIconColorOverlay->setChecked(false);

    // HP Gauge
    ui->cbMetroidHudHpGauge->setChecked(true);
    ui->comboMetroidHudHpGaugeOrientation->setCurrentIndex(1); // Vertical
    setSlider(ui->spinMetroidHudHpGaugeLength,  ui->inputMetroidHudHpGaugeLength,  ui->labelMetroidHudHpGaugeLength,  80);
    setSlider(ui->spinMetroidHudHpGaugeWidth,   ui->inputMetroidHudHpGaugeWidth,   ui->labelMetroidHudHpGaugeWidth,   3);
    setSlider(ui->spinMetroidHudHpGaugeOffsetX, ui->inputMetroidHudHpGaugeOffsetX, ui->labelMetroidHudHpGaugeOffsetX, -14);
    setSlider(ui->spinMetroidHudHpGaugeOffsetY, ui->inputMetroidHudHpGaugeOffsetY, ui->labelMetroidHudHpGaugeOffsetY, 1);
    ui->comboMetroidHudHpGaugeAnchor->setCurrentIndex(3);
    ui->comboMetroidHudHpGaugePosMode->setCurrentIndex(1);
    setSlider(ui->spinMetroidHudHpGaugePosX, ui->inputMetroidHudHpGaugePosX, ui->labelMetroidHudHpGaugePosX, 14);
    setSlider(ui->spinMetroidHudHpGaugePosY, ui->inputMetroidHudHpGaugePosY, ui->labelMetroidHudHpGaugePosY, 56);
    ui->cbMetroidHudHpGaugeAutoColor->setChecked(true);
    {
        Config::Table& instcfg = emuInstance->getLocalConfig();
        instcfg.SetInt("Metroid.Visual.HudHpGaugeColorR", 56);
        instcfg.SetInt("Metroid.Visual.HudHpGaugeColorG", 192);
        instcfg.SetInt("Metroid.Visual.HudHpGaugeColorB", 8);
        ui->spinMetroidHudHpGaugeColorR->setValue(56);
        ui->spinMetroidHudHpGaugeColorG->setValue(192);
        ui->spinMetroidHudHpGaugeColorB->setValue(8);
        ui->leMetroidHudHpGaugeColorCode->setText("#38C008");
        ui->btnMetroidHudHpGaugeColor->setStyleSheet("background-color: #38C008;");
    }

    // Ammo Gauge
    ui->cbMetroidHudAmmoGauge->setChecked(true);
    ui->comboMetroidHudAmmoGaugeOrientation->setCurrentIndex(1); // Vertical
    setSlider(ui->spinMetroidHudAmmoGaugeLength,  ui->inputMetroidHudAmmoGaugeLength,  ui->labelMetroidHudAmmoGaugeLength,  80);
    setSlider(ui->spinMetroidHudAmmoGaugeWidth,   ui->inputMetroidHudAmmoGaugeWidth,   ui->labelMetroidHudAmmoGaugeWidth,   3);
    setSlider(ui->spinMetroidHudAmmoGaugeOffsetX, ui->inputMetroidHudAmmoGaugeOffsetX, ui->labelMetroidHudAmmoGaugeOffsetX, 9);
    setSlider(ui->spinMetroidHudAmmoGaugeOffsetY, ui->inputMetroidHudAmmoGaugeOffsetY, ui->labelMetroidHudAmmoGaugeOffsetY, 2);
    ui->comboMetroidHudAmmoGaugeAnchor->setCurrentIndex(2);
    ui->comboMetroidHudAmmoGaugePosMode->setCurrentIndex(0);
    setSlider(ui->spinMetroidHudAmmoGaugePosX, ui->inputMetroidHudAmmoGaugePosX, ui->labelMetroidHudAmmoGaugePosX, 239);
    setSlider(ui->spinMetroidHudAmmoGaugePosY, ui->inputMetroidHudAmmoGaugePosY, ui->labelMetroidHudAmmoGaugePosY, 56);
    {
        Config::Table& instcfg = emuInstance->getLocalConfig();
        instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorR", 56);
        instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorG", 192);
        instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorB", 8);
        ui->spinMetroidHudAmmoGaugeColorR->setValue(56);
        ui->spinMetroidHudAmmoGaugeColorG->setValue(192);
        ui->spinMetroidHudAmmoGaugeColorB->setValue(8);
        ui->leMetroidHudAmmoGaugeColorCode->setText("#38C008");
        ui->btnMetroidHudAmmoGaugeColor->setStyleSheet("background-color: #38C008;");
    }
}

void MelonPrimeInputConfig::resetMatchStatusDefaults()
{

    auto setSlider = [this](QSlider* sl, QSpinBox* input, QLabel* lbl, int v) {
        sl->setValue(v);
        if (input)
            input->setValue(v);
        if (lbl) lbl->setText(QString::number(v));
    };

    ui->cbMetroidHudMatchStatusShow->setChecked(true);
    setSlider(ui->spinMetroidHudMatchStatusX, ui->inputMetroidHudMatchStatusX, ui->labelMetroidHudMatchStatusX, 20);
    setSlider(ui->spinMetroidHudMatchStatusY, ui->inputMetroidHudMatchStatusY, ui->labelMetroidHudMatchStatusY, 19);
    ui->comboMetroidHudMatchStatusLabelPos->setCurrentIndex(0); // Above
    setSlider(ui->spinMetroidHudMatchStatusLabelOfsX, ui->inputMetroidHudMatchStatusLabelOfsX, ui->labelMetroidHudMatchStatusLabelOfsX, 0);
    setSlider(ui->spinMetroidHudMatchStatusLabelOfsY, ui->inputMetroidHudMatchStatusLabelOfsY, ui->labelMetroidHudMatchStatusLabelOfsY, 1);
    ui->leMetroidHudMatchStatusLabelPoints->setText("points");
    ui->leMetroidHudMatchStatusLabelOctoliths->setText("octoliths");
    ui->leMetroidHudMatchStatusLabelLives->setText("lives left");
    ui->leMetroidHudMatchStatusLabelRingTime->setText("ring time");
    ui->leMetroidHudMatchStatusLabelPrimeTime->setText("prime time");

    // Colors
    {
        Config::Table& instcfg = emuInstance->getLocalConfig();
        auto resetClr = [&](QPushButton* btn, QLineEdit* le, QSpinBox* spR, QSpinBox* spG, QSpinBox* spB,
                             const char* kR, const char* kG, const char* kB, int r, int g, int b) {
            instcfg.SetInt(kR, r); instcfg.SetInt(kG, g); instcfg.SetInt(kB, b);
            if (spR) spR->setValue(r);
            if (spG) spG->setValue(g);
            if (spB) spB->setValue(b);
            if (le) le->setText(QColor(r, g, b).name().toUpper());
            btn->setStyleSheet(QString("background-color: %1;").arg(QColor(r, g, b).name()));
        };
        resetClr(ui->btnMetroidHudMatchStatusColor, ui->leMetroidHudMatchStatusColorCode,
            ui->spinMetroidHudMatchStatusColorR, ui->spinMetroidHudMatchStatusColorG, ui->spinMetroidHudMatchStatusColorB,
            "Metroid.Visual.HudMatchStatusColorR", "Metroid.Visual.HudMatchStatusColorG", "Metroid.Visual.HudMatchStatusColorB",
            255, 255, 255);
        ui->comboMetroidHudMatchStatusLabelColor->setCurrentIndex(0);
        resetClr(ui->btnMetroidHudMatchStatusLabelColor, ui->leMetroidHudMatchStatusLabelColorCode,
            ui->spinMetroidHudMatchStatusLabelColorR, ui->spinMetroidHudMatchStatusLabelColorG, ui->spinMetroidHudMatchStatusLabelColorB,
            "Metroid.Visual.HudMatchStatusLabelColorR", "Metroid.Visual.HudMatchStatusLabelColorG", "Metroid.Visual.HudMatchStatusLabelColorB",
            255, 255, 255);
        ui->comboMetroidHudMatchStatusValueColor->setCurrentIndex(0);
        resetClr(ui->btnMetroidHudMatchStatusValueColor, ui->leMetroidHudMatchStatusValueColorCode,
            ui->spinMetroidHudMatchStatusValueColorR, ui->spinMetroidHudMatchStatusValueColorG, ui->spinMetroidHudMatchStatusValueColorB,
            "Metroid.Visual.HudMatchStatusValueColorR", "Metroid.Visual.HudMatchStatusValueColorG", "Metroid.Visual.HudMatchStatusValueColorB",
            255, 255, 255);
        ui->comboMetroidHudMatchStatusSepColor->setCurrentIndex(0);
        resetClr(ui->btnMetroidHudMatchStatusSepColor, ui->leMetroidHudMatchStatusSepColorCode,
            ui->spinMetroidHudMatchStatusSepColorR, ui->spinMetroidHudMatchStatusSepColorG, ui->spinMetroidHudMatchStatusSepColorB,
            "Metroid.Visual.HudMatchStatusSepColorR", "Metroid.Visual.HudMatchStatusSepColorG", "Metroid.Visual.HudMatchStatusSepColorB",
            255, 255, 255);
        ui->comboMetroidHudMatchStatusGoalColor->setCurrentIndex(0);
        resetClr(ui->btnMetroidHudMatchStatusGoalColor, ui->leMetroidHudMatchStatusGoalColorCode,
            ui->spinMetroidHudMatchStatusGoalColorR, ui->spinMetroidHudMatchStatusGoalColorG, ui->spinMetroidHudMatchStatusGoalColorB,
            "Metroid.Visual.HudMatchStatusGoalColorR", "Metroid.Visual.HudMatchStatusGoalColorG", "Metroid.Visual.HudMatchStatusGoalColorB",
            255, 255, 255);
    }
}

void MelonPrimeInputConfig::resetRankTimeDefaults()
{
    auto setSlider = [this](QSlider* sl, QSpinBox* input, int v) {
        sl->setValue(v);
        if (input) input->setValue(v);
    };

    // Rank
    ui->cbMetroidHudRankShow->setChecked(true);
    setSlider(ui->spinMetroidHudRankX, ui->inputMetroidHudRankX, 20);
    setSlider(ui->spinMetroidHudRankY, ui->inputMetroidHudRankY, 30);
    ui->comboMetroidHudRankColor->setCurrentIndex(0);
    ui->comboMetroidHudRankAlign->setCurrentIndex(0);
    ui->leMetroidHudRankPrefix->setText("");
    ui->cbMetroidHudRankShowOrdinal->setChecked(true);
    ui->leMetroidHudRankSuffix->setText("");
    ui->spinMetroidHudRankColorR->setValue(255);
    ui->spinMetroidHudRankColorG->setValue(255);
    ui->spinMetroidHudRankColorB->setValue(255);
    ui->leMetroidHudRankColorCode->setText("#FFFFFF");
    ui->btnMetroidHudRankColor->setStyleSheet("background-color: #ffffff;");

    // Time Left
    ui->cbMetroidHudTimeLeftShow->setChecked(false);
    setSlider(ui->spinMetroidHudTimeLeftX, ui->inputMetroidHudTimeLeftX, 20);
    setSlider(ui->spinMetroidHudTimeLeftY, ui->inputMetroidHudTimeLeftY, 42);
    ui->comboMetroidHudTimeLeftAlign->setCurrentIndex(0);
    ui->comboMetroidHudTimeLeftColor->setCurrentIndex(0);
    ui->spinMetroidHudTimeLeftColorR->setValue(255);
    ui->spinMetroidHudTimeLeftColorG->setValue(255);
    ui->spinMetroidHudTimeLeftColorB->setValue(255);
    ui->leMetroidHudTimeLeftColorCode->setText("#FFFFFF");
    ui->btnMetroidHudTimeLeftColor->setStyleSheet("background-color: #ffffff;");

    // Bomb Left
    ui->cbMetroidHudBombLeftShow->setChecked(true);
    setSlider(ui->spinMetroidHudBombLeftX, ui->inputMetroidHudBombLeftX, 210);
    setSlider(ui->spinMetroidHudBombLeftY, ui->inputMetroidHudBombLeftY, 185);
    ui->comboMetroidHudBombLeftAlign->setCurrentIndex(0);
    ui->comboMetroidHudBombLeftColor->setCurrentIndex(0);
    ui->spinMetroidHudBombLeftColorR->setValue(255);
    ui->spinMetroidHudBombLeftColorG->setValue(255);
    ui->spinMetroidHudBombLeftColorB->setValue(255);
    ui->leMetroidHudBombLeftColorCode->setText("#FFFFFF");
    ui->btnMetroidHudBombLeftColor->setStyleSheet("background-color: #ffffff;");
    ui->leMetroidHudBombLeftPrefix->setText("bombs:");
    ui->leMetroidHudBombLeftSuffix->setText("");

    // Time Limit
    ui->cbMetroidHudTimeLimitShow->setChecked(false);
    setSlider(ui->spinMetroidHudTimeLimitX, ui->inputMetroidHudTimeLimitX, 20);
    setSlider(ui->spinMetroidHudTimeLimitY, ui->inputMetroidHudTimeLimitY, 54);
    ui->comboMetroidHudTimeLimitAlign->setCurrentIndex(0);
    ui->comboMetroidHudTimeLimitColor->setCurrentIndex(0);
    ui->spinMetroidHudTimeLimitColorR->setValue(255);
    ui->spinMetroidHudTimeLimitColorG->setValue(255);
    ui->spinMetroidHudTimeLimitColorB->setValue(255);
    ui->leMetroidHudTimeLimitColorCode->setText("#FFFFFF");
    ui->btnMetroidHudTimeLimitColor->setStyleSheet("background-color: #ffffff;");

    applyVisualPreview();
}



