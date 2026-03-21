/*
    Copyright 2016-2025 melonDS team
    (MelonPrime specific configuration extension)
*/

#include <QGroupBox>
#include <QLabel>
#include <QGridLayout>
#include <QTabWidget>
#include <QSpinBox>
#include <QColor>
#include <QLineEdit>
#include <QComboBox>

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
    ui->cbMetroidDisableMphAimSmoothing->setChecked(instcfg.GetBool("Metroid.Aim.Disable.MphAimSmoothing"));
    ui->cbMetroidEnableAimAccumulator->setChecked(instcfg.GetBool("Metroid.Aim.Enable.Accumulator"));

    // Screen Sync Mode
    ui->comboMetroidScreenSyncMode->setCurrentIndex(instcfg.GetInt("Metroid.Screen.SyncMode"));

    // Custom HUD
    ui->cbMetroidEnableCustomHud->setChecked(instcfg.GetBool("Metroid.Visual.CustomHUD"));

    // Crosshair — Color
    int chR = instcfg.GetInt("Metroid.Visual.CrosshairColorR");

    // HUD element positions
    ui->spinMetroidHudHpX->setValue(instcfg.GetInt("Metroid.Visual.HudHpX"));
    ui->spinMetroidHudHpY->setValue(instcfg.GetInt("Metroid.Visual.HudHpY"));
    ui->spinMetroidHudWeaponX->setValue(instcfg.GetInt("Metroid.Visual.HudWeaponX"));
    ui->spinMetroidHudWeaponY->setValue(instcfg.GetInt("Metroid.Visual.HudWeaponY"));
    ui->comboMetroidHudWeaponLayout->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudWeaponLayout"));

    // Gauge settings — HP
    ui->cbMetroidHudHpGauge->setChecked(instcfg.GetBool("Metroid.Visual.HudHpGauge"));
    ui->comboMetroidHudHpGaugeOrientation->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudHpGaugeOrientation"));
    ui->spinMetroidHudHpGaugeLength->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeLength"));
    ui->spinMetroidHudHpGaugeWidth->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeWidth"));
    ui->spinMetroidHudHpGaugeOffsetX->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeOffsetX"));
    ui->spinMetroidHudHpGaugeOffsetY->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeOffsetY"));
    ui->comboMetroidHudHpGaugeAnchor->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudHpGaugeAnchor"));
    ui->cbMetroidHudHpGaugeAutoColor->setChecked(instcfg.GetBool("Metroid.Visual.HudHpGaugeAutoColor"));
    ui->spinMetroidHudHpGaugeColorR->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeColorR"));
    ui->spinMetroidHudHpGaugeColorG->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeColorG"));
    ui->spinMetroidHudHpGaugeColorB->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeColorB"));

    // HP gauge color preset detection
    {
        struct Clr { int r, g, b; };
        static const Clr presets[] = {
            {0,255,0}, {255,0,0}, {255,165,0}, {255,255,0}, {0,200,255}, {255,255,255}
        };
        int r = ui->spinMetroidHudHpGaugeColorR->value();
        int g = ui->spinMetroidHudHpGaugeColorG->value();
        int b = ui->spinMetroidHudHpGaugeColorB->value();
        int idx = 6; // Custom
        for (int i = 0; i < 6; i++) {
            if (r == presets[i].r && g == presets[i].g && b == presets[i].b) { idx = i; break; }
        }
        ui->comboMetroidHudHpGaugeColor->setCurrentIndex(idx);
    }
    // HP gauge color preset → RGB
    connect(ui->comboMetroidHudHpGaugeColor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        struct Clr { int r, g, b; };
        static const Clr presets[] = {
            {0,255,0}, {255,0,0}, {255,165,0}, {255,255,0}, {0,200,255}, {255,255,255}
        };
        if (idx < 0 || idx >= 6) return;
        ui->spinMetroidHudHpGaugeColorR->blockSignals(true);
        ui->spinMetroidHudHpGaugeColorG->blockSignals(true);
        ui->spinMetroidHudHpGaugeColorB->blockSignals(true);
        ui->spinMetroidHudHpGaugeColorR->setValue(presets[idx].r);
        ui->spinMetroidHudHpGaugeColorG->setValue(presets[idx].g);
        ui->spinMetroidHudHpGaugeColorB->setValue(presets[idx].b);
        ui->spinMetroidHudHpGaugeColorR->blockSignals(false);
        ui->spinMetroidHudHpGaugeColorG->blockSignals(false);
        ui->spinMetroidHudHpGaugeColorB->blockSignals(false);
    });
    auto hpGaugeColorToCustom = [this]() {
        ui->comboMetroidHudHpGaugeColor->blockSignals(true);
        ui->comboMetroidHudHpGaugeColor->setCurrentIndex(6);
        ui->comboMetroidHudHpGaugeColor->blockSignals(false);
    };
    connect(ui->spinMetroidHudHpGaugeColorR, QOverload<int>::of(&QSpinBox::valueChanged), this, hpGaugeColorToCustom);
    connect(ui->spinMetroidHudHpGaugeColorG, QOverload<int>::of(&QSpinBox::valueChanged), this, hpGaugeColorToCustom);
    connect(ui->spinMetroidHudHpGaugeColorB, QOverload<int>::of(&QSpinBox::valueChanged), this, hpGaugeColorToCustom);

    // Gauge settings — Ammo
    ui->cbMetroidHudAmmoGauge->setChecked(instcfg.GetBool("Metroid.Visual.HudAmmoGauge"));
    ui->comboMetroidHudAmmoGaugeOrientation->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOrientation"));
    ui->spinMetroidHudAmmoGaugeLength->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeLength"));
    ui->spinMetroidHudAmmoGaugeWidth->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeWidth"));
    ui->spinMetroidHudAmmoGaugeOffsetX->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetX"));
    ui->spinMetroidHudAmmoGaugeOffsetY->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetY"));
    ui->comboMetroidHudAmmoGaugeAnchor->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeAnchor"));
    ui->spinMetroidHudAmmoGaugeColorR->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeColorR"));
    ui->spinMetroidHudAmmoGaugeColorG->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeColorG"));
    ui->spinMetroidHudAmmoGaugeColorB->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeColorB"));

    // Ammo gauge color preset detection (Cyan first in list)
    {
        struct Clr { int r, g, b; };
        static const Clr presets[] = {
            {0,200,255}, {0,255,0}, {255,0,0}, {255,165,0}, {255,255,0}, {255,255,255}
        };
        int r = ui->spinMetroidHudAmmoGaugeColorR->value();
        int g = ui->spinMetroidHudAmmoGaugeColorG->value();
        int b = ui->spinMetroidHudAmmoGaugeColorB->value();
        int idx = 6; // Custom
        for (int i = 0; i < 6; i++) {
            if (r == presets[i].r && g == presets[i].g && b == presets[i].b) { idx = i; break; }
        }
        ui->comboMetroidHudAmmoGaugeColor->setCurrentIndex(idx);
    }
    // Ammo gauge color preset → RGB
    connect(ui->comboMetroidHudAmmoGaugeColor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        struct Clr { int r, g, b; };
        static const Clr presets[] = {
            {0,200,255}, {0,255,0}, {255,0,0}, {255,165,0}, {255,255,0}, {255,255,255}
        };
        if (idx < 0 || idx >= 6) return;
        ui->spinMetroidHudAmmoGaugeColorR->blockSignals(true);
        ui->spinMetroidHudAmmoGaugeColorG->blockSignals(true);
        ui->spinMetroidHudAmmoGaugeColorB->blockSignals(true);
        ui->spinMetroidHudAmmoGaugeColorR->setValue(presets[idx].r);
        ui->spinMetroidHudAmmoGaugeColorG->setValue(presets[idx].g);
        ui->spinMetroidHudAmmoGaugeColorB->setValue(presets[idx].b);
        ui->spinMetroidHudAmmoGaugeColorR->blockSignals(false);
        ui->spinMetroidHudAmmoGaugeColorG->blockSignals(false);
        ui->spinMetroidHudAmmoGaugeColorB->blockSignals(false);
    });
    auto ammoGaugeColorToCustom = [this]() {
        ui->comboMetroidHudAmmoGaugeColor->blockSignals(true);
        ui->comboMetroidHudAmmoGaugeColor->setCurrentIndex(6);
        ui->comboMetroidHudAmmoGaugeColor->blockSignals(false);
    };
    connect(ui->spinMetroidHudAmmoGaugeColorR, QOverload<int>::of(&QSpinBox::valueChanged), this, ammoGaugeColorToCustom);
    connect(ui->spinMetroidHudAmmoGaugeColorG, QOverload<int>::of(&QSpinBox::valueChanged), this, ammoGaugeColorToCustom);
    connect(ui->spinMetroidHudAmmoGaugeColorB, QOverload<int>::of(&QSpinBox::valueChanged), this, ammoGaugeColorToCustom);

    // HUD position presets: detect current preset from X/Y values
    // 0=TopLeft 1=TopCenter 2=TopRight 3=Right 4=BottomRight 5=BottomCenter 6=BottomLeft 7=Left 8=Custom
    {
        struct Pos { int x, y; };
        static const Pos hpPresets[] = {
            {4,8}, {120,8}, {220,8}, {220,96}, {220,188}, {120,188}, {4,188}, {4,96}
        };
        static const Pos wpnPresets[] = {
            {4,2}, {120,2}, {226,2}, {226,82}, {226,164}, {120,164}, {4,164}, {4,82}
        };
        int hpIdx = 8, wpnIdx = 8;
        int hpX = ui->spinMetroidHudHpX->value(), hpY = ui->spinMetroidHudHpY->value();
        int wpnX = ui->spinMetroidHudWeaponX->value(), wpnY = ui->spinMetroidHudWeaponY->value();
        for (int i = 0; i < 8; i++) {
            if (hpX == hpPresets[i].x && hpY == hpPresets[i].y) hpIdx = i;
            if (wpnX == wpnPresets[i].x && wpnY == wpnPresets[i].y) wpnIdx = i;
        }
        ui->comboMetroidHudHpPosition->setCurrentIndex(hpIdx);
        ui->comboMetroidHudWeaponPosition->setCurrentIndex(wpnIdx);
    }

    // HP position preset → update X/Y
    connect(ui->comboMetroidHudHpPosition, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        struct Pos { int x, y; };
        static const Pos presets[] = {
            {4,8}, {120,8}, {220,8}, {220,96}, {220,188}, {120,188}, {4,188}, {4,96}
        };
        if (idx < 0 || idx >= 8) return;
        ui->spinMetroidHudHpX->blockSignals(true);
        ui->spinMetroidHudHpY->blockSignals(true);
        ui->spinMetroidHudHpX->setValue(presets[idx].x);
        ui->spinMetroidHudHpY->setValue(presets[idx].y);
        ui->spinMetroidHudHpX->blockSignals(false);
        ui->spinMetroidHudHpY->blockSignals(false);
    });
    // HP X/Y manual change → switch to Custom
    connect(ui->spinMetroidHudHpX, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        ui->comboMetroidHudHpPosition->blockSignals(true);
        ui->comboMetroidHudHpPosition->setCurrentIndex(8);
        ui->comboMetroidHudHpPosition->blockSignals(false);
    });
    connect(ui->spinMetroidHudHpY, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        ui->comboMetroidHudHpPosition->blockSignals(true);
        ui->comboMetroidHudHpPosition->setCurrentIndex(8);
        ui->comboMetroidHudHpPosition->blockSignals(false);
    });

    // Weapon position preset → update X/Y
    connect(ui->comboMetroidHudWeaponPosition, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        struct Pos { int x, y; };
        static const Pos presets[] = {
            {4,2}, {120,2}, {226,2}, {226,82}, {226,164}, {120,164}, {4,164}, {4,82}
        };
        if (idx < 0 || idx >= 8) return;
        ui->spinMetroidHudWeaponX->blockSignals(true);
        ui->spinMetroidHudWeaponY->blockSignals(true);
        ui->spinMetroidHudWeaponX->setValue(presets[idx].x);
        ui->spinMetroidHudWeaponY->setValue(presets[idx].y);
        ui->spinMetroidHudWeaponX->blockSignals(false);
        ui->spinMetroidHudWeaponY->blockSignals(false);
    });
    // Weapon X/Y manual change → switch to Custom
    connect(ui->spinMetroidHudWeaponX, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        ui->comboMetroidHudWeaponPosition->blockSignals(true);
        ui->comboMetroidHudWeaponPosition->setCurrentIndex(8);
        ui->comboMetroidHudWeaponPosition->blockSignals(false);
    });
    connect(ui->spinMetroidHudWeaponY, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        ui->comboMetroidHudWeaponPosition->blockSignals(true);
        ui->comboMetroidHudWeaponPosition->setCurrentIndex(8);
        ui->comboMetroidHudWeaponPosition->blockSignals(false);
    });

    // Crosshair — Color (continued)
    int chG = instcfg.GetInt("Metroid.Visual.CrosshairColorG");
    int chB = instcfg.GetInt("Metroid.Visual.CrosshairColorB");
    ui->spinMetroidCrosshairR->setValue(chR);
    ui->spinMetroidCrosshairG->setValue(chG);
    ui->spinMetroidCrosshairB->setValue(chB);
    ui->leMetroidCrosshairColorCode->setText(
        QString("#%1%2%3")
            .arg(chR, 2, 16, QChar('0'))
            .arg(chG, 2, 16, QChar('0'))
            .arg(chB, 2, 16, QChar('0')).toUpper());

    // Detect color preset (match against known presets, else Custom=8)
    {
        struct Preset { int r, g, b; };
        static const Preset presets[] = {
            {255,255,255}, // 0: White
            {0,255,0},     // 1: Green
            {127,255,0},   // 2: Yellow Green
            {191,255,0},   // 3: Green Yellow
            {255,255,0},   // 4: Yellow
            {0,255,255},   // 5: Cyan
            {255,105,180}, // 6: Pink
            {255,0,0},     // 7: Red
        };
        int presetIdx = 8; // Custom
        for (int i = 0; i < 8; i++) {
            if (chR == presets[i].r && chG == presets[i].g && chB == presets[i].b) {
                presetIdx = i; break;
            }
        }
        ui->comboMetroidCrosshairColor->setCurrentIndex(presetIdx);
    }

    // Crosshair — General
    ui->cbMetroidCrosshairOutline->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairOutline"));
    ui->spinMetroidCrosshairOutlineOpacity->setValue(instcfg.GetDouble("Metroid.Visual.CrosshairOutlineOpacity"));
    int olThick = instcfg.GetInt("Metroid.Visual.CrosshairOutlineThickness");
    ui->spinMetroidCrosshairOutlineThickness->setValue(olThick > 0 ? olThick : 1);
    ui->cbMetroidCrosshairCenterDot->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairCenterDot"));
    ui->spinMetroidCrosshairDotOpacity->setValue(instcfg.GetDouble("Metroid.Visual.CrosshairDotOpacity"));
    int dotThick = instcfg.GetInt("Metroid.Visual.CrosshairDotThickness");
    ui->spinMetroidCrosshairDotThickness->setValue(dotThick > 0 ? dotThick : 1);
    ui->cbMetroidCrosshairTStyle->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairTStyle"));

    // Crosshair — Inner Lines
    ui->cbMetroidCrosshairInnerShow->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairInnerShow"));
    ui->spinMetroidCrosshairInnerOpacity->setValue(instcfg.GetDouble("Metroid.Visual.CrosshairInnerOpacity"));
    ui->spinMetroidCrosshairInnerLengthX->setValue(instcfg.GetInt("Metroid.Visual.CrosshairInnerLengthX"));
    ui->spinMetroidCrosshairInnerLengthY->setValue(instcfg.GetInt("Metroid.Visual.CrosshairInnerLengthY"));
    int innerThick = instcfg.GetInt("Metroid.Visual.CrosshairInnerThickness");
    ui->spinMetroidCrosshairInnerThickness->setValue(innerThick > 0 ? innerThick : 1);
    ui->spinMetroidCrosshairInnerOffset->setValue(instcfg.GetInt("Metroid.Visual.CrosshairInnerOffset"));
    ui->cbMetroidCrosshairInnerLinkXY->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairInnerLinkXY"));

    // Crosshair — Outer Lines
    ui->cbMetroidCrosshairOuterShow->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairOuterShow"));
    ui->spinMetroidCrosshairOuterOpacity->setValue(instcfg.GetDouble("Metroid.Visual.CrosshairOuterOpacity"));
    ui->spinMetroidCrosshairOuterLengthX->setValue(instcfg.GetInt("Metroid.Visual.CrosshairOuterLengthX"));
    ui->spinMetroidCrosshairOuterLengthY->setValue(instcfg.GetInt("Metroid.Visual.CrosshairOuterLengthY"));
    int outerThick = instcfg.GetInt("Metroid.Visual.CrosshairOuterThickness");
    ui->spinMetroidCrosshairOuterThickness->setValue(outerThick > 0 ? outerThick : 1);
    ui->spinMetroidCrosshairOuterOffset->setValue(instcfg.GetInt("Metroid.Visual.CrosshairOuterOffset"));
    ui->cbMetroidCrosshairOuterLinkXY->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairOuterLinkXY"));

    // Color code <-> RGB sync
    connect(ui->spinMetroidCrosshairR, QOverload<int>::of(&QSpinBox::valueChanged), this, &MelonPrimeInputConfig::onCrosshairColorSpinChanged);
    connect(ui->spinMetroidCrosshairG, QOverload<int>::of(&QSpinBox::valueChanged), this, &MelonPrimeInputConfig::onCrosshairColorSpinChanged);
    connect(ui->spinMetroidCrosshairB, QOverload<int>::of(&QSpinBox::valueChanged), this, &MelonPrimeInputConfig::onCrosshairColorSpinChanged);

    // Link X/Y sync: when linked, changing X updates Y and vice versa
    connect(ui->spinMetroidCrosshairInnerLengthX, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (ui->cbMetroidCrosshairInnerLinkXY->isChecked()) {
            ui->spinMetroidCrosshairInnerLengthY->blockSignals(true);
            ui->spinMetroidCrosshairInnerLengthY->setValue(val);
            ui->spinMetroidCrosshairInnerLengthY->blockSignals(false);
        }
    });
    connect(ui->spinMetroidCrosshairInnerLengthY, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (ui->cbMetroidCrosshairInnerLinkXY->isChecked()) {
            ui->spinMetroidCrosshairInnerLengthX->blockSignals(true);
            ui->spinMetroidCrosshairInnerLengthX->setValue(val);
            ui->spinMetroidCrosshairInnerLengthX->blockSignals(false);
        }
    });
    connect(ui->spinMetroidCrosshairOuterLengthX, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (ui->cbMetroidCrosshairOuterLinkXY->isChecked()) {
            ui->spinMetroidCrosshairOuterLengthY->blockSignals(true);
            ui->spinMetroidCrosshairOuterLengthY->setValue(val);
            ui->spinMetroidCrosshairOuterLengthY->blockSignals(false);
        }
    });
    connect(ui->spinMetroidCrosshairOuterLengthY, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (ui->cbMetroidCrosshairOuterLinkXY->isChecked()) {
            ui->spinMetroidCrosshairOuterLengthX->blockSignals(true);
            ui->spinMetroidCrosshairOuterLengthX->setValue(val);
            ui->spinMetroidCrosshairOuterLengthX->blockSignals(false);
        }
    });

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
    instcfg.SetBool("Metroid.Aim.Disable.MphAimSmoothing", ui->cbMetroidDisableMphAimSmoothing->checkState() == Qt::Checked);
    instcfg.SetBool("Metroid.Aim.Enable.Accumulator", ui->cbMetroidEnableAimAccumulator->checkState() == Qt::Checked);

    // Screen Sync Mode
    instcfg.SetInt("Metroid.Screen.SyncMode", ui->comboMetroidScreenSyncMode->currentIndex());

    // Custom HUD
    instcfg.SetBool("Metroid.Visual.CustomHUD", ui->cbMetroidEnableCustomHud->checkState() == Qt::Checked);

    // Crosshair — Color
    instcfg.SetInt("Metroid.Visual.CrosshairColorR", ui->spinMetroidCrosshairR->value());

    // HUD element positions
    instcfg.SetInt("Metroid.Visual.HudHpX", ui->spinMetroidHudHpX->value());
    instcfg.SetInt("Metroid.Visual.HudHpY", ui->spinMetroidHudHpY->value());
    instcfg.SetInt("Metroid.Visual.HudWeaponX", ui->spinMetroidHudWeaponX->value());
    instcfg.SetInt("Metroid.Visual.HudWeaponY", ui->spinMetroidHudWeaponY->value());
    instcfg.SetInt("Metroid.Visual.HudWeaponLayout", ui->comboMetroidHudWeaponLayout->currentIndex());

    // Gauge settings — HP
    instcfg.SetBool("Metroid.Visual.HudHpGauge", ui->cbMetroidHudHpGauge->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudHpGaugeOrientation", ui->comboMetroidHudHpGaugeOrientation->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeLength", ui->spinMetroidHudHpGaugeLength->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeWidth", ui->spinMetroidHudHpGaugeWidth->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeOffsetX", ui->spinMetroidHudHpGaugeOffsetX->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeOffsetY", ui->spinMetroidHudHpGaugeOffsetY->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeAnchor", ui->comboMetroidHudHpGaugeAnchor->currentIndex());
    instcfg.SetBool("Metroid.Visual.HudHpGaugeAutoColor", ui->cbMetroidHudHpGaugeAutoColor->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudHpGaugeColorR", ui->spinMetroidHudHpGaugeColorR->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeColorG", ui->spinMetroidHudHpGaugeColorG->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeColorB", ui->spinMetroidHudHpGaugeColorB->value());

    // Gauge settings — Ammo
    instcfg.SetBool("Metroid.Visual.HudAmmoGauge", ui->cbMetroidHudAmmoGauge->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeOrientation", ui->comboMetroidHudAmmoGaugeOrientation->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeLength", ui->spinMetroidHudAmmoGaugeLength->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeWidth", ui->spinMetroidHudAmmoGaugeWidth->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeOffsetX", ui->spinMetroidHudAmmoGaugeOffsetX->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeOffsetY", ui->spinMetroidHudAmmoGaugeOffsetY->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeAnchor", ui->comboMetroidHudAmmoGaugeAnchor->currentIndex());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorR", ui->spinMetroidHudAmmoGaugeColorR->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorG", ui->spinMetroidHudAmmoGaugeColorG->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorB", ui->spinMetroidHudAmmoGaugeColorB->value());

    // Crosshair — Color (continued)
    instcfg.SetInt("Metroid.Visual.CrosshairColorG", ui->spinMetroidCrosshairG->value());
    instcfg.SetInt("Metroid.Visual.CrosshairColorB", ui->spinMetroidCrosshairB->value());

    // Crosshair — General
    instcfg.SetBool("Metroid.Visual.CrosshairOutline", ui->cbMetroidCrosshairOutline->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairOutlineOpacity", ui->spinMetroidCrosshairOutlineOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOutlineThickness", ui->spinMetroidCrosshairOutlineThickness->value());
    instcfg.SetBool("Metroid.Visual.CrosshairCenterDot", ui->cbMetroidCrosshairCenterDot->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairDotOpacity", ui->spinMetroidCrosshairDotOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairDotThickness", ui->spinMetroidCrosshairDotThickness->value());
    instcfg.SetBool("Metroid.Visual.CrosshairTStyle", ui->cbMetroidCrosshairTStyle->checkState() == Qt::Checked);

    // Crosshair — Inner Lines
    instcfg.SetBool("Metroid.Visual.CrosshairInnerShow", ui->cbMetroidCrosshairInnerShow->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairInnerOpacity", ui->spinMetroidCrosshairInnerOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerLengthX", ui->spinMetroidCrosshairInnerLengthX->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerLengthY", ui->spinMetroidCrosshairInnerLengthY->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerThickness", ui->spinMetroidCrosshairInnerThickness->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerOffset", ui->spinMetroidCrosshairInnerOffset->value());
    instcfg.SetBool("Metroid.Visual.CrosshairInnerLinkXY", ui->cbMetroidCrosshairInnerLinkXY->checkState() == Qt::Checked);

    // Crosshair — Outer Lines
    instcfg.SetBool("Metroid.Visual.CrosshairOuterShow", ui->cbMetroidCrosshairOuterShow->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairOuterOpacity", ui->spinMetroidCrosshairOuterOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOuterLengthX", ui->spinMetroidCrosshairOuterLengthX->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOuterLengthY", ui->spinMetroidCrosshairOuterLengthY->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOuterThickness", ui->spinMetroidCrosshairOuterThickness->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOuterOffset", ui->spinMetroidCrosshairOuterOffset->value());
    instcfg.SetBool("Metroid.Visual.CrosshairOuterLinkXY", ui->cbMetroidCrosshairOuterLinkXY->checkState() == Qt::Checked);

    
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

void MelonPrimeInputConfig::on_cbMetroidEnableCustomHud_stateChanged(int state)
{
    auto& cfg = emuInstance->getLocalConfig();
    cfg.SetBool("Metroid.Visual.CustomHUD", state != 0);
}

void MelonPrimeInputConfig::onCrosshairColorSpinChanged()
{
    int r = ui->spinMetroidCrosshairR->value();
    int g = ui->spinMetroidCrosshairG->value();
    int b = ui->spinMetroidCrosshairB->value();
    ui->leMetroidCrosshairColorCode->setText(
        QString("#%1%2%3")
            .arg(r, 2, 16, QChar('0'))
            .arg(g, 2, 16, QChar('0'))
            .arg(b, 2, 16, QChar('0')).toUpper());

    // Switch to Custom if user manually tweaked RGB
    ui->comboMetroidCrosshairColor->blockSignals(true);
    ui->comboMetroidCrosshairColor->setCurrentIndex(8);
    ui->comboMetroidCrosshairColor->blockSignals(false);
}

void MelonPrimeInputConfig::on_leMetroidCrosshairColorCode_editingFinished()
{
    QString text = ui->leMetroidCrosshairColorCode->text().trimmed();
    if (text.startsWith('#')) text = text.mid(1);
    if (text.length() != 6) return;

    bool ok;
    int val = text.toInt(&ok, 16);
    if (!ok) return;

    ui->spinMetroidCrosshairR->blockSignals(true);
    ui->spinMetroidCrosshairG->blockSignals(true);
    ui->spinMetroidCrosshairB->blockSignals(true);

    ui->spinMetroidCrosshairR->setValue((val >> 16) & 0xFF);
    ui->spinMetroidCrosshairG->setValue((val >> 8) & 0xFF);
    ui->spinMetroidCrosshairB->setValue(val & 0xFF);

    ui->spinMetroidCrosshairR->blockSignals(false);
    ui->spinMetroidCrosshairG->blockSignals(false);
    ui->spinMetroidCrosshairB->blockSignals(false);

    // Switch combo to Custom since user typed a code
    ui->comboMetroidCrosshairColor->blockSignals(true);
    ui->comboMetroidCrosshairColor->setCurrentIndex(8);
    ui->comboMetroidCrosshairColor->blockSignals(false);
}

void MelonPrimeInputConfig::on_comboMetroidCrosshairColor_currentIndexChanged(int index)
{
    // Preset colors matching Valorant's palette
    struct Preset { int r, g, b; };
    static const Preset presets[] = {
        {255,255,255}, // 0: White
        {0,255,0},     // 1: Green
        {127,255,0},   // 2: Yellow Green
        {191,255,0},   // 3: Green Yellow
        {255,255,0},   // 4: Yellow
        {0,255,255},   // 5: Cyan
        {255,105,180}, // 6: Pink
        {255,0,0},     // 7: Red
    };
    if (index < 0 || index >= 8) return; // Custom (8) = don't change

    ui->spinMetroidCrosshairR->blockSignals(true);
    ui->spinMetroidCrosshairG->blockSignals(true);
    ui->spinMetroidCrosshairB->blockSignals(true);

    ui->spinMetroidCrosshairR->setValue(presets[index].r);
    ui->spinMetroidCrosshairG->setValue(presets[index].g);
    ui->spinMetroidCrosshairB->setValue(presets[index].b);

    ui->spinMetroidCrosshairR->blockSignals(false);
    ui->spinMetroidCrosshairG->blockSignals(false);
    ui->spinMetroidCrosshairB->blockSignals(false);

    // Update hex code
    ui->leMetroidCrosshairColorCode->setText(
        QString("#%1%2%3")
            .arg(presets[index].r, 2, 16, QChar('0'))
            .arg(presets[index].g, 2, 16, QChar('0'))
            .arg(presets[index].b, 2, 16, QChar('0')).toUpper());
}
