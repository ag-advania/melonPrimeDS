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
#include <QTimer>

#include "MelonPrimeInputConfig.h"
#include "MelonPrimeInputConfigInternal.h"
#include "ui_MelonPrimeInputConfig.h"
#include "Config.h"
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeCustomHud.h"
#endif

using namespace melonDS;

namespace {

// Helper shared by all reset functions: set slider + optional spinbox + optional label.
void setSliderValue(QSlider* sl, QSpinBox* input, QLabel* lbl, int v)
{
    sl->setValue(v);
    if (input) input->setValue(v);
    if (lbl)   lbl->setText(QString::number(v));
}

void setSliderValue(QSlider* sl, QSpinBox* input, int v)
{
    setSliderValue(sl, input, nullptr, v);
}

struct DefaultLocalConfig
{
    toml::value Data;
    Config::Table Table;

    DefaultLocalConfig()
        : Data(toml::table())
        , Table(Data, "Instance0")
    {
    }
};

void setComboIndexSilently(QComboBox* combo, int index)
{
    combo->blockSignals(true);
    combo->setCurrentIndex(index);
    combo->blockSignals(false);
}

int getHudColorComboIndex(int r, int g, int b, int customIndex = kHudColorCustomIndex,
    int presetIndexOffset = 0)
{
    return findPresetColorIndex(
        kUnifiedHudColorPresets,
        kHudColorPresetCount,
        r, g, b,
        customIndex,
        presetIndexOffset);
}

void applyColorDefaults(Config::Table& defaults, Config::Table& instcfg,
    QComboBox* combo, QLineEdit* lineEdit,
    QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB,
    QPushButton* button,
    const char* keyR, const char* keyG, const char* keyB,
    int customIndex = kHudColorCustomIndex,
    int presetIndexOffset = 0)
{
    const int r = defaults.GetInt(keyR);
    const int g = defaults.GetInt(keyG);
    const int b = defaults.GetInt(keyB);

    instcfg.SetInt(keyR, r);
    instcfg.SetInt(keyG, g);
    instcfg.SetInt(keyB, b);

    if (combo)
        setComboIndexSilently(combo, getHudColorComboIndex(r, g, b, customIndex, presetIndexOffset));

    setColorSpinValues(spinR, spinG, spinB, r, g, b);
    if (lineEdit)
        lineEdit->setText(formatColorHex(r, g, b));
    if (button)
        button->setStyleSheet(QString("background-color: %1;").arg(QColor(r, g, b).name()));
}

void applySubColorDefaults(Config::Table& defaults, Config::Table& instcfg,
    QComboBox* combo, QLineEdit* lineEdit,
    QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB,
    QPushButton* button,
    const char* overallKey,
    const char* keyR, const char* keyG, const char* keyB)
{
    const bool useOverall = defaults.GetBool(overallKey);
    instcfg.SetBool(overallKey, useOverall);

    const int r = defaults.GetInt(keyR);
    const int g = defaults.GetInt(keyG);
    const int b = defaults.GetInt(keyB);

    instcfg.SetInt(keyR, r);
    instcfg.SetInt(keyG, g);
    instcfg.SetInt(keyB, b);

    setComboIndexSilently(combo, useOverall
        ? kHudColorOverallIndex
        : getHudColorComboIndex(r, g, b, kHudColorSubColorCustomIndex, kHudColorSubColorPresetIndexOffset));

    setColorSpinValues(spinR, spinG, spinB, r, g, b);
    lineEdit->setText(formatColorHex(r, g, b));
    button->setStyleSheet(QString("background-color: %1;").arg(QColor(r, g, b).name()));
}
} // namespace

void MelonPrimeInputConfig::saveConfig()
{
    Config::Table& instcfg = emuInstance->getLocalConfig();
    const bool oldClipCursorToBottomScreenWhenNotInGame =
        instcfg.GetBool("Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame");
    const bool oldInGameTopScreenOnly =
        instcfg.GetBool("Metroid.Visual.InGameTopScreenOnly");
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

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
    const bool clipCursorToBottomScreenWhenNotInGame =
        (ui->cbMetroidClipCursorToBottomScreenWhenNotInGame->checkState() == Qt::Checked);
    instcfg.SetBool("Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame", clipCursorToBottomScreenWhenNotInGame);
    const bool inGameTopScreenOnly =
        (ui->cbMetroidInGameTopScreenOnly->checkState() == Qt::Checked);
    instcfg.SetBool("Metroid.Visual.InGameTopScreenOnly", inGameTopScreenOnly);
    if (oldClipCursorToBottomScreenWhenNotInGame != clipCursorToBottomScreenWhenNotInGame) {
        QTimer::singleShot(0, this, [this]() {
            for (int i = 0; i < emuInstance->getNumWindows(); ++i) {
                MainWindow* win = emuInstance->getWindow(i);
                if (win && win->panel)
                    win->panel->updateClipIfNeeded();
            }
        });
    }
    if (oldInGameTopScreenOnly != inGameTopScreenOnly) {
        QTimer::singleShot(0, this, [this]() {
            for (int i = 0; i < emuInstance->getNumWindows(); ++i) {
                MainWindow* win = emuInstance->getWindow(i);
                if (win && win->panel)
                    QMetaObject::invokeMethod(win->panel, "onScreenLayoutChanged", Qt::QueuedConnection);
            }
        });
    }
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
    instcfg.SetBool("Metroid.Visual.HudBombLeftShow",     ui->cbMetroidHudBombLeftShow->checkState() == Qt::Checked);
    instcfg.SetBool("Metroid.Visual.HudBombLeftTextShow", ui->cbMetroidHudBombLeftTextShow->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudBombLeftX",        ui->spinMetroidHudBombLeftX->value());
    instcfg.SetInt("Metroid.Visual.HudBombLeftY",        ui->spinMetroidHudBombLeftY->value());
    instcfg.SetInt("Metroid.Visual.HudBombLeftAlign",    ui->comboMetroidHudBombLeftAlign->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudBombLeftColorR",   ui->spinMetroidHudBombLeftColorR->value());
    instcfg.SetInt("Metroid.Visual.HudBombLeftColorG",   ui->spinMetroidHudBombLeftColorG->value());
    instcfg.SetInt("Metroid.Visual.HudBombLeftColorB",   ui->spinMetroidHudBombLeftColorB->value());
    instcfg.SetString("Metroid.Visual.HudBombLeftPrefix", ui->leMetroidHudBombLeftPrefix->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudBombLeftSuffix", ui->leMetroidHudBombLeftSuffix->text().toStdString());
    instcfg.SetBool("Metroid.Visual.HudBombLeftIconShow",         ui->cbMetroidHudBombLeftIconShow->isChecked());
    instcfg.SetBool("Metroid.Visual.HudBombLeftIconColorOverlay", ui->cbMetroidHudBombLeftIconColorOverlay->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconColorR",       ui->spinMetroidHudBombLeftIconColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconColorG",       ui->spinMetroidHudBombLeftIconColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconColorB",       ui->spinMetroidHudBombLeftIconColorB->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconMode",         ui->comboMetroidHudBombLeftIconMode->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconOfsX",         ui->spinMetroidHudBombLeftIconOfsX->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconOfsY",         ui->spinMetroidHudBombLeftIconOfsY->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconPosX",         ui->spinMetroidHudBombLeftIconPosX->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconPosY",         ui->spinMetroidHudBombLeftIconPosY->value());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconAnchorX",      ui->comboMetroidHudBombLeftIconAnchorX->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudBombLeftIconAnchorY",      ui->comboMetroidHudBombLeftIconAnchorY->currentIndex());

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
    instcfg.SetBool("Metroid.UI.SectionInGameApply",  ui->btnToggleInGameApply->isChecked());
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

    // Crosshair - General
    instcfg.SetBool("Metroid.Visual.CrosshairOutline", ui->cbMetroidCrosshairOutline->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairOutlineOpacity", ui->spinMetroidCrosshairOutlineOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOutlineThickness", ui->spinMetroidCrosshairOutlineThickness->value());
    instcfg.SetBool("Metroid.Visual.CrosshairCenterDot", ui->cbMetroidCrosshairCenterDot->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairDotOpacity", ui->spinMetroidCrosshairDotOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairDotThickness", ui->spinMetroidCrosshairDotThickness->value());
    instcfg.SetBool("Metroid.Visual.CrosshairTStyle", ui->cbMetroidCrosshairTStyle->checkState() == Qt::Checked);

    // Crosshair - Inner Lines
    instcfg.SetBool("Metroid.Visual.CrosshairInnerShow", ui->cbMetroidCrosshairInnerShow->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairInnerOpacity", ui->spinMetroidCrosshairInnerOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerLengthX", ui->spinMetroidCrosshairInnerLengthX->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerLengthY", ui->spinMetroidCrosshairInnerLengthY->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerThickness", ui->spinMetroidCrosshairInnerThickness->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerOffset", ui->spinMetroidCrosshairInnerOffset->value());
    instcfg.SetBool("Metroid.Visual.CrosshairInnerLinkXY", ui->cbMetroidCrosshairInnerLinkXY->checkState() == Qt::Checked);

    // Crosshair - Outer Lines
    instcfg.SetBool("Metroid.Visual.CrosshairOuterShow", ui->cbMetroidCrosshairOuterShow->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairOuterOpacity", ui->spinMetroidCrosshairOuterOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOuterLengthX", ui->spinMetroidCrosshairOuterLengthX->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOuterLengthY", ui->spinMetroidCrosshairOuterLengthY->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOuterThickness", ui->spinMetroidCrosshairOuterThickness->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOuterOffset", ui->spinMetroidCrosshairOuterOffset->value());
    instcfg.SetBool("Metroid.Visual.CrosshairOuterLinkXY", ui->cbMetroidCrosshairOuterLinkXY->checkState() == Qt::Checked);

    // HUD Radar
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
    DefaultLocalConfig defaultLocal;
    Config::Table& defaults = defaultLocal.Table;
    Config::Table& instcfg = emuInstance->getLocalConfig();

    applyColorDefaults(defaults, instcfg,
        ui->comboMetroidCrosshairColor, ui->leMetroidCrosshairColorCode,
        ui->spinMetroidCrosshairR, ui->spinMetroidCrosshairG, ui->spinMetroidCrosshairB,
        ui->btnMetroidCrosshairColor,
        "Metroid.Visual.CrosshairColorR", "Metroid.Visual.CrosshairColorG", "Metroid.Visual.CrosshairColorB");

    ui->cbMetroidCrosshairOutline->setChecked(defaults.GetBool("Metroid.Visual.CrosshairOutline"));
    ui->spinMetroidCrosshairOutlineOpacity->setValue(defaults.GetDouble("Metroid.Visual.CrosshairOutlineOpacity"));
    ui->spinMetroidCrosshairOutlineThickness->setValue(defaults.GetInt("Metroid.Visual.CrosshairOutlineThickness"));
    ui->cbMetroidCrosshairCenterDot->setChecked(defaults.GetBool("Metroid.Visual.CrosshairCenterDot"));
    ui->spinMetroidCrosshairDotOpacity->setValue(defaults.GetDouble("Metroid.Visual.CrosshairDotOpacity"));
    ui->spinMetroidCrosshairDotThickness->setValue(defaults.GetInt("Metroid.Visual.CrosshairDotThickness"));
    ui->cbMetroidCrosshairTStyle->setChecked(defaults.GetBool("Metroid.Visual.CrosshairTStyle"));

    ui->cbMetroidCrosshairInnerShow->setChecked(defaults.GetBool("Metroid.Visual.CrosshairInnerShow"));
    ui->spinMetroidCrosshairInnerOpacity->setValue(defaults.GetDouble("Metroid.Visual.CrosshairInnerOpacity"));
    ui->spinMetroidCrosshairInnerLengthX->setValue(defaults.GetInt("Metroid.Visual.CrosshairInnerLengthX"));
    ui->spinMetroidCrosshairInnerLengthY->setValue(defaults.GetInt("Metroid.Visual.CrosshairInnerLengthY"));
    ui->cbMetroidCrosshairInnerLinkXY->setChecked(defaults.GetBool("Metroid.Visual.CrosshairInnerLinkXY"));
    ui->spinMetroidCrosshairInnerThickness->setValue(defaults.GetInt("Metroid.Visual.CrosshairInnerThickness"));
    ui->spinMetroidCrosshairInnerOffset->setValue(defaults.GetInt("Metroid.Visual.CrosshairInnerOffset"));

    ui->cbMetroidCrosshairOuterShow->setChecked(defaults.GetBool("Metroid.Visual.CrosshairOuterShow"));
    ui->spinMetroidCrosshairOuterOpacity->setValue(defaults.GetDouble("Metroid.Visual.CrosshairOuterOpacity"));
    ui->spinMetroidCrosshairOuterLengthX->setValue(defaults.GetInt("Metroid.Visual.CrosshairOuterLengthX"));
    ui->spinMetroidCrosshairOuterLengthY->setValue(defaults.GetInt("Metroid.Visual.CrosshairOuterLengthY"));
    ui->cbMetroidCrosshairOuterLinkXY->setChecked(defaults.GetBool("Metroid.Visual.CrosshairOuterLinkXY"));
    ui->spinMetroidCrosshairOuterThickness->setValue(defaults.GetInt("Metroid.Visual.CrosshairOuterThickness"));
    ui->spinMetroidCrosshairOuterOffset->setValue(defaults.GetInt("Metroid.Visual.CrosshairOuterOffset"));
}

void MelonPrimeInputConfig::resetHpAmmoDefaults()
{
    DefaultLocalConfig defaultLocal;
    Config::Table& defaults = defaultLocal.Table;
    Config::Table& instcfg = emuInstance->getLocalConfig();

    const int hpX = defaults.GetInt("Metroid.Visual.HudHpX");
    const int hpY = defaults.GetInt("Metroid.Visual.HudHpY");
    setComboIndexSilently(ui->comboMetroidHudHpPosition,
        findPositionPresetIndex(kHudHpPositionPresets, hpX, hpY, kHudPositionCustomIndex));
    setSliderValue(ui->spinMetroidHudHpX, ui->inputMetroidHudHpX, ui->labelMetroidHudHpX, hpX);
    setSliderValue(ui->spinMetroidHudHpY, ui->inputMetroidHudHpY, ui->labelMetroidHudHpY, hpY);
    ui->leMetroidHudHpPrefix->setText(QString::fromStdString(defaults.GetString("Metroid.Visual.HudHpPrefix")));
    ui->comboMetroidHudHpAlign->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudHpAlign"));
    ui->cbMetroidHudHpTextAutoColor->setChecked(defaults.GetBool("Metroid.Visual.HudHpTextAutoColor"));
    applyColorDefaults(defaults, instcfg,
        ui->comboMetroidHudHpTextColor, ui->leMetroidHudHpTextColorCode,
        ui->spinMetroidHudHpTextColorR, ui->spinMetroidHudHpTextColorG, ui->spinMetroidHudHpTextColorB,
        ui->btnMetroidHudHpTextColor,
        "Metroid.Visual.HudHpTextColorR", "Metroid.Visual.HudHpTextColorG", "Metroid.Visual.HudHpTextColorB");

    const int weaponX = defaults.GetInt("Metroid.Visual.HudWeaponX");
    const int weaponY = defaults.GetInt("Metroid.Visual.HudWeaponY");
    setComboIndexSilently(ui->comboMetroidHudWeaponPosition,
        findPositionPresetIndex(kHudWeaponPositionPresets, weaponX, weaponY, kHudPositionCustomIndex));
    setSliderValue(ui->spinMetroidHudWeaponX, ui->inputMetroidHudWeaponX, ui->labelMetroidHudWeaponX, weaponX);
    setSliderValue(ui->spinMetroidHudWeaponY, ui->inputMetroidHudWeaponY, ui->labelMetroidHudWeaponY, weaponY);
    ui->leMetroidHudAmmoPrefix->setText(QString::fromStdString(defaults.GetString("Metroid.Visual.HudAmmoPrefix")));
    ui->comboMetroidHudAmmoAlign->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudAmmoAlign"));
    applyColorDefaults(defaults, instcfg,
        ui->comboMetroidHudAmmoTextColor, ui->leMetroidHudAmmoTextColorCode,
        ui->spinMetroidHudAmmoTextColorR, ui->spinMetroidHudAmmoTextColorG, ui->spinMetroidHudAmmoTextColorB,
        ui->btnMetroidHudAmmoTextColor,
        "Metroid.Visual.HudAmmoTextColorR", "Metroid.Visual.HudAmmoTextColorG", "Metroid.Visual.HudAmmoTextColorB");

    ui->cbMetroidHudWeaponIconShow->setChecked(defaults.GetBool("Metroid.Visual.HudWeaponIconShow"));
    ui->comboMetroidHudWeaponIconMode->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudWeaponIconMode"));
    setSliderValue(ui->spinMetroidHudWeaponIconOffsetX, ui->inputMetroidHudWeaponIconOffsetX, ui->labelMetroidHudWeaponIconOffsetX,
        defaults.GetInt("Metroid.Visual.HudWeaponIconOffsetX"));
    setSliderValue(ui->spinMetroidHudWeaponIconOffsetY, ui->inputMetroidHudWeaponIconOffsetY, ui->labelMetroidHudWeaponIconOffsetY,
        defaults.GetInt("Metroid.Visual.HudWeaponIconOffsetY"));
    const int weaponIconPosX = defaults.GetInt("Metroid.Visual.HudWeaponIconPosX");
    const int weaponIconPosY = defaults.GetInt("Metroid.Visual.HudWeaponIconPosY");
    setComboIndexSilently(ui->comboMetroidHudWeaponIconPosition,
        findPositionPresetIndex(kHudWeaponIconPositionPresets, weaponIconPosX, weaponIconPosY, kHudPositionCustomIndex));
    setSliderValue(ui->spinMetroidHudWeaponIconPosX, ui->inputMetroidHudWeaponIconPosX, ui->labelMetroidHudWeaponIconPosX, weaponIconPosX);
    setSliderValue(ui->spinMetroidHudWeaponIconPosY, ui->inputMetroidHudWeaponIconPosY, ui->labelMetroidHudWeaponIconPosY, weaponIconPosY);
    ui->comboMetroidHudWeaponIconAnchorX->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudWeaponIconAnchorX"));
    ui->comboMetroidHudWeaponIconAnchorY->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudWeaponIconAnchorY"));
    ui->cbMetroidHudWeaponIconColorOverlay->setChecked(defaults.GetBool("Metroid.Visual.HudWeaponIconColorOverlay"));

    ui->cbMetroidHudHpGauge->setChecked(defaults.GetBool("Metroid.Visual.HudHpGauge"));
    ui->comboMetroidHudHpGaugeOrientation->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudHpGaugeOrientation"));
    setSliderValue(ui->spinMetroidHudHpGaugeLength, ui->inputMetroidHudHpGaugeLength, ui->labelMetroidHudHpGaugeLength,
        defaults.GetInt("Metroid.Visual.HudHpGaugeLength"));
    setSliderValue(ui->spinMetroidHudHpGaugeWidth, ui->inputMetroidHudHpGaugeWidth, ui->labelMetroidHudHpGaugeWidth,
        defaults.GetInt("Metroid.Visual.HudHpGaugeWidth"));
    setSliderValue(ui->spinMetroidHudHpGaugeOffsetX, ui->inputMetroidHudHpGaugeOffsetX, ui->labelMetroidHudHpGaugeOffsetX,
        defaults.GetInt("Metroid.Visual.HudHpGaugeOffsetX"));
    setSliderValue(ui->spinMetroidHudHpGaugeOffsetY, ui->inputMetroidHudHpGaugeOffsetY, ui->labelMetroidHudHpGaugeOffsetY,
        defaults.GetInt("Metroid.Visual.HudHpGaugeOffsetY"));
    ui->comboMetroidHudHpGaugeAnchor->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudHpGaugeAnchor"));
    ui->comboMetroidHudHpGaugePosMode->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudHpGaugePosMode"));
    setSliderValue(ui->spinMetroidHudHpGaugePosX, ui->inputMetroidHudHpGaugePosX, ui->labelMetroidHudHpGaugePosX,
        defaults.GetInt("Metroid.Visual.HudHpGaugePosX"));
    setSliderValue(ui->spinMetroidHudHpGaugePosY, ui->inputMetroidHudHpGaugePosY, ui->labelMetroidHudHpGaugePosY,
        defaults.GetInt("Metroid.Visual.HudHpGaugePosY"));
    ui->cbMetroidHudHpGaugeAutoColor->setChecked(defaults.GetBool("Metroid.Visual.HudHpGaugeAutoColor"));
    applyColorDefaults(defaults, instcfg,
        ui->comboMetroidHudHpGaugeColor, ui->leMetroidHudHpGaugeColorCode,
        ui->spinMetroidHudHpGaugeColorR, ui->spinMetroidHudHpGaugeColorG, ui->spinMetroidHudHpGaugeColorB,
        ui->btnMetroidHudHpGaugeColor,
        "Metroid.Visual.HudHpGaugeColorR", "Metroid.Visual.HudHpGaugeColorG", "Metroid.Visual.HudHpGaugeColorB");

    ui->cbMetroidHudAmmoGauge->setChecked(defaults.GetBool("Metroid.Visual.HudAmmoGauge"));
    ui->comboMetroidHudAmmoGaugeOrientation->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudAmmoGaugeOrientation"));
    setSliderValue(ui->spinMetroidHudAmmoGaugeLength, ui->inputMetroidHudAmmoGaugeLength, ui->labelMetroidHudAmmoGaugeLength,
        defaults.GetInt("Metroid.Visual.HudAmmoGaugeLength"));
    setSliderValue(ui->spinMetroidHudAmmoGaugeWidth, ui->inputMetroidHudAmmoGaugeWidth, ui->labelMetroidHudAmmoGaugeWidth,
        defaults.GetInt("Metroid.Visual.HudAmmoGaugeWidth"));
    setSliderValue(ui->spinMetroidHudAmmoGaugeOffsetX, ui->inputMetroidHudAmmoGaugeOffsetX, ui->labelMetroidHudAmmoGaugeOffsetX,
        defaults.GetInt("Metroid.Visual.HudAmmoGaugeOffsetX"));
    setSliderValue(ui->spinMetroidHudAmmoGaugeOffsetY, ui->inputMetroidHudAmmoGaugeOffsetY, ui->labelMetroidHudAmmoGaugeOffsetY,
        defaults.GetInt("Metroid.Visual.HudAmmoGaugeOffsetY"));
    ui->comboMetroidHudAmmoGaugeAnchor->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudAmmoGaugeAnchor"));
    ui->comboMetroidHudAmmoGaugePosMode->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudAmmoGaugePosMode"));
    setSliderValue(ui->spinMetroidHudAmmoGaugePosX, ui->inputMetroidHudAmmoGaugePosX, ui->labelMetroidHudAmmoGaugePosX,
        defaults.GetInt("Metroid.Visual.HudAmmoGaugePosX"));
    setSliderValue(ui->spinMetroidHudAmmoGaugePosY, ui->inputMetroidHudAmmoGaugePosY, ui->labelMetroidHudAmmoGaugePosY,
        defaults.GetInt("Metroid.Visual.HudAmmoGaugePosY"));
    applyColorDefaults(defaults, instcfg,
        ui->comboMetroidHudAmmoGaugeColor, ui->leMetroidHudAmmoGaugeColorCode,
        ui->spinMetroidHudAmmoGaugeColorR, ui->spinMetroidHudAmmoGaugeColorG, ui->spinMetroidHudAmmoGaugeColorB,
        ui->btnMetroidHudAmmoGaugeColor,
        "Metroid.Visual.HudAmmoGaugeColorR", "Metroid.Visual.HudAmmoGaugeColorG", "Metroid.Visual.HudAmmoGaugeColorB");
}

void MelonPrimeInputConfig::resetMatchStatusDefaults()
{
    DefaultLocalConfig defaultLocal;
    Config::Table& defaults = defaultLocal.Table;
    Config::Table& instcfg = emuInstance->getLocalConfig();

    ui->cbMetroidHudMatchStatusShow->setChecked(defaults.GetBool("Metroid.Visual.HudMatchStatusShow"));
    setSliderValue(ui->spinMetroidHudMatchStatusX, ui->inputMetroidHudMatchStatusX, ui->labelMetroidHudMatchStatusX,
        defaults.GetInt("Metroid.Visual.HudMatchStatusX"));
    setSliderValue(ui->spinMetroidHudMatchStatusY, ui->inputMetroidHudMatchStatusY, ui->labelMetroidHudMatchStatusY,
        defaults.GetInt("Metroid.Visual.HudMatchStatusY"));
    ui->comboMetroidHudMatchStatusLabelPos->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudMatchStatusLabelPos"));
    setSliderValue(ui->spinMetroidHudMatchStatusLabelOfsX, ui->inputMetroidHudMatchStatusLabelOfsX, ui->labelMetroidHudMatchStatusLabelOfsX,
        defaults.GetInt("Metroid.Visual.HudMatchStatusLabelOfsX"));
    setSliderValue(ui->spinMetroidHudMatchStatusLabelOfsY, ui->inputMetroidHudMatchStatusLabelOfsY, ui->labelMetroidHudMatchStatusLabelOfsY,
        defaults.GetInt("Metroid.Visual.HudMatchStatusLabelOfsY"));
    ui->leMetroidHudMatchStatusLabelPoints->setText(QString::fromStdString(defaults.GetString("Metroid.Visual.HudMatchStatusLabelPoints")));
    ui->leMetroidHudMatchStatusLabelOctoliths->setText(QString::fromStdString(defaults.GetString("Metroid.Visual.HudMatchStatusLabelOctoliths")));
    ui->leMetroidHudMatchStatusLabelLives->setText(QString::fromStdString(defaults.GetString("Metroid.Visual.HudMatchStatusLabelLives")));
    ui->leMetroidHudMatchStatusLabelRingTime->setText(QString::fromStdString(defaults.GetString("Metroid.Visual.HudMatchStatusLabelRingTime")));
    ui->leMetroidHudMatchStatusLabelPrimeTime->setText(QString::fromStdString(defaults.GetString("Metroid.Visual.HudMatchStatusLabelPrimeTime")));

    applyColorDefaults(defaults, instcfg,
        ui->comboMetroidHudMatchStatusColor, ui->leMetroidHudMatchStatusColorCode,
        ui->spinMetroidHudMatchStatusColorR, ui->spinMetroidHudMatchStatusColorG, ui->spinMetroidHudMatchStatusColorB,
        ui->btnMetroidHudMatchStatusColor,
        "Metroid.Visual.HudMatchStatusColorR", "Metroid.Visual.HudMatchStatusColorG", "Metroid.Visual.HudMatchStatusColorB");
    applySubColorDefaults(defaults, instcfg,
        ui->comboMetroidHudMatchStatusLabelColor, ui->leMetroidHudMatchStatusLabelColorCode,
        ui->spinMetroidHudMatchStatusLabelColorR, ui->spinMetroidHudMatchStatusLabelColorG, ui->spinMetroidHudMatchStatusLabelColorB,
        ui->btnMetroidHudMatchStatusLabelColor,
        "Metroid.Visual.HudMatchStatusLabelColorOverall",
        "Metroid.Visual.HudMatchStatusLabelColorR", "Metroid.Visual.HudMatchStatusLabelColorG", "Metroid.Visual.HudMatchStatusLabelColorB");
    applySubColorDefaults(defaults, instcfg,
        ui->comboMetroidHudMatchStatusValueColor, ui->leMetroidHudMatchStatusValueColorCode,
        ui->spinMetroidHudMatchStatusValueColorR, ui->spinMetroidHudMatchStatusValueColorG, ui->spinMetroidHudMatchStatusValueColorB,
        ui->btnMetroidHudMatchStatusValueColor,
        "Metroid.Visual.HudMatchStatusValueColorOverall",
        "Metroid.Visual.HudMatchStatusValueColorR", "Metroid.Visual.HudMatchStatusValueColorG", "Metroid.Visual.HudMatchStatusValueColorB");
    applySubColorDefaults(defaults, instcfg,
        ui->comboMetroidHudMatchStatusSepColor, ui->leMetroidHudMatchStatusSepColorCode,
        ui->spinMetroidHudMatchStatusSepColorR, ui->spinMetroidHudMatchStatusSepColorG, ui->spinMetroidHudMatchStatusSepColorB,
        ui->btnMetroidHudMatchStatusSepColor,
        "Metroid.Visual.HudMatchStatusSepColorOverall",
        "Metroid.Visual.HudMatchStatusSepColorR", "Metroid.Visual.HudMatchStatusSepColorG", "Metroid.Visual.HudMatchStatusSepColorB");
    applySubColorDefaults(defaults, instcfg,
        ui->comboMetroidHudMatchStatusGoalColor, ui->leMetroidHudMatchStatusGoalColorCode,
        ui->spinMetroidHudMatchStatusGoalColorR, ui->spinMetroidHudMatchStatusGoalColorG, ui->spinMetroidHudMatchStatusGoalColorB,
        ui->btnMetroidHudMatchStatusGoalColor,
        "Metroid.Visual.HudMatchStatusGoalColorOverall",
        "Metroid.Visual.HudMatchStatusGoalColorR", "Metroid.Visual.HudMatchStatusGoalColorG", "Metroid.Visual.HudMatchStatusGoalColorB");
}

void MelonPrimeInputConfig::resetRankTimeDefaults()
{
    DefaultLocalConfig defaultLocal;
    Config::Table& defaults = defaultLocal.Table;
    Config::Table& instcfg = emuInstance->getLocalConfig();

    ui->cbMetroidHudRankShow->setChecked(defaults.GetBool("Metroid.Visual.HudRankShow"));
    setSliderValue(ui->spinMetroidHudRankX, ui->inputMetroidHudRankX, defaults.GetInt("Metroid.Visual.HudRankX"));
    setSliderValue(ui->spinMetroidHudRankY, ui->inputMetroidHudRankY, defaults.GetInt("Metroid.Visual.HudRankY"));
    ui->comboMetroidHudRankAlign->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudRankAlign"));
    ui->leMetroidHudRankPrefix->setText(QString::fromStdString(defaults.GetString("Metroid.Visual.HudRankPrefix")));
    ui->cbMetroidHudRankShowOrdinal->setChecked(defaults.GetBool("Metroid.Visual.HudRankShowOrdinal"));
    ui->leMetroidHudRankSuffix->setText(QString::fromStdString(defaults.GetString("Metroid.Visual.HudRankSuffix")));
    applyColorDefaults(defaults, instcfg,
        ui->comboMetroidHudRankColor, ui->leMetroidHudRankColorCode,
        ui->spinMetroidHudRankColorR, ui->spinMetroidHudRankColorG, ui->spinMetroidHudRankColorB,
        ui->btnMetroidHudRankColor,
        "Metroid.Visual.HudRankColorR", "Metroid.Visual.HudRankColorG", "Metroid.Visual.HudRankColorB");

    ui->cbMetroidHudTimeLeftShow->setChecked(defaults.GetBool("Metroid.Visual.HudTimeLeftShow"));
    setSliderValue(ui->spinMetroidHudTimeLeftX, ui->inputMetroidHudTimeLeftX, defaults.GetInt("Metroid.Visual.HudTimeLeftX"));
    setSliderValue(ui->spinMetroidHudTimeLeftY, ui->inputMetroidHudTimeLeftY, defaults.GetInt("Metroid.Visual.HudTimeLeftY"));
    ui->comboMetroidHudTimeLeftAlign->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudTimeLeftAlign"));
    applyColorDefaults(defaults, instcfg,
        ui->comboMetroidHudTimeLeftColor, ui->leMetroidHudTimeLeftColorCode,
        ui->spinMetroidHudTimeLeftColorR, ui->spinMetroidHudTimeLeftColorG, ui->spinMetroidHudTimeLeftColorB,
        ui->btnMetroidHudTimeLeftColor,
        "Metroid.Visual.HudTimeLeftColorR", "Metroid.Visual.HudTimeLeftColorG", "Metroid.Visual.HudTimeLeftColorB");

    ui->cbMetroidHudTimeLimitShow->setChecked(defaults.GetBool("Metroid.Visual.HudTimeLimitShow"));
    setSliderValue(ui->spinMetroidHudTimeLimitX, ui->inputMetroidHudTimeLimitX, defaults.GetInt("Metroid.Visual.HudTimeLimitX"));
    setSliderValue(ui->spinMetroidHudTimeLimitY, ui->inputMetroidHudTimeLimitY, defaults.GetInt("Metroid.Visual.HudTimeLimitY"));
    ui->comboMetroidHudTimeLimitAlign->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudTimeLimitAlign"));
    applyColorDefaults(defaults, instcfg,
        ui->comboMetroidHudTimeLimitColor, ui->leMetroidHudTimeLimitColorCode,
        ui->spinMetroidHudTimeLimitColorR, ui->spinMetroidHudTimeLimitColorG, ui->spinMetroidHudTimeLimitColorB,
        ui->btnMetroidHudTimeLimitColor,
        "Metroid.Visual.HudTimeLimitColorR", "Metroid.Visual.HudTimeLimitColorG", "Metroid.Visual.HudTimeLimitColorB");

    applyVisualPreview();
}

void MelonPrimeInputConfig::resetBombLeftDefaults()
{
    DefaultLocalConfig defaultLocal;
    Config::Table& defaults = defaultLocal.Table;
    Config::Table& instcfg = emuInstance->getLocalConfig();

    ui->cbMetroidHudBombLeftShow->setChecked(defaults.GetBool("Metroid.Visual.HudBombLeftShow"));
    ui->cbMetroidHudBombLeftTextShow->setChecked(defaults.GetBool("Metroid.Visual.HudBombLeftTextShow"));
    setSliderValue(ui->spinMetroidHudBombLeftX, ui->inputMetroidHudBombLeftX, defaults.GetInt("Metroid.Visual.HudBombLeftX"));
    setSliderValue(ui->spinMetroidHudBombLeftY, ui->inputMetroidHudBombLeftY, defaults.GetInt("Metroid.Visual.HudBombLeftY"));
    ui->comboMetroidHudBombLeftAlign->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudBombLeftAlign"));
    ui->leMetroidHudBombLeftPrefix->setText(QString::fromStdString(defaults.GetString("Metroid.Visual.HudBombLeftPrefix")));
    ui->leMetroidHudBombLeftSuffix->setText(QString::fromStdString(defaults.GetString("Metroid.Visual.HudBombLeftSuffix")));
    applyColorDefaults(defaults, instcfg,
        ui->comboMetroidHudBombLeftColor, ui->leMetroidHudBombLeftColorCode,
        ui->spinMetroidHudBombLeftColorR, ui->spinMetroidHudBombLeftColorG, ui->spinMetroidHudBombLeftColorB,
        ui->btnMetroidHudBombLeftColor,
        "Metroid.Visual.HudBombLeftColorR", "Metroid.Visual.HudBombLeftColorG", "Metroid.Visual.HudBombLeftColorB");

    ui->cbMetroidHudBombLeftIconShow->setChecked(defaults.GetBool("Metroid.Visual.HudBombLeftIconShow"));
    ui->cbMetroidHudBombLeftIconColorOverlay->setChecked(defaults.GetBool("Metroid.Visual.HudBombLeftIconColorOverlay"));
    applyColorDefaults(defaults, instcfg,
        ui->comboMetroidHudBombLeftIconColor, ui->leMetroidHudBombLeftIconColorCode,
        ui->spinMetroidHudBombLeftIconColorR, ui->spinMetroidHudBombLeftIconColorG, ui->spinMetroidHudBombLeftIconColorB,
        ui->btnMetroidHudBombLeftIconColor,
        "Metroid.Visual.HudBombLeftIconColorR", "Metroid.Visual.HudBombLeftIconColorG", "Metroid.Visual.HudBombLeftIconColorB");
    ui->comboMetroidHudBombLeftIconMode->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudBombLeftIconMode"));
    setSliderValue(ui->spinMetroidHudBombLeftIconOfsX, ui->inputMetroidHudBombLeftIconOfsX, defaults.GetInt("Metroid.Visual.HudBombLeftIconOfsX"));
    setSliderValue(ui->spinMetroidHudBombLeftIconOfsY, ui->inputMetroidHudBombLeftIconOfsY, defaults.GetInt("Metroid.Visual.HudBombLeftIconOfsY"));
    setSliderValue(ui->spinMetroidHudBombLeftIconPosX, ui->inputMetroidHudBombLeftIconPosX, defaults.GetInt("Metroid.Visual.HudBombLeftIconPosX"));
    setSliderValue(ui->spinMetroidHudBombLeftIconPosY, ui->inputMetroidHudBombLeftIconPosY, defaults.GetInt("Metroid.Visual.HudBombLeftIconPosY"));
    ui->comboMetroidHudBombLeftIconAnchorX->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudBombLeftIconAnchorX"));
    ui->comboMetroidHudBombLeftIconAnchorY->setCurrentIndex(defaults.GetInt("Metroid.Visual.HudBombLeftIconAnchorY"));

    applyVisualPreview();
}
void MelonPrimeInputConfig::resetRadarDefaults()
{
    DefaultLocalConfig defaultLocal;
    Config::Table& defaults = defaultLocal.Table;

    ui->cbMetroidBtmOverlayEnable->setChecked(defaults.GetBool("Metroid.Visual.BtmOverlayEnable"));
    setSliderValue(ui->spinMetroidBtmOverlayDstX, ui->inputMetroidBtmOverlayDstX, ui->labelMetroidBtmOverlayDstX, defaults.GetInt("Metroid.Visual.BtmOverlayDstX"));
    setSliderValue(ui->spinMetroidBtmOverlayDstY, ui->inputMetroidBtmOverlayDstY, ui->labelMetroidBtmOverlayDstY, defaults.GetInt("Metroid.Visual.BtmOverlayDstY"));
    setSliderValue(ui->spinMetroidBtmOverlayDstSize, ui->inputMetroidBtmOverlayDstSize, ui->labelMetroidBtmOverlayDstSize, defaults.GetInt("Metroid.Visual.BtmOverlayDstSize"));
    ui->spinMetroidBtmOverlayOpacity->setValue(defaults.GetDouble("Metroid.Visual.BtmOverlayOpacity"));
    ui->sliderMetroidBtmOverlayOpacity->setValue(qRound(defaults.GetDouble("Metroid.Visual.BtmOverlayOpacity") * 100));
    setSliderValue(ui->spinMetroidBtmOverlaySrcRadius, ui->inputMetroidBtmOverlaySrcRadius, ui->labelMetroidBtmOverlaySrcRadius, defaults.GetInt("Metroid.Visual.BtmOverlaySrcRadius"));

    applyAndPreviewRadar();
}

