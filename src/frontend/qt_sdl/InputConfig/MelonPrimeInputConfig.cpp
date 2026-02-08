/*
    Copyright 2016-2025 melonDS team
    (MelonPrime specific configuration extension)
*/

#include <QGroupBox>
#include <QLabel>
#include <QGridLayout>
#include <QTabWidget>

#include "MelonPrimeInputConfig.h"
#include "ui_MelonPrimeInputConfig.h"
#include "Config.h"

// MapButton.h をインクルードする前に、InputConfigDialog の完全な定義が必要です。
// これがないと MapButton 内で parentDialog へのアクセス時にエラーになります。
#include "InputConfigDialog.h" 

#include "MapButton.h"
#include "Platform.h"
#include "VideoSettingsDialog.h"

using namespace melonDS;

MelonPrimeInputConfig::MelonPrimeInputConfig(EmuInstance* emu, QWidget* parent) :
    QWidget(parent),
    ui(new Ui::MelonPrimeInputConfig),
    emuInstance(emu)
{
    ui->setupUi(this);

    Config::Table& instcfg = emuInstance->getLocalConfig();
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

    // Load key values
    int i = 0;
    for (int hotkey : hk_tabAddonsMetroid)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        addonsMetroidKeyMap[i] = keycfg.GetInt(btn);
        addonsMetroidJoyMap[i] = joycfg.GetInt(btn);
        i++;
    }

    i = 0;
    for (int hotkey : hk_tabAddonsMetroid2)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        addonsMetroid2KeyMap[i] = keycfg.GetInt(btn);
        addonsMetroid2JoyMap[i] = joycfg.GetInt(btn);
        i++;
    }

    // Populate Pages
    populatePage(ui->tabAddonsMetroid, hk_tabAddonsMetroid_labels, addonsMetroidKeyMap, addonsMetroidJoyMap);
    populatePage(ui->tabAddonsMetroid2, hk_tabAddonsMetroid2_labels, addonsMetroid2KeyMap, addonsMetroid2JoyMap);

    // Sensitivities
    ui->metroidMphSensitvitySpinBox->setValue(instcfg.GetDouble("Metroid.Sensitivity.Mph"));
    ui->metroidAimSensitvitySpinBox->setValue(instcfg.GetInt("Metroid.Sensitivity.Aim"));
    ui->metroidAimYAxisScaleSpinBox->setValue(instcfg.GetDouble("Metroid.Sensitivity.AimYAxisScale"));
    ui->metroidAimAdjustSpinBox->setValue(instcfg.GetDouble("Metroid.Aim.Adjust"));

    // Toggles
    ui->cbMetroidEnableSnapTap->setChecked(instcfg.GetBool("Metroid.Operation.SnapTap"));
    ui->cbMetroidUnlockAll->setChecked(instcfg.GetBool("Metroid.Data.Unlock"));
    ui->cbMetroidApplyHeadphone->setChecked(instcfg.GetBool("Metroid.Apply.Headphone"));
    ui->cbMetroidUseFirmwareName->setChecked(instcfg.GetBool("Metroid.Use.Firmware.Name"));

    // Hunter license
    ui->cbMetroidApplyHunter->setChecked(instcfg.GetBool("Metroid.HunterLicense.Hunter.Apply"));
    ui->comboMetroidSelectedHunter->setCurrentIndex(
        instcfg.GetInt("Metroid.HunterLicense.Hunter.Selected"));

    ui->cbMetroidApplyColor->setChecked(instcfg.GetBool("Metroid.HunterLicense.Color.Apply"));
    ui->comboMetroidSelectedColor->setCurrentIndex(
        instcfg.GetInt("Metroid.HunterLicense.Color.Selected"));

    // Volume
    ui->cbMetroidApplySfxVolume->setChecked(instcfg.GetBool("Metroid.Apply.SfxVolume"));
    ui->spinMetroidVolumeSFX->setValue(instcfg.GetInt("Metroid.Volume.SFX"));

    ui->cbMetroidApplyMusicVolume->setChecked(instcfg.GetBool("Metroid.Apply.MusicVolume"));
    ui->spinMetroidVolumeMusic->setValue(instcfg.GetInt("Metroid.Volume.Music"));

    // Other Metroid Settings 2 Tab
    ui->cbMetroidApplyJoy2KeySupport->setChecked(instcfg.GetBool("Metroid.Apply.joy2KeySupport"));
    ui->cbMetroidEnableStylusMode->setChecked(instcfg.GetBool("Metroid.Enable.stylusMode"));
}

MelonPrimeInputConfig::~MelonPrimeInputConfig()
{
    delete ui;
}

QTabWidget* MelonPrimeInputConfig::getTabWidget()
{
    return ui->metroidTabWidget;
}

void MelonPrimeInputConfig::populatePage(QWidget* page, const std::initializer_list<const char*>& labels, int* keymap, int* joymap)
{
    // This logic is copied from InputConfigDialog::populatePage but adapted for local use
    bool ishotkey = true;

    QHBoxLayout* main_layout = new QHBoxLayout();

    QGroupBox* group;
    QGridLayout* group_layout;

    group = new QGroupBox("Keyboard mappings:");
    main_layout->addWidget(group);
    group_layout = new QGridLayout();
    group_layout->setSpacing(1);
    int i = 0;
    for (const char* labelStr : labels)
    {
        QLabel* label = new QLabel(QString(labelStr) + ":");
        KeyMapButton* btn = new KeyMapButton(&keymap[i], ishotkey);

        group_layout->addWidget(label, i, 0);
        group_layout->addWidget(btn, i, 1);
        i++;
    }
    group_layout->setRowStretch(labels.size(), 1);
    group->setLayout(group_layout);
    group->setMinimumWidth(275);

    group = new QGroupBox("Joystick mappings:");
    main_layout->addWidget(group);
    group_layout = new QGridLayout();
    group_layout->setSpacing(1);
    i = 0;
    for (const char* labelStr : labels)
    {
        QLabel* label = new QLabel(QString(labelStr) + ":");
        JoyMapButton* btn = new JoyMapButton(&joymap[i], ishotkey);

        group_layout->addWidget(label, i, 0);
        group_layout->addWidget(btn, i, 1);
        i++;
    }
    group_layout->setRowStretch(labels.size(), 1);
    group->setLayout(group_layout);
    group->setMinimumWidth(275);

    page->setLayout(main_layout);
}

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
}

void MelonPrimeInputConfig::on_metroidResetSensitivityValues_clicked()
{
    ui->metroidMphSensitvitySpinBox->setValue(-3);
    ui->metroidAimSensitvitySpinBox->setValue(63);
    ui->metroidAimYAxisScaleSpinBox->setValue(1.500000);
    ui->metroidAimAdjustSpinBox->setValue(0.010000);
}

void MelonPrimeInputConfig::on_metroidSetVideoQualityToLow_clicked()
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Screen.UseGL", true);
    cfg.SetBool("Screen.VSync", false);
    cfg.SetInt("Screen.VSyncInterval", 1);
    cfg.SetInt("3D.Renderer", renderer3D_Software);
    cfg.SetBool("3D.Soft.Threaded", true);
    cfg.SetInt("3D.GL.ScaleFactor", 4);
    cfg.SetBool("3D.GL.BetterPolygons", true);
}

void MelonPrimeInputConfig::on_metroidSetVideoQualityToHigh_clicked()
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Screen.UseGL", true);
    cfg.SetBool("Screen.VSync", false);
    cfg.SetInt("Screen.VSyncInterval", 1);
    cfg.SetInt("3D.Renderer", renderer3D_OpenGL);
    cfg.SetBool("3D.Soft.Threaded", true);
    cfg.SetInt("3D.GL.ScaleFactor", 4);
    cfg.SetBool("3D.GL.BetterPolygons", true);
}

void MelonPrimeInputConfig::on_metroidSetVideoQualityToHigh2_clicked()
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Screen.UseGL", true);
    cfg.SetBool("Screen.VSync", false);
    cfg.SetInt("Screen.VSyncInterval", 1);
    cfg.SetInt("3D.Renderer", renderer3D_OpenGLCompute);
    cfg.SetBool("3D.Soft.Threaded", true);
    cfg.SetInt("3D.GL.ScaleFactor", 4);
    cfg.SetBool("3D.GL.BetterPolygons", true);
}

void MelonPrimeInputConfig::on_cbMetroidEnableSnapTap_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Metroid.Operation.SnapTap", state != 0);
}

void MelonPrimeInputConfig::on_cbMetroidUnlockAll_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Metroid.Data.Unlock", state != 0);
}

void MelonPrimeInputConfig::on_cbMetroidApplyHeadphone_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Metroid.Apply.Headphone", state != 0);
}

void MelonPrimeInputConfig::on_cbMetroidUseFirmwareName_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Metroid.Use.Firmware.Name", state != 0);
}