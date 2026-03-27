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
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeCustomHud.h"
#endif

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
    ui->cbMetroidClipCursorToBottomScreenWhenNotInGame->setChecked(instcfg.GetBool("Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame"));

    // In-game scaling (mode 0=Auto, 1=5:3, 2=16:10, 3=16:9, 4=21:9)
    ui->cbMetroidInGameAspectRatio->setChecked(instcfg.GetBool("Metroid.Visual.InGameAspectRatio"));
    ui->comboMetroidInGameAspectRatioMode->setCurrentIndex(instcfg.GetInt("Metroid.Visual.InGameAspectRatioMode"));

    // Battle HUD
    ui->cbMetroidHudMatchStatusShow->setChecked(instcfg.GetBool("Metroid.Visual.HudMatchStatusShow"));
    ui->spinMetroidHudMatchStatusX->setValue(instcfg.GetInt("Metroid.Visual.HudMatchStatusX"));
    ui->spinMetroidHudMatchStatusY->setValue(instcfg.GetInt("Metroid.Visual.HudMatchStatusY"));
    ui->spinMetroidHudMatchStatusLabelOfsX->setValue(instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsX"));
    ui->spinMetroidHudMatchStatusLabelOfsY->setValue(instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsY"));
    ui->comboMetroidHudMatchStatusLabelPos->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelPos"));
    ui->leMetroidHudMatchStatusLabelPoints->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudMatchStatusLabelPoints")));
    ui->leMetroidHudMatchStatusLabelOctoliths->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudMatchStatusLabelOctoliths")));
    ui->leMetroidHudMatchStatusLabelLives->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudMatchStatusLabelLives")));
    ui->leMetroidHudMatchStatusLabelRingTime->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudMatchStatusLabelRingTime")));
    ui->leMetroidHudMatchStatusLabelPrimeTime->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudMatchStatusLabelPrimeTime")));

    // Match Status color
    ui->spinMetroidHudMatchStatusColorR->setValue(instcfg.GetInt("Metroid.Visual.HudMatchStatusColorR"));
    ui->spinMetroidHudMatchStatusColorG->setValue(instcfg.GetInt("Metroid.Visual.HudMatchStatusColorG"));
    ui->spinMetroidHudMatchStatusColorB->setValue(instcfg.GetInt("Metroid.Visual.HudMatchStatusColorB"));
    ui->leMetroidHudMatchStatusColorCode->setText(
        QString("#%1%2%3")
            .arg(ui->spinMetroidHudMatchStatusColorR->value(), 2, 16, QChar('0'))
            .arg(ui->spinMetroidHudMatchStatusColorG->value(), 2, 16, QChar('0'))
            .arg(ui->spinMetroidHudMatchStatusColorB->value(), 2, 16, QChar('0')).toUpper());
    // Match Status color preset detection
    {
        struct Clr { int r, g, b; };
        static const Clr presets[] = {
            {255,255,255}, {0,255,0}, {127,255,0}, {255,255,0},
            {0,200,255}, {255,105,180}, {255,0,0}, {56,192,8}, {248,248,88}, {120,240,64}, {40,152,80}, {248,176,24}, {200,80,40}, {248,40,40}, {80,152,208}, {40,104,152}, {208,152,56}, {248,224,128}, {76,0,252}, {88,224,40}
        };
        int r = ui->spinMetroidHudMatchStatusColorR->value();
        int g = ui->spinMetroidHudMatchStatusColorG->value();
        int b = ui->spinMetroidHudMatchStatusColorB->value();
        int idx = 21; // Custom
        for (int i = 0; i < 21; i++) {
            if (r == presets[i].r && g == presets[i].g && b == presets[i].b) { idx = i; break; }
        }
        ui->comboMetroidHudMatchStatusColor->setCurrentIndex(idx);
    }
    // Match Status color preset → RGB + hex
    connect(ui->comboMetroidHudMatchStatusColor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        struct Clr { int r, g, b; };
        static const Clr presets[] = {
            {255,255,255}, {0,255,0}, {127,255,0}, {255,255,0},
            {0,200,255}, {255,105,180}, {255,0,0}, {56,192,8}, {248,248,88}, {120,240,64}, {40,152,80}, {248,176,24}, {200,80,40}, {248,40,40}, {80,152,208}, {40,104,152}, {208,152,56}, {248,224,128}, {76,0,252}, {88,224,40}
        };
        if (idx < 0 || idx >= 19) return;
        ui->spinMetroidHudMatchStatusColorR->blockSignals(true);
        ui->spinMetroidHudMatchStatusColorG->blockSignals(true);
        ui->spinMetroidHudMatchStatusColorB->blockSignals(true);
        ui->spinMetroidHudMatchStatusColorR->setValue(presets[idx].r);
        ui->spinMetroidHudMatchStatusColorG->setValue(presets[idx].g);
        ui->spinMetroidHudMatchStatusColorB->setValue(presets[idx].b);
        ui->leMetroidHudMatchStatusColorCode->setText(
            QString("#%1%2%3").arg(presets[idx].r,2,16,QChar('0')).arg(presets[idx].g,2,16,QChar('0')).arg(presets[idx].b,2,16,QChar('0')).toUpper());
        ui->spinMetroidHudMatchStatusColorR->blockSignals(false);
        ui->spinMetroidHudMatchStatusColorG->blockSignals(false);
        ui->spinMetroidHudMatchStatusColorB->blockSignals(false);
    });
    // Match Status RGB spin → hex + Custom
    auto matchStatusRgbChanged = [this]() {
        ui->comboMetroidHudMatchStatusColor->blockSignals(true);
        ui->comboMetroidHudMatchStatusColor->setCurrentIndex(20);
        ui->comboMetroidHudMatchStatusColor->blockSignals(false);
        ui->leMetroidHudMatchStatusColorCode->setText(
            QString("#%1%2%3")
                .arg(ui->spinMetroidHudMatchStatusColorR->value(),2,16,QChar('0'))
                .arg(ui->spinMetroidHudMatchStatusColorG->value(),2,16,QChar('0'))
                .arg(ui->spinMetroidHudMatchStatusColorB->value(),2,16,QChar('0')).toUpper());
    };
    connect(ui->spinMetroidHudMatchStatusColorR, QOverload<int>::of(&QSpinBox::valueChanged), this, matchStatusRgbChanged);
    connect(ui->spinMetroidHudMatchStatusColorG, QOverload<int>::of(&QSpinBox::valueChanged), this, matchStatusRgbChanged);
    connect(ui->spinMetroidHudMatchStatusColorB, QOverload<int>::of(&QSpinBox::valueChanged), this, matchStatusRgbChanged);
    // Match Status hex → RGB + Custom
    connect(ui->leMetroidHudMatchStatusColorCode, &QLineEdit::editingFinished, this, [this]() {
        QColor c(ui->leMetroidHudMatchStatusColorCode->text());
        if (!c.isValid()) return;
        ui->spinMetroidHudMatchStatusColorR->blockSignals(true);
        ui->spinMetroidHudMatchStatusColorG->blockSignals(true);
        ui->spinMetroidHudMatchStatusColorB->blockSignals(true);
        ui->spinMetroidHudMatchStatusColorR->setValue(c.red());
        ui->spinMetroidHudMatchStatusColorG->setValue(c.green());
        ui->spinMetroidHudMatchStatusColorB->setValue(c.blue());
        ui->comboMetroidHudMatchStatusColor->blockSignals(true);
        ui->comboMetroidHudMatchStatusColor->setCurrentIndex(20);
        ui->comboMetroidHudMatchStatusColor->blockSignals(false);
        ui->spinMetroidHudMatchStatusColorR->blockSignals(false);
        ui->spinMetroidHudMatchStatusColorG->blockSignals(false);
        ui->spinMetroidHudMatchStatusColorB->blockSignals(false);
    });

    // Sub-color helper: sets up load, combo↔RGB sync, and enable/disable for one part
    // comboIdx 0 = "Overall" (useOverall=true); 1..20 = presets; 21 = Custom
    struct SubColorPreset { int r, g, b; };
    static const SubColorPreset kSubPresets[] = {
        {255,255,255}, {0,255,0}, {127,255,0}, {255,255,0},
        {0,200,255}, {255,105,180}, {255,0,0}, {56,192,8}, {248,248,88}, {120,240,64},
        {40,152,80}, {248,176,24}, {200,80,40}, {248,40,40}, {80,152,208}, {40,104,152},
        {208,152,56}, {248,224,128}, {76,0,252}, {88,224,40}
    };
    auto setupSubColor = [&](
        QComboBox* combo, QLineEdit* le,
        QSpinBox* spR, QSpinBox* spG, QSpinBox* spB,
        bool useOverall, int cfgR, int cfgG, int cfgB)
    {
        // Enable/disable helper
        auto setEnabled = [le, spR, spG, spB](bool en) {
            le->setEnabled(en); spR->setEnabled(en); spG->setEnabled(en); spB->setEnabled(en);
        };
        // Load initial values
        spR->setValue(cfgR); spG->setValue(cfgG); spB->setValue(cfgB);
        le->setText(QString("#%1%2%3")
            .arg(cfgR,2,16,QChar('0')).arg(cfgG,2,16,QChar('0')).arg(cfgB,2,16,QChar('0')).toUpper());
        // Detect preset index (offset +1 for "Overall" at 0)
        int idx = 20; // Custom
        if (useOverall) {
            idx = 0;
        } else {
            for (int i = 0; i < 20; i++) {
                if (cfgR == kSubPresets[i].r && cfgG == kSubPresets[i].g && cfgB == kSubPresets[i].b)
                    { idx = i + 1; break; }
            }
        }
        combo->setCurrentIndex(idx);
        setEnabled(idx != 0);

        // Combo → RGB + hex + enable/disable
        QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int i) {
            setEnabled(i != 0);
            if (i <= 0 || i > 20) return; // Overall or Custom: don't change RGB
            spR->blockSignals(true); spG->blockSignals(true); spB->blockSignals(true);
            spR->setValue(kSubPresets[i-1].r);
            spG->setValue(kSubPresets[i-1].g);
            spB->setValue(kSubPresets[i-1].b);
            le->setText(QString("#%1%2%3")
                .arg(kSubPresets[i-1].r,2,16,QChar('0'))
                .arg(kSubPresets[i-1].g,2,16,QChar('0'))
                .arg(kSubPresets[i-1].b,2,16,QChar('0')).toUpper());
            spR->blockSignals(false); spG->blockSignals(false); spB->blockSignals(false);
        });
        // RGB → hex + switch to Custom
        auto rgbChanged = [=]() {
            combo->blockSignals(true); combo->setCurrentIndex(21); combo->blockSignals(false);
            le->setText(QString("#%1%2%3")
                .arg(spR->value(),2,16,QChar('0'))
                .arg(spG->value(),2,16,QChar('0'))
                .arg(spB->value(),2,16,QChar('0')).toUpper());
        };
        QObject::connect(spR, QOverload<int>::of(&QSpinBox::valueChanged), this, rgbChanged);
        QObject::connect(spG, QOverload<int>::of(&QSpinBox::valueChanged), this, rgbChanged);
        QObject::connect(spB, QOverload<int>::of(&QSpinBox::valueChanged), this, rgbChanged);
        // Hex → RGB + switch to Custom
        QObject::connect(le, &QLineEdit::editingFinished, this, [=]() {
            QColor c(le->text());
            if (!c.isValid()) return;
            spR->blockSignals(true); spG->blockSignals(true); spB->blockSignals(true);
            spR->setValue(c.red()); spG->setValue(c.green()); spB->setValue(c.blue());
            combo->blockSignals(true); combo->setCurrentIndex(21); combo->blockSignals(false);
            spR->blockSignals(false); spG->blockSignals(false); spB->blockSignals(false);
        });
    };

    setupSubColor(
        ui->comboMetroidHudMatchStatusLabelColor,
        ui->leMetroidHudMatchStatusLabelColorCode,
        ui->spinMetroidHudMatchStatusLabelColorR,
        ui->spinMetroidHudMatchStatusLabelColorG,
        ui->spinMetroidHudMatchStatusLabelColorB,
        instcfg.GetBool("Metroid.Visual.HudMatchStatusLabelColorOverall"),
        instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelColorR"),
        instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelColorG"),
        instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelColorB"));
    setupSubColor(
        ui->comboMetroidHudMatchStatusValueColor,
        ui->leMetroidHudMatchStatusValueColorCode,
        ui->spinMetroidHudMatchStatusValueColorR,
        ui->spinMetroidHudMatchStatusValueColorG,
        ui->spinMetroidHudMatchStatusValueColorB,
        instcfg.GetBool("Metroid.Visual.HudMatchStatusValueColorOverall"),
        instcfg.GetInt("Metroid.Visual.HudMatchStatusValueColorR"),
        instcfg.GetInt("Metroid.Visual.HudMatchStatusValueColorG"),
        instcfg.GetInt("Metroid.Visual.HudMatchStatusValueColorB"));
    setupSubColor(
        ui->comboMetroidHudMatchStatusSepColor,
        ui->leMetroidHudMatchStatusSepColorCode,
        ui->spinMetroidHudMatchStatusSepColorR,
        ui->spinMetroidHudMatchStatusSepColorG,
        ui->spinMetroidHudMatchStatusSepColorB,
        instcfg.GetBool("Metroid.Visual.HudMatchStatusSepColorOverall"),
        instcfg.GetInt("Metroid.Visual.HudMatchStatusSepColorR"),
        instcfg.GetInt("Metroid.Visual.HudMatchStatusSepColorG"),
        instcfg.GetInt("Metroid.Visual.HudMatchStatusSepColorB"));
    setupSubColor(
        ui->comboMetroidHudMatchStatusGoalColor,
        ui->leMetroidHudMatchStatusGoalColorCode,
        ui->spinMetroidHudMatchStatusGoalColorR,
        ui->spinMetroidHudMatchStatusGoalColorG,
        ui->spinMetroidHudMatchStatusGoalColorB,
        instcfg.GetBool("Metroid.Visual.HudMatchStatusGoalColorOverall"),
        instcfg.GetInt("Metroid.Visual.HudMatchStatusGoalColorR"),
        instcfg.GetInt("Metroid.Visual.HudMatchStatusGoalColorG"),
        instcfg.GetInt("Metroid.Visual.HudMatchStatusGoalColorB"));

    // Reset buttons
    connect(ui->btnResetMatchStatusDefaults, &QPushButton::clicked, this, &MelonPrimeInputConfig::resetMatchStatusDefaults);

    // Custom HUD
    ui->cbMetroidEnableCustomHud->setChecked(instcfg.GetBool("Metroid.Visual.CustomHUD"));

    // --- Collapsible sections: remember expand/collapse state ---
    auto setupToggle = [&instcfg](QPushButton* btn, QWidget* section, const QString& label, const char* cfgKey) {
        bool expanded = instcfg.GetBool(cfgKey);
        section->setVisible(expanded);
        btn->setChecked(expanded);
        btn->setText((expanded ? QString::fromUtf8("▼ ") : QString::fromUtf8("▶ ")) + label);
        QObject::connect(btn, &QPushButton::toggled, [btn, section, label](bool checked) {
            section->setVisible(checked);
            btn->setText((checked ? QString::fromUtf8("▼ ") : QString::fromUtf8("▶ ")) + label);
        });
    };
    // Custom HUD tab
    setupToggle(ui->btnToggleCrosshair, ui->sectionCrosshair, "CROSSHAIR",      "Metroid.UI.SectionCrosshair");
    setupToggle(ui->btnToggleInner,     ui->sectionInner,     "INNER LINES",    "Metroid.UI.SectionInner");
    setupToggle(ui->btnToggleOuter,     ui->sectionOuter,     "OUTER LINES",    "Metroid.UI.SectionOuter");
    // HP & Ammo tab
    setupToggle(ui->btnToggleHpPos,     ui->sectionHpPos,     "HP NUMBER POSITION",    "Metroid.UI.SectionHpPos");
    setupToggle(ui->btnToggleWpnPos,    ui->sectionWpnPos,    "AMMO NUMBER POSITION",  "Metroid.UI.SectionWpnPos");
    setupToggle(ui->btnToggleWpnIcon,   ui->sectionWpnIcon,   "WEAPON ICON",    "Metroid.UI.SectionWpnIcon");
    setupToggle(ui->btnToggleHpGauge,   ui->sectionHpGauge,   "HP GAUGE",       "Metroid.UI.SectionHpGauge");
    setupToggle(ui->btnToggleAmmoGauge, ui->sectionAmmoGauge, "AMMO GAUGE",     "Metroid.UI.SectionAmmoGauge");
    // Other Metroid Settings 2 tab
    setupToggle(ui->btnToggleInputSettings, ui->sectionInputSettings, "INPUT SETTINGS",   "Metroid.UI.SectionInputSettings");
    setupToggle(ui->btnToggleScreenSync,    ui->sectionScreenSync,    "SCREEN SYNC",      "Metroid.UI.SectionScreenSync");
    setupToggle(ui->btnToggleCursorClipSettings, ui->sectionCursorClipSettings, "CURSOR CLIP SETTINGS",  "Metroid.UI.SectionCursorClipSettings");
    setupToggle(ui->btnToggleInGameAspectRatio, ui->sectionInGameAspectRatio, "IN-GAME ASPECT RATIO",  "Metroid.UI.SectionInGameAspectRatio");
    // Other Metroid Settings tab
    setupToggle(ui->btnToggleSensitivity, ui->sectionSensitivity, "SENSITIVITY",      "Metroid.UI.SectionSensitivity");
    setupToggle(ui->btnToggleGameplay,    ui->sectionGameplay,    "GAMEPLAY TOGGLES", "Metroid.UI.SectionGameplay");
    setupToggle(ui->btnToggleVideo,       ui->sectionVideo,       "VIDEO QUALITY",    "Metroid.UI.SectionVideo");
    setupToggle(ui->btnToggleVolume,      ui->sectionVolume,      "VOLUME",           "Metroid.UI.SectionVolume");
    setupToggle(ui->btnToggleLicense,     ui->sectionLicense,     "LICENSE APPLY",    "Metroid.UI.SectionLicense");

    // --- Reset buttons ---
    connect(ui->btnResetCrosshairDefaults, &QPushButton::clicked, this, &MelonPrimeInputConfig::resetCrosshairDefaults);
    connect(ui->btnResetHpAmmoDefaults,    &QPushButton::clicked, this, &MelonPrimeInputConfig::resetHpAmmoDefaults);

    // Crosshair — Color
    int chR = instcfg.GetInt("Metroid.Visual.CrosshairColorR");

    // HUD element positions
    ui->spinMetroidHudHpX->setValue(instcfg.GetInt("Metroid.Visual.HudHpX"));
    ui->spinMetroidHudHpY->setValue(instcfg.GetInt("Metroid.Visual.HudHpY"));
    ui->leMetroidHudHpPrefix->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudHpPrefix")));
    ui->comboMetroidHudHpAlign->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudHpAlign"));
    ui->cbMetroidHudHpTextAutoColor->setChecked(instcfg.GetBool("Metroid.Visual.HudHpTextAutoColor"));
    ui->spinMetroidHudHpTextColorR->setValue(instcfg.GetInt("Metroid.Visual.HudHpTextColorR"));
    ui->spinMetroidHudHpTextColorG->setValue(instcfg.GetInt("Metroid.Visual.HudHpTextColorG"));
    ui->spinMetroidHudHpTextColorB->setValue(instcfg.GetInt("Metroid.Visual.HudHpTextColorB"));
    ui->leMetroidHudHpTextColorCode->setText(
        QString("#%1%2%3")
            .arg(ui->spinMetroidHudHpTextColorR->value(), 2, 16, QChar('0'))
            .arg(ui->spinMetroidHudHpTextColorG->value(), 2, 16, QChar('0'))
            .arg(ui->spinMetroidHudHpTextColorB->value(), 2, 16, QChar('0')).toUpper());
    ui->spinMetroidHudWeaponX->setValue(instcfg.GetInt("Metroid.Visual.HudWeaponX"));
    ui->spinMetroidHudWeaponY->setValue(instcfg.GetInt("Metroid.Visual.HudWeaponY"));
    ui->leMetroidHudAmmoPrefix->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudAmmoPrefix")));
    ui->comboMetroidHudAmmoAlign->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudAmmoAlign"));
    ui->spinMetroidHudAmmoTextColorR->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoTextColorR"));
    ui->spinMetroidHudAmmoTextColorG->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoTextColorG"));
    ui->spinMetroidHudAmmoTextColorB->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoTextColorB"));
    ui->leMetroidHudAmmoTextColorCode->setText(
        QString("#%1%2%3")
            .arg(ui->spinMetroidHudAmmoTextColorR->value(), 2, 16, QChar('0'))
            .arg(ui->spinMetroidHudAmmoTextColorG->value(), 2, 16, QChar('0'))
            .arg(ui->spinMetroidHudAmmoTextColorB->value(), 2, 16, QChar('0')).toUpper());
    ui->cbMetroidHudWeaponIconShow->setChecked(instcfg.GetBool("Metroid.Visual.HudWeaponIconShow"));
    ui->comboMetroidHudWeaponIconMode->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudWeaponIconMode"));
    ui->spinMetroidHudWeaponIconOffsetX->setValue(instcfg.GetInt("Metroid.Visual.HudWeaponIconOffsetX"));
    ui->spinMetroidHudWeaponIconOffsetY->setValue(instcfg.GetInt("Metroid.Visual.HudWeaponIconOffsetY"));
    ui->spinMetroidHudWeaponIconPosX->setValue(instcfg.GetInt("Metroid.Visual.HudWeaponIconPosX"));
    ui->spinMetroidHudWeaponIconPosY->setValue(instcfg.GetInt("Metroid.Visual.HudWeaponIconPosY"));
    ui->comboMetroidHudWeaponIconAnchorX->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudWeaponIconAnchorX"));
    ui->comboMetroidHudWeaponIconAnchorY->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudWeaponIconAnchorY"));
    ui->cbMetroidHudWeaponIconColorOverlay->setChecked(instcfg.GetBool("Metroid.Visual.HudWeaponIconColorOverlay"));

    struct HudTextClr { int r, g, b; };
    static const HudTextClr kHudTextColorPresets[] = {
        {255,255,255}, {0,255,0}, {127,255,0}, {255,255,0},
        {0,200,255}, {255,105,180}, {255,0,0}, {56,192,8}, {248,248,88}, {120,240,64},
        {40,152,80}, {248,176,24}, {200,80,40}, {248,40,40}, {80,152,208}, {40,104,152},
        {208,152,56}, {248,224,128}, {76,0,252}, {88,224,40}
    };
    auto setupTextColorPreset = [this](QComboBox* combo, QLineEdit* lineEdit,
                                       QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB) {
        int idx = 20; // Custom
        const int r = spinR->value();
        const int g = spinG->value();
        const int b = spinB->value();
        for (int i = 0; i < 20; i++) {
            if (r == kHudTextColorPresets[i].r && g == kHudTextColorPresets[i].g && b == kHudTextColorPresets[i].b) {
                idx = i;
                break;
            }
        }
        combo->setCurrentIndex(idx);

        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int presetIdx) {
            if (presetIdx < 0 || presetIdx >= 20) return;
            spinR->blockSignals(true);
            spinG->blockSignals(true);
            spinB->blockSignals(true);
            spinR->setValue(kHudTextColorPresets[presetIdx].r);
            spinG->setValue(kHudTextColorPresets[presetIdx].g);
            spinB->setValue(kHudTextColorPresets[presetIdx].b);
            lineEdit->setText(QString("#%1%2%3")
                .arg(kHudTextColorPresets[presetIdx].r,2,16,QChar('0'))
                .arg(kHudTextColorPresets[presetIdx].g,2,16,QChar('0'))
                .arg(kHudTextColorPresets[presetIdx].b,2,16,QChar('0')).toUpper());
            spinR->blockSignals(false);
            spinG->blockSignals(false);
            spinB->blockSignals(false);
        });

        auto rgbChanged = [=]() {
            combo->blockSignals(true);
            combo->setCurrentIndex(20);
            combo->blockSignals(false);
            lineEdit->setText(QString("#%1%2%3")
                .arg(spinR->value(),2,16,QChar('0'))
                .arg(spinG->value(),2,16,QChar('0'))
                .arg(spinB->value(),2,16,QChar('0')).toUpper());
        };
        connect(spinR, QOverload<int>::of(&QSpinBox::valueChanged), this, rgbChanged);
        connect(spinG, QOverload<int>::of(&QSpinBox::valueChanged), this, rgbChanged);
        connect(spinB, QOverload<int>::of(&QSpinBox::valueChanged), this, rgbChanged);

        connect(lineEdit, &QLineEdit::editingFinished, this, [=]() {
            QColor c(lineEdit->text());
            if (!c.isValid()) return;
            spinR->blockSignals(true);
            spinG->blockSignals(true);
            spinB->blockSignals(true);
            spinR->setValue(c.red());
            spinG->setValue(c.green());
            spinB->setValue(c.blue());
            combo->blockSignals(true);
            combo->setCurrentIndex(20);
            combo->blockSignals(false);
            spinR->blockSignals(false);
            spinG->blockSignals(false);
            spinB->blockSignals(false);
        });
    };

    setupTextColorPreset(ui->comboMetroidHudHpTextColor,
                         ui->leMetroidHudHpTextColorCode,
                         ui->spinMetroidHudHpTextColorR,
                         ui->spinMetroidHudHpTextColorG,
                         ui->spinMetroidHudHpTextColorB);
    setupTextColorPreset(ui->comboMetroidHudAmmoTextColor,
                         ui->leMetroidHudAmmoTextColorCode,
                         ui->spinMetroidHudAmmoTextColorR,
                         ui->spinMetroidHudAmmoTextColorG,
                         ui->spinMetroidHudAmmoTextColorB);

    // Icon independent position preset detection
    {
        struct Pos { int x, y; };
        static const Pos presets[] = {
            {4,2}, {120,2}, {226,2}, {226,82}, {226,174}, {120,174}, {4,174}, {4,82}
        };
        int ix = ui->spinMetroidHudWeaponIconPosX->value();
        int iy = ui->spinMetroidHudWeaponIconPosY->value();
        int idx = 8;
        for (int i = 0; i < 8; i++) {
            if (ix == presets[i].x && iy == presets[i].y) { idx = i; break; }
        }
        ui->comboMetroidHudWeaponIconPosition->setCurrentIndex(idx);
    }
    connect(ui->comboMetroidHudWeaponIconPosition, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        struct Pos { int x, y; };
        static const Pos presets[] = {
            {4,2}, {120,2}, {226,2}, {226,82}, {226,174}, {120,174}, {4,174}, {4,82}
        };
        if (idx < 0 || idx >= 19) return;
        ui->spinMetroidHudWeaponIconPosX->blockSignals(true);
        ui->spinMetroidHudWeaponIconPosY->blockSignals(true);
        ui->spinMetroidHudWeaponIconPosX->setValue(presets[idx].x);
        ui->spinMetroidHudWeaponIconPosY->setValue(presets[idx].y);
        ui->spinMetroidHudWeaponIconPosX->blockSignals(false);
        ui->spinMetroidHudWeaponIconPosY->blockSignals(false);
    });
    connect(ui->spinMetroidHudWeaponIconPosX, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        ui->comboMetroidHudWeaponIconPosition->blockSignals(true);
        ui->comboMetroidHudWeaponIconPosition->setCurrentIndex(8);
        ui->comboMetroidHudWeaponIconPosition->blockSignals(false);
    });
    connect(ui->spinMetroidHudWeaponIconPosY, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() {
        ui->comboMetroidHudWeaponIconPosition->blockSignals(true);
        ui->comboMetroidHudWeaponIconPosition->setCurrentIndex(8);
        ui->comboMetroidHudWeaponIconPosition->blockSignals(false);
    });

    // Gauge settings — HP
    ui->cbMetroidHudHpGauge->setChecked(instcfg.GetBool("Metroid.Visual.HudHpGauge"));
    ui->comboMetroidHudHpGaugeOrientation->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudHpGaugeOrientation"));
    ui->spinMetroidHudHpGaugeLength->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeLength"));
    ui->spinMetroidHudHpGaugeWidth->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeWidth"));
    ui->spinMetroidHudHpGaugeOffsetX->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeOffsetX"));
    ui->spinMetroidHudHpGaugeOffsetY->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeOffsetY"));
    ui->comboMetroidHudHpGaugeAnchor->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudHpGaugeAnchor"));
    ui->comboMetroidHudHpGaugePosMode->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudHpGaugePosMode"));
    ui->spinMetroidHudHpGaugePosX->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugePosX"));
    ui->spinMetroidHudHpGaugePosY->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugePosY"));
    ui->cbMetroidHudHpGaugeAutoColor->setChecked(instcfg.GetBool("Metroid.Visual.HudHpGaugeAutoColor"));
    ui->spinMetroidHudHpGaugeColorR->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeColorR"));
    ui->spinMetroidHudHpGaugeColorG->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeColorG"));
    ui->spinMetroidHudHpGaugeColorB->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeColorB"));
    ui->leMetroidHudHpGaugeColorCode->setText(
        QString("#%1%2%3")
            .arg(ui->spinMetroidHudHpGaugeColorR->value(), 2, 16, QChar('0'))
            .arg(ui->spinMetroidHudHpGaugeColorG->value(), 2, 16, QChar('0'))
            .arg(ui->spinMetroidHudHpGaugeColorB->value(), 2, 16, QChar('0')).toUpper());

    // HP gauge color preset detection
    {
        struct Clr { int r, g, b; };
        static const Clr presets[] = {
            {0,255,0}, {255,0,0}, {255,165,0}, {255,255,0}, {0,200,255}, {255,255,255}, {56,192,8}, {248,248,88}, {120,240,64}, {40,152,80}, {248,176,24}, {200,80,40}, {248,40,40}, {80,152,208}, {40,104,152}, {208,152,56}, {248,224,128}, {76,0,252}, {88,224,40}
        };
        int r = ui->spinMetroidHudHpGaugeColorR->value();
        int g = ui->spinMetroidHudHpGaugeColorG->value();
        int b = ui->spinMetroidHudHpGaugeColorB->value();
        int idx = 19; // Custom
        for (int i = 0; i < 19; i++) {
            if (r == presets[i].r && g == presets[i].g && b == presets[i].b) { idx = i; break; }
        }
        ui->comboMetroidHudHpGaugeColor->setCurrentIndex(idx);
    }
    // HP gauge color preset → RGB + hex
    connect(ui->comboMetroidHudHpGaugeColor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        struct Clr { int r, g, b; };
        static const Clr presets[] = {
            {0,255,0}, {255,0,0}, {255,165,0}, {255,255,0}, {0,200,255}, {255,255,255}, {56,192,8}, {248,248,88}, {120,240,64}, {40,152,80}, {248,176,24}, {200,80,40}, {248,40,40}, {80,152,208}, {40,104,152}, {208,152,56}, {248,224,128}, {76,0,252}, {88,224,40}
        };
        if (idx < 0 || idx >= 19) return;
        ui->spinMetroidHudHpGaugeColorR->blockSignals(true);
        ui->spinMetroidHudHpGaugeColorG->blockSignals(true);
        ui->spinMetroidHudHpGaugeColorB->blockSignals(true);
        ui->spinMetroidHudHpGaugeColorR->setValue(presets[idx].r);
        ui->spinMetroidHudHpGaugeColorG->setValue(presets[idx].g);
        ui->spinMetroidHudHpGaugeColorB->setValue(presets[idx].b);
        ui->leMetroidHudHpGaugeColorCode->setText(
            QString("#%1%2%3").arg(presets[idx].r,2,16,QChar('0')).arg(presets[idx].g,2,16,QChar('0')).arg(presets[idx].b,2,16,QChar('0')).toUpper());
        ui->spinMetroidHudHpGaugeColorR->blockSignals(false);
        ui->spinMetroidHudHpGaugeColorG->blockSignals(false);
        ui->spinMetroidHudHpGaugeColorB->blockSignals(false);
    });
    // HP gauge RGB spin → hex + Custom
    auto hpGaugeRgbChanged = [this]() {
        ui->comboMetroidHudHpGaugeColor->blockSignals(true);
        ui->comboMetroidHudHpGaugeColor->setCurrentIndex(19);
        ui->comboMetroidHudHpGaugeColor->blockSignals(false);
        ui->leMetroidHudHpGaugeColorCode->setText(
            QString("#%1%2%3")
                .arg(ui->spinMetroidHudHpGaugeColorR->value(),2,16,QChar('0'))
                .arg(ui->spinMetroidHudHpGaugeColorG->value(),2,16,QChar('0'))
                .arg(ui->spinMetroidHudHpGaugeColorB->value(),2,16,QChar('0')).toUpper());
    };
    connect(ui->spinMetroidHudHpGaugeColorR, QOverload<int>::of(&QSpinBox::valueChanged), this, hpGaugeRgbChanged);
    connect(ui->spinMetroidHudHpGaugeColorG, QOverload<int>::of(&QSpinBox::valueChanged), this, hpGaugeRgbChanged);
    connect(ui->spinMetroidHudHpGaugeColorB, QOverload<int>::of(&QSpinBox::valueChanged), this, hpGaugeRgbChanged);
    // HP gauge hex → RGB + Custom
    connect(ui->leMetroidHudHpGaugeColorCode, &QLineEdit::editingFinished, this, [this]() {
        QColor c(ui->leMetroidHudHpGaugeColorCode->text());
        if (!c.isValid()) return;
        ui->spinMetroidHudHpGaugeColorR->blockSignals(true);
        ui->spinMetroidHudHpGaugeColorG->blockSignals(true);
        ui->spinMetroidHudHpGaugeColorB->blockSignals(true);
        ui->spinMetroidHudHpGaugeColorR->setValue(c.red());
        ui->spinMetroidHudHpGaugeColorG->setValue(c.green());
        ui->spinMetroidHudHpGaugeColorB->setValue(c.blue());
        ui->comboMetroidHudHpGaugeColor->blockSignals(true);
        ui->comboMetroidHudHpGaugeColor->setCurrentIndex(19);
        ui->comboMetroidHudHpGaugeColor->blockSignals(false);
        ui->spinMetroidHudHpGaugeColorR->blockSignals(false);
        ui->spinMetroidHudHpGaugeColorG->blockSignals(false);
        ui->spinMetroidHudHpGaugeColorB->blockSignals(false);
    });

    // Gauge settings — Ammo
    ui->cbMetroidHudAmmoGauge->setChecked(instcfg.GetBool("Metroid.Visual.HudAmmoGauge"));
    ui->comboMetroidHudAmmoGaugeOrientation->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOrientation"));
    ui->spinMetroidHudAmmoGaugeLength->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeLength"));
    ui->spinMetroidHudAmmoGaugeWidth->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeWidth"));
    ui->spinMetroidHudAmmoGaugeOffsetX->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetX"));
    ui->spinMetroidHudAmmoGaugeOffsetY->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetY"));
    ui->comboMetroidHudAmmoGaugeAnchor->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeAnchor"));
    ui->comboMetroidHudAmmoGaugePosMode->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudAmmoGaugePosMode"));
    ui->spinMetroidHudAmmoGaugePosX->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugePosX"));
    ui->spinMetroidHudAmmoGaugePosY->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugePosY"));
    ui->spinMetroidHudAmmoGaugeColorR->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeColorR"));
    ui->spinMetroidHudAmmoGaugeColorG->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeColorG"));
    ui->spinMetroidHudAmmoGaugeColorB->setValue(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeColorB"));
    ui->leMetroidHudAmmoGaugeColorCode->setText(
        QString("#%1%2%3")
            .arg(ui->spinMetroidHudAmmoGaugeColorR->value(), 2, 16, QChar('0'))
            .arg(ui->spinMetroidHudAmmoGaugeColorG->value(), 2, 16, QChar('0'))
            .arg(ui->spinMetroidHudAmmoGaugeColorB->value(), 2, 16, QChar('0')).toUpper());

    // Ammo gauge color preset detection (Cyan first in list)
    {
        struct Clr { int r, g, b; };
        static const Clr presets[] = {
            {0,200,255}, {0,255,0}, {255,0,0}, {255,165,0}, {255,255,0}, {255,255,255}, {56,192,8}, {248,248,88}, {120,240,64}, {40,152,80}, {248,176,24}, {200,80,40}, {248,40,40}, {80,152,208}, {40,104,152}, {208,152,56}, {248,224,128}, {76,0,252}, {88,224,40}
        };
        int r = ui->spinMetroidHudAmmoGaugeColorR->value();
        int g = ui->spinMetroidHudAmmoGaugeColorG->value();
        int b = ui->spinMetroidHudAmmoGaugeColorB->value();
        int idx = 19; // Custom
        for (int i = 0; i < 19; i++) {
            if (r == presets[i].r && g == presets[i].g && b == presets[i].b) { idx = i; break; }
        }
        ui->comboMetroidHudAmmoGaugeColor->setCurrentIndex(idx);
    }
    // Ammo gauge color preset → RGB + hex
    connect(ui->comboMetroidHudAmmoGaugeColor, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        struct Clr { int r, g, b; };
        static const Clr presets[] = {
            {0,200,255}, {0,255,0}, {255,0,0}, {255,165,0}, {255,255,0}, {255,255,255}, {56,192,8}, {248,248,88}, {120,240,64}, {40,152,80}, {248,176,24}, {200,80,40}, {248,40,40}, {80,152,208}, {40,104,152}, {208,152,56}, {248,224,128}, {76,0,252}, {88,224,40}
        };
        if (idx < 0 || idx >= 19) return;
        ui->spinMetroidHudAmmoGaugeColorR->blockSignals(true);
        ui->spinMetroidHudAmmoGaugeColorG->blockSignals(true);
        ui->spinMetroidHudAmmoGaugeColorB->blockSignals(true);
        ui->spinMetroidHudAmmoGaugeColorR->setValue(presets[idx].r);
        ui->spinMetroidHudAmmoGaugeColorG->setValue(presets[idx].g);
        ui->spinMetroidHudAmmoGaugeColorB->setValue(presets[idx].b);
        ui->leMetroidHudAmmoGaugeColorCode->setText(
            QString("#%1%2%3").arg(presets[idx].r,2,16,QChar('0')).arg(presets[idx].g,2,16,QChar('0')).arg(presets[idx].b,2,16,QChar('0')).toUpper());
        ui->spinMetroidHudAmmoGaugeColorR->blockSignals(false);
        ui->spinMetroidHudAmmoGaugeColorG->blockSignals(false);
        ui->spinMetroidHudAmmoGaugeColorB->blockSignals(false);
    });
    // Ammo gauge RGB spin → hex + Custom
    auto ammoGaugeRgbChanged = [this]() {
        ui->comboMetroidHudAmmoGaugeColor->blockSignals(true);
        ui->comboMetroidHudAmmoGaugeColor->setCurrentIndex(19);
        ui->comboMetroidHudAmmoGaugeColor->blockSignals(false);
        ui->leMetroidHudAmmoGaugeColorCode->setText(
            QString("#%1%2%3")
                .arg(ui->spinMetroidHudAmmoGaugeColorR->value(),2,16,QChar('0'))
                .arg(ui->spinMetroidHudAmmoGaugeColorG->value(),2,16,QChar('0'))
                .arg(ui->spinMetroidHudAmmoGaugeColorB->value(),2,16,QChar('0')).toUpper());
    };
    connect(ui->spinMetroidHudAmmoGaugeColorR, QOverload<int>::of(&QSpinBox::valueChanged), this, ammoGaugeRgbChanged);
    connect(ui->spinMetroidHudAmmoGaugeColorG, QOverload<int>::of(&QSpinBox::valueChanged), this, ammoGaugeRgbChanged);
    connect(ui->spinMetroidHudAmmoGaugeColorB, QOverload<int>::of(&QSpinBox::valueChanged), this, ammoGaugeRgbChanged);
    // Ammo gauge hex → RGB + Custom
    connect(ui->leMetroidHudAmmoGaugeColorCode, &QLineEdit::editingFinished, this, [this]() {
        QColor c(ui->leMetroidHudAmmoGaugeColorCode->text());
        if (!c.isValid()) return;
        ui->spinMetroidHudAmmoGaugeColorR->blockSignals(true);
        ui->spinMetroidHudAmmoGaugeColorG->blockSignals(true);
        ui->spinMetroidHudAmmoGaugeColorB->blockSignals(true);
        ui->spinMetroidHudAmmoGaugeColorR->setValue(c.red());
        ui->spinMetroidHudAmmoGaugeColorG->setValue(c.green());
        ui->spinMetroidHudAmmoGaugeColorB->setValue(c.blue());
        ui->comboMetroidHudAmmoGaugeColor->blockSignals(true);
        ui->comboMetroidHudAmmoGaugeColor->setCurrentIndex(19);
        ui->comboMetroidHudAmmoGaugeColor->blockSignals(false);
        ui->spinMetroidHudAmmoGaugeColorR->blockSignals(false);
        ui->spinMetroidHudAmmoGaugeColorG->blockSignals(false);
        ui->spinMetroidHudAmmoGaugeColorB->blockSignals(false);
    });

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

    auto updateWeaponIconModeUi = [this]() {
        const bool independent = (ui->comboMetroidHudWeaponIconMode->currentIndex() == 1);
        ui->spinMetroidHudWeaponIconOffsetX->setEnabled(!independent);
        ui->spinMetroidHudWeaponIconOffsetY->setEnabled(!independent);
        ui->comboMetroidHudWeaponIconPosition->setEnabled(independent);
        ui->spinMetroidHudWeaponIconPosX->setEnabled(independent);
        ui->spinMetroidHudWeaponIconPosY->setEnabled(independent);
        ui->comboMetroidHudWeaponIconAnchorX->setEnabled(independent);
        ui->comboMetroidHudWeaponIconAnchorY->setEnabled(independent);
    };
    connect(ui->comboMetroidHudWeaponIconMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [updateWeaponIconModeUi](int) {
        updateWeaponIconModeUi();
    });
    updateWeaponIconModeUi();
    auto updateHpGaugeModeUi = [this]() {
        const bool independent = (ui->comboMetroidHudHpGaugePosMode->currentIndex() == 1);
        ui->spinMetroidHudHpGaugeOffsetX->setEnabled(!independent);
        ui->spinMetroidHudHpGaugeOffsetY->setEnabled(!independent);
        ui->comboMetroidHudHpGaugeAnchor->setEnabled(!independent);
        ui->spinMetroidHudHpGaugePosX->setEnabled(independent);
        ui->spinMetroidHudHpGaugePosY->setEnabled(independent);
    };
    connect(ui->comboMetroidHudHpGaugePosMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [updateHpGaugeModeUi](int) {
        updateHpGaugeModeUi();
    });
    updateHpGaugeModeUi();

    auto updateAmmoGaugeModeUi = [this]() {
        const bool independent = (ui->comboMetroidHudAmmoGaugePosMode->currentIndex() == 1);
        ui->spinMetroidHudAmmoGaugeOffsetX->setEnabled(!independent);
        ui->spinMetroidHudAmmoGaugeOffsetY->setEnabled(!independent);
        ui->comboMetroidHudAmmoGaugeAnchor->setEnabled(!independent);
        ui->spinMetroidHudAmmoGaugePosX->setEnabled(independent);
        ui->spinMetroidHudAmmoGaugePosY->setEnabled(independent);
    };
    connect(ui->comboMetroidHudAmmoGaugePosMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [updateAmmoGaugeModeUi](int) {
        updateAmmoGaugeModeUi();
    });
    updateAmmoGaugeModeUi();

    // HP position preset → update X/Y
    connect(ui->comboMetroidHudHpPosition, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        struct Pos { int x, y; };
        static const Pos presets[] = {
            {4,8}, {120,8}, {220,8}, {220,96}, {220,188}, {120,188}, {4,188}, {4,96}
        };
        if (idx < 0 || idx >= 9) return;
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
        if (idx < 0 || idx >= 9) return;
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

    // Detect color preset (match against known presets, else Custom)
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
            {56,192,8},    // 8: Sylux Hud Color
            {248,248,88},  // 9: Kanden HUD Color
            {120,240,64},  // 10: Samus HUD
            {40,152,80},   // 11: Samus HUD Outline
            {248,176,24},  // 12: Spire HUD
            {200,80,40},   // 13: Spire HUD Outline
            {248,40,40},   // 14: Trace HUD
            {80,152,208},  // 15: Noxus HUD
            {40,104,152},  // 16: Noxus HUD Outline
            {208,152,56},  // 17: Weavel HUD
            {248,224,128}, // 18: Weavel HUD Outline
            {76,0,252},    // 19: Avium Purple
            {88,224,40},   // 20: Sylux Crosshair Color
        };
        int presetIdx = 21; // Custom
        for (int i = 0; i < 20; i++) {
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

    // --- Live visual preview ---
    snapshotVisualConfig();

    auto prvI = [&](QSpinBox* w) {
        connect(w, QOverload<int>::of(&QSpinBox::valueChanged), this, &MelonPrimeInputConfig::applyVisualPreview);
    };
    auto prvD = [&](QDoubleSpinBox* w) {
        connect(w, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MelonPrimeInputConfig::applyVisualPreview);
    };
    auto prvB = [&](QCheckBox* w) {
        connect(w, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) { applyVisualPreview(); });
    };
    auto prvC = [&](QComboBox* w) {
        connect(w, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyVisualPreview(); });
    };
    auto prvE = [&](QLineEdit* w) {
        connect(w, &QLineEdit::textChanged, this, [this](const QString&) { applyVisualPreview(); });
    };

    prvB(ui->cbMetroidEnableCustomHud);
    prvB(ui->cbMetroidInGameAspectRatio);
    prvC(ui->comboMetroidInGameAspectRatioMode);
    // Match Status
    prvB(ui->cbMetroidHudMatchStatusShow);
    prvI(ui->spinMetroidHudMatchStatusX);         prvI(ui->spinMetroidHudMatchStatusY);
    prvI(ui->spinMetroidHudMatchStatusLabelOfsX); prvI(ui->spinMetroidHudMatchStatusLabelOfsY);
    prvC(ui->comboMetroidHudMatchStatusLabelPos);
    prvE(ui->leMetroidHudMatchStatusLabelPoints);    prvE(ui->leMetroidHudMatchStatusLabelOctoliths);
    prvE(ui->leMetroidHudMatchStatusLabelLives);     prvE(ui->leMetroidHudMatchStatusLabelRingTime);
    prvE(ui->leMetroidHudMatchStatusLabelPrimeTime);
    prvC(ui->comboMetroidHudMatchStatusColor);
    prvI(ui->spinMetroidHudMatchStatusColorR);    prvI(ui->spinMetroidHudMatchStatusColorG);    prvI(ui->spinMetroidHudMatchStatusColorB);
    prvC(ui->comboMetroidHudMatchStatusLabelColor);
    prvI(ui->spinMetroidHudMatchStatusLabelColorR); prvI(ui->spinMetroidHudMatchStatusLabelColorG); prvI(ui->spinMetroidHudMatchStatusLabelColorB);
    prvC(ui->comboMetroidHudMatchStatusValueColor);
    prvI(ui->spinMetroidHudMatchStatusValueColorR); prvI(ui->spinMetroidHudMatchStatusValueColorG); prvI(ui->spinMetroidHudMatchStatusValueColorB);
    prvC(ui->comboMetroidHudMatchStatusSepColor);
    prvI(ui->spinMetroidHudMatchStatusSepColorR);   prvI(ui->spinMetroidHudMatchStatusSepColorG);   prvI(ui->spinMetroidHudMatchStatusSepColorB);
    prvC(ui->comboMetroidHudMatchStatusGoalColor);
    prvI(ui->spinMetroidHudMatchStatusGoalColorR);  prvI(ui->spinMetroidHudMatchStatusGoalColorG);  prvI(ui->spinMetroidHudMatchStatusGoalColorB);
    // HP/Weapon positions
    prvI(ui->spinMetroidHudHpX);    prvI(ui->spinMetroidHudHpY);    prvE(ui->leMetroidHudHpPrefix);    prvC(ui->comboMetroidHudHpAlign);
    prvB(ui->cbMetroidHudHpTextAutoColor); prvC(ui->comboMetroidHudHpTextColor);
    prvI(ui->spinMetroidHudHpTextColorR); prvI(ui->spinMetroidHudHpTextColorG); prvI(ui->spinMetroidHudHpTextColorB);
    prvI(ui->spinMetroidHudWeaponX); prvI(ui->spinMetroidHudWeaponY); prvE(ui->leMetroidHudAmmoPrefix); prvC(ui->comboMetroidHudAmmoAlign);
    prvC(ui->comboMetroidHudAmmoTextColor);
    prvI(ui->spinMetroidHudAmmoTextColorR); prvI(ui->spinMetroidHudAmmoTextColorG); prvI(ui->spinMetroidHudAmmoTextColorB);
    prvB(ui->cbMetroidHudWeaponIconShow);  prvC(ui->comboMetroidHudWeaponIconMode);
    prvI(ui->spinMetroidHudWeaponIconOffsetX); prvI(ui->spinMetroidHudWeaponIconOffsetY);
    prvI(ui->spinMetroidHudWeaponIconPosX);   prvI(ui->spinMetroidHudWeaponIconPosY);
    prvC(ui->comboMetroidHudWeaponIconAnchorX); prvC(ui->comboMetroidHudWeaponIconAnchorY); prvB(ui->cbMetroidHudWeaponIconColorOverlay);
    // HP Gauge
    prvB(ui->cbMetroidHudHpGauge);
    prvC(ui->comboMetroidHudHpGaugeOrientation);
    prvI(ui->spinMetroidHudHpGaugeLength); prvI(ui->spinMetroidHudHpGaugeWidth);
    prvI(ui->spinMetroidHudHpGaugeOffsetX); prvI(ui->spinMetroidHudHpGaugeOffsetY);
    prvC(ui->comboMetroidHudHpGaugeAnchor); prvC(ui->comboMetroidHudHpGaugePosMode);
    prvI(ui->spinMetroidHudHpGaugePosX);   prvI(ui->spinMetroidHudHpGaugePosY);
    prvB(ui->cbMetroidHudHpGaugeAutoColor);
    prvI(ui->spinMetroidHudHpGaugeColorR); prvI(ui->spinMetroidHudHpGaugeColorG); prvI(ui->spinMetroidHudHpGaugeColorB);
    // Ammo Gauge
    prvB(ui->cbMetroidHudAmmoGauge);
    prvC(ui->comboMetroidHudAmmoGaugeOrientation);
    prvI(ui->spinMetroidHudAmmoGaugeLength); prvI(ui->spinMetroidHudAmmoGaugeWidth);
    prvI(ui->spinMetroidHudAmmoGaugeOffsetX); prvI(ui->spinMetroidHudAmmoGaugeOffsetY);
    prvC(ui->comboMetroidHudAmmoGaugeAnchor); prvC(ui->comboMetroidHudAmmoGaugePosMode);
    prvI(ui->spinMetroidHudAmmoGaugePosX);   prvI(ui->spinMetroidHudAmmoGaugePosY);
    prvI(ui->spinMetroidHudAmmoGaugeColorR); prvI(ui->spinMetroidHudAmmoGaugeColorG); prvI(ui->spinMetroidHudAmmoGaugeColorB);
    // Crosshair
    prvI(ui->spinMetroidCrosshairR); prvI(ui->spinMetroidCrosshairG); prvI(ui->spinMetroidCrosshairB);
    prvC(ui->comboMetroidCrosshairColor);
    prvB(ui->cbMetroidCrosshairOutline);
    prvD(ui->spinMetroidCrosshairOutlineOpacity); prvI(ui->spinMetroidCrosshairOutlineThickness);
    prvB(ui->cbMetroidCrosshairCenterDot);
    prvD(ui->spinMetroidCrosshairDotOpacity); prvI(ui->spinMetroidCrosshairDotThickness);
    prvB(ui->cbMetroidCrosshairTStyle);
    prvB(ui->cbMetroidCrosshairInnerShow);
    prvD(ui->spinMetroidCrosshairInnerOpacity);
    prvI(ui->spinMetroidCrosshairInnerLengthX); prvI(ui->spinMetroidCrosshairInnerLengthY);
    prvI(ui->spinMetroidCrosshairInnerThickness); prvI(ui->spinMetroidCrosshairInnerOffset);
    prvB(ui->cbMetroidCrosshairInnerLinkXY);
    prvB(ui->cbMetroidCrosshairOuterShow);
    prvD(ui->spinMetroidCrosshairOuterOpacity);
    prvI(ui->spinMetroidCrosshairOuterLengthX); prvI(ui->spinMetroidCrosshairOuterLengthY);
    prvI(ui->spinMetroidCrosshairOuterThickness); prvI(ui->spinMetroidCrosshairOuterOffset);
    prvB(ui->cbMetroidCrosshairOuterLinkXY);

    m_applyPreviewEnabled = true;
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

void MelonPrimeInputConfig::snapshotVisualConfig()
{
    QVariantMap& s = m_visualSnapshot;
    s.clear();

    auto sI = [&](const char* k, QSpinBox* w)       { s[k] = w->value(); };
    auto sD = [&](const char* k, QDoubleSpinBox* w)  { s[k] = w->value(); };
    auto sB = [&](const char* k, QCheckBox* w)       { s[k] = w->isChecked(); };
    auto sC = [&](const char* k, QComboBox* w)       { s[k] = w->currentIndex(); };
    auto sE = [&](const char* k, QLineEdit* w)       { s[k] = w->text(); };

    sB("cCustomHud",       ui->cbMetroidEnableCustomHud);
    sB("cAspectRatio",     ui->cbMetroidInGameAspectRatio);
    sC("cAspectRatioMode", ui->comboMetroidInGameAspectRatioMode);
    // Match Status
    sB("cMatchShow",  ui->cbMetroidHudMatchStatusShow);
    sI("sMatchX",     ui->spinMetroidHudMatchStatusX);
    sI("sMatchY",     ui->spinMetroidHudMatchStatusY);
    sI("sMatchLOfsX", ui->spinMetroidHudMatchStatusLabelOfsX);
    sI("sMatchLOfsY", ui->spinMetroidHudMatchStatusLabelOfsY);
    sC("cMatchLPos",  ui->comboMetroidHudMatchStatusLabelPos);
    sE("eMatchLP",    ui->leMetroidHudMatchStatusLabelPoints);
    sE("eMatchLO",    ui->leMetroidHudMatchStatusLabelOctoliths);
    sE("eMatchLL",    ui->leMetroidHudMatchStatusLabelLives);
    sE("eMatchLR",    ui->leMetroidHudMatchStatusLabelRingTime);
    sE("eMatchLPT",   ui->leMetroidHudMatchStatusLabelPrimeTime);
    sC("cMatchClr",   ui->comboMetroidHudMatchStatusColor);
    sI("sMatchClrR",  ui->spinMetroidHudMatchStatusColorR);
    sI("sMatchClrG",  ui->spinMetroidHudMatchStatusColorG);
    sI("sMatchClrB",  ui->spinMetroidHudMatchStatusColorB);
    sC("cMatchLblClr",  ui->comboMetroidHudMatchStatusLabelColor);
    sI("sMatchLblClrR", ui->spinMetroidHudMatchStatusLabelColorR);
    sI("sMatchLblClrG", ui->spinMetroidHudMatchStatusLabelColorG);
    sI("sMatchLblClrB", ui->spinMetroidHudMatchStatusLabelColorB);
    sC("cMatchValClr",  ui->comboMetroidHudMatchStatusValueColor);
    sI("sMatchValClrR", ui->spinMetroidHudMatchStatusValueColorR);
    sI("sMatchValClrG", ui->spinMetroidHudMatchStatusValueColorG);
    sI("sMatchValClrB", ui->spinMetroidHudMatchStatusValueColorB);
    sC("cMatchSepClr",  ui->comboMetroidHudMatchStatusSepColor);
    sI("sMatchSepClrR", ui->spinMetroidHudMatchStatusSepColorR);
    sI("sMatchSepClrG", ui->spinMetroidHudMatchStatusSepColorG);
    sI("sMatchSepClrB", ui->spinMetroidHudMatchStatusSepColorB);
    sC("cMatchGolClr",  ui->comboMetroidHudMatchStatusGoalColor);
    sI("sMatchGolClrR", ui->spinMetroidHudMatchStatusGoalColorR);
    sI("sMatchGolClrG", ui->spinMetroidHudMatchStatusGoalColorG);
    sI("sMatchGolClrB", ui->spinMetroidHudMatchStatusGoalColorB);
    // HP/Weapon
    sI("sHpX",  ui->spinMetroidHudHpX);       sI("sHpY",  ui->spinMetroidHudHpY);
    sE("eHpPfx", ui->leMetroidHudHpPrefix);
    sC("cHpAlign", ui->comboMetroidHudHpAlign);
    sB("cHpTxtAuto", ui->cbMetroidHudHpTextAutoColor);
    sC("cHpTxtClr", ui->comboMetroidHudHpTextColor);
    sI("sHpTxtClrR", ui->spinMetroidHudHpTextColorR);
    sI("sHpTxtClrG", ui->spinMetroidHudHpTextColorG);
    sI("sHpTxtClrB", ui->spinMetroidHudHpTextColorB);
    sI("sWpnX", ui->spinMetroidHudWeaponX);    sI("sWpnY", ui->spinMetroidHudWeaponY);
    sE("eAmmoPfx", ui->leMetroidHudAmmoPrefix);
    sC("cAmmoAlign", ui->comboMetroidHudAmmoAlign);
    sC("cAmmoTxtClr", ui->comboMetroidHudAmmoTextColor);
    sI("sAmmoTxtClrR", ui->spinMetroidHudAmmoTextColorR);
    sI("sAmmoTxtClrG", ui->spinMetroidHudAmmoTextColorG);
    sI("sAmmoTxtClrB", ui->spinMetroidHudAmmoTextColorB);
    sC("cHpPos",  ui->comboMetroidHudHpPosition);
    sC("cWpnPos", ui->comboMetroidHudWeaponPosition);
    sB("cWpnIconShow",  ui->cbMetroidHudWeaponIconShow);
    sC("cWpnIconMode",  ui->comboMetroidHudWeaponIconMode);
    sI("sWpnIconOfsX",  ui->spinMetroidHudWeaponIconOffsetX);
    sI("sWpnIconOfsY",  ui->spinMetroidHudWeaponIconOffsetY);
    sI("sWpnIconPosX",   ui->spinMetroidHudWeaponIconPosX);
    sI("sWpnIconPosY",   ui->spinMetroidHudWeaponIconPosY);
    sC("cWpnIconPos",    ui->comboMetroidHudWeaponIconPosition);
    sC("cWpnIconAnchX",  ui->comboMetroidHudWeaponIconAnchorX);
    sC("cWpnIconAnchY",  ui->comboMetroidHudWeaponIconAnchorY);
    sB("cWpnIconClrOv",  ui->cbMetroidHudWeaponIconColorOverlay);
    // HP Gauge
    sB("cHpGauge",       ui->cbMetroidHudHpGauge);
    sC("cHpGaugeOrient", ui->comboMetroidHudHpGaugeOrientation);
    sI("sHpGaugeLen",    ui->spinMetroidHudHpGaugeLength);
    sI("sHpGaugeW",      ui->spinMetroidHudHpGaugeWidth);
    sI("sHpGaugeOfsX",   ui->spinMetroidHudHpGaugeOffsetX);
    sI("sHpGaugeOfsY",   ui->spinMetroidHudHpGaugeOffsetY);
    sC("cHpGaugeAnch",   ui->comboMetroidHudHpGaugeAnchor);
    sC("cHpGaugePosMode",ui->comboMetroidHudHpGaugePosMode);
    sI("sHpGaugePosX",   ui->spinMetroidHudHpGaugePosX);
    sI("sHpGaugePosY",   ui->spinMetroidHudHpGaugePosY);
    sB("cHpGaugeAutoClr",ui->cbMetroidHudHpGaugeAutoColor);
    sC("cHpGaugeClr",    ui->comboMetroidHudHpGaugeColor);
    sI("sHpGaugeClrR",   ui->spinMetroidHudHpGaugeColorR);
    sI("sHpGaugeClrG",   ui->spinMetroidHudHpGaugeColorG);
    sI("sHpGaugeClrB",   ui->spinMetroidHudHpGaugeColorB);
    // Ammo Gauge
    sB("cAmmoGauge",       ui->cbMetroidHudAmmoGauge);
    sC("cAmmoGaugeOrient", ui->comboMetroidHudAmmoGaugeOrientation);
    sI("sAmmoGaugeLen",    ui->spinMetroidHudAmmoGaugeLength);
    sI("sAmmoGaugeW",      ui->spinMetroidHudAmmoGaugeWidth);
    sI("sAmmoGaugeOfsX",   ui->spinMetroidHudAmmoGaugeOffsetX);
    sI("sAmmoGaugeOfsY",   ui->spinMetroidHudAmmoGaugeOffsetY);
    sC("cAmmoGaugeAnch",   ui->comboMetroidHudAmmoGaugeAnchor);
    sC("cAmmoGaugePosMode",ui->comboMetroidHudAmmoGaugePosMode);
    sI("sAmmoGaugePosX",   ui->spinMetroidHudAmmoGaugePosX);
    sI("sAmmoGaugePosY",   ui->spinMetroidHudAmmoGaugePosY);
    sC("cAmmoGaugeClr",    ui->comboMetroidHudAmmoGaugeColor);
    sI("sAmmoGaugeClrR",   ui->spinMetroidHudAmmoGaugeColorR);
    sI("sAmmoGaugeClrG",   ui->spinMetroidHudAmmoGaugeColorG);
    sI("sAmmoGaugeClrB",   ui->spinMetroidHudAmmoGaugeColorB);
    // Crosshair
    sC("cChClr",      ui->comboMetroidCrosshairColor);
    sI("sChR",        ui->spinMetroidCrosshairR);
    sI("sChG",        ui->spinMetroidCrosshairG);
    sI("sChB",        ui->spinMetroidCrosshairB);
    sB("cChOutline",  ui->cbMetroidCrosshairOutline);
    sD("dChOlOp",     ui->spinMetroidCrosshairOutlineOpacity);
    sI("sChOlThick",  ui->spinMetroidCrosshairOutlineThickness);
    sB("cChDot",      ui->cbMetroidCrosshairCenterDot);
    sD("dChDotOp",    ui->spinMetroidCrosshairDotOpacity);
    sI("sChDotThick", ui->spinMetroidCrosshairDotThickness);
    sB("cChTStyle",   ui->cbMetroidCrosshairTStyle);
    sB("cChInnerShow",  ui->cbMetroidCrosshairInnerShow);
    sD("dChInnerOp",    ui->spinMetroidCrosshairInnerOpacity);
    sI("sChInnerLX",    ui->spinMetroidCrosshairInnerLengthX);
    sI("sChInnerLY",    ui->spinMetroidCrosshairInnerLengthY);
    sI("sChInnerThick", ui->spinMetroidCrosshairInnerThickness);
    sI("sChInnerOfs",   ui->spinMetroidCrosshairInnerOffset);
    sB("cChInnerLink",  ui->cbMetroidCrosshairInnerLinkXY);
    sB("cChOuterShow",  ui->cbMetroidCrosshairOuterShow);
    sD("dChOuterOp",    ui->spinMetroidCrosshairOuterOpacity);
    sI("sChOuterLX",    ui->spinMetroidCrosshairOuterLengthX);
    sI("sChOuterLY",    ui->spinMetroidCrosshairOuterLengthY);
    sI("sChOuterThick", ui->spinMetroidCrosshairOuterThickness);
    sI("sChOuterOfs",   ui->spinMetroidCrosshairOuterOffset);
    sB("cChOuterLink",  ui->cbMetroidCrosshairOuterLinkXY);
}

void MelonPrimeInputConfig::restoreVisualSnapshot()
{
    if (m_visualSnapshot.isEmpty()) return;
    m_applyPreviewEnabled = false;

    const QVariantMap& s = m_visualSnapshot;
    auto rI = [&](const char* k, QSpinBox* w) {
        auto it = s.find(k); if (it == s.end()) return;
        w->blockSignals(true); w->setValue(it->toInt()); w->blockSignals(false);
    };
    auto rD = [&](const char* k, QDoubleSpinBox* w) {
        auto it = s.find(k); if (it == s.end()) return;
        w->blockSignals(true); w->setValue(it->toDouble()); w->blockSignals(false);
    };
    auto rB = [&](const char* k, QCheckBox* w) {
        auto it = s.find(k); if (it == s.end()) return;
        w->blockSignals(true); w->setChecked(it->toBool()); w->blockSignals(false);
    };
    auto rC = [&](const char* k, QComboBox* w) {
        auto it = s.find(k); if (it == s.end()) return;
        w->blockSignals(true); w->setCurrentIndex(it->toInt()); w->blockSignals(false);
    };
    auto rE = [&](const char* k, QLineEdit* w) {
        auto it = s.find(k); if (it == s.end()) return;
        w->blockSignals(true); w->setText(it->toString()); w->blockSignals(false);
    };

    rB("cCustomHud",       ui->cbMetroidEnableCustomHud);
    rB("cAspectRatio",     ui->cbMetroidInGameAspectRatio);
    rC("cAspectRatioMode", ui->comboMetroidInGameAspectRatioMode);
    // Match Status
    rB("cMatchShow",  ui->cbMetroidHudMatchStatusShow);
    rI("sMatchX",     ui->spinMetroidHudMatchStatusX);
    rI("sMatchY",     ui->spinMetroidHudMatchStatusY);
    rI("sMatchLOfsX", ui->spinMetroidHudMatchStatusLabelOfsX);
    rI("sMatchLOfsY", ui->spinMetroidHudMatchStatusLabelOfsY);
    rC("cMatchLPos",  ui->comboMetroidHudMatchStatusLabelPos);
    rE("eMatchLP",    ui->leMetroidHudMatchStatusLabelPoints);
    rE("eMatchLO",    ui->leMetroidHudMatchStatusLabelOctoliths);
    rE("eMatchLL",    ui->leMetroidHudMatchStatusLabelLives);
    rE("eMatchLR",    ui->leMetroidHudMatchStatusLabelRingTime);
    rE("eMatchLPT",   ui->leMetroidHudMatchStatusLabelPrimeTime);
    rC("cMatchClr",   ui->comboMetroidHudMatchStatusColor);
    rI("sMatchClrR",  ui->spinMetroidHudMatchStatusColorR);
    rI("sMatchClrG",  ui->spinMetroidHudMatchStatusColorG);
    rI("sMatchClrB",  ui->spinMetroidHudMatchStatusColorB);
    rC("cMatchLblClr",  ui->comboMetroidHudMatchStatusLabelColor);
    rI("sMatchLblClrR", ui->spinMetroidHudMatchStatusLabelColorR);
    rI("sMatchLblClrG", ui->spinMetroidHudMatchStatusLabelColorG);
    rI("sMatchLblClrB", ui->spinMetroidHudMatchStatusLabelColorB);
    rC("cMatchValClr",  ui->comboMetroidHudMatchStatusValueColor);
    rI("sMatchValClrR", ui->spinMetroidHudMatchStatusValueColorR);
    rI("sMatchValClrG", ui->spinMetroidHudMatchStatusValueColorG);
    rI("sMatchValClrB", ui->spinMetroidHudMatchStatusValueColorB);
    rC("cMatchSepClr",  ui->comboMetroidHudMatchStatusSepColor);
    rI("sMatchSepClrR", ui->spinMetroidHudMatchStatusSepColorR);
    rI("sMatchSepClrG", ui->spinMetroidHudMatchStatusSepColorG);
    rI("sMatchSepClrB", ui->spinMetroidHudMatchStatusSepColorB);
    rC("cMatchGolClr",  ui->comboMetroidHudMatchStatusGoalColor);
    rI("sMatchGolClrR", ui->spinMetroidHudMatchStatusGoalColorR);
    rI("sMatchGolClrG", ui->spinMetroidHudMatchStatusGoalColorG);
    rI("sMatchGolClrB", ui->spinMetroidHudMatchStatusGoalColorB);
    // HP/Weapon
    rI("sHpX",  ui->spinMetroidHudHpX);        rI("sHpY",  ui->spinMetroidHudHpY);
    rE("eHpPfx", ui->leMetroidHudHpPrefix);
    rC("cHpAlign", ui->comboMetroidHudHpAlign);
    rB("cHpTxtAuto", ui->cbMetroidHudHpTextAutoColor);
    rC("cHpTxtClr", ui->comboMetroidHudHpTextColor);
    rI("sHpTxtClrR", ui->spinMetroidHudHpTextColorR);
    rI("sHpTxtClrG", ui->spinMetroidHudHpTextColorG);
    rI("sHpTxtClrB", ui->spinMetroidHudHpTextColorB);
    rI("sWpnX", ui->spinMetroidHudWeaponX);     rI("sWpnY", ui->spinMetroidHudWeaponY);
    rE("eAmmoPfx", ui->leMetroidHudAmmoPrefix);
    rC("cAmmoAlign", ui->comboMetroidHudAmmoAlign);
    rC("cAmmoTxtClr", ui->comboMetroidHudAmmoTextColor);
    rI("sAmmoTxtClrR", ui->spinMetroidHudAmmoTextColorR);
    rI("sAmmoTxtClrG", ui->spinMetroidHudAmmoTextColorG);
    rI("sAmmoTxtClrB", ui->spinMetroidHudAmmoTextColorB);
    rC("cHpPos",  ui->comboMetroidHudHpPosition);
    rC("cWpnPos", ui->comboMetroidHudWeaponPosition);
    rB("cWpnIconShow",  ui->cbMetroidHudWeaponIconShow);
    rC("cWpnIconMode",  ui->comboMetroidHudWeaponIconMode);
    rI("sWpnIconOfsX",  ui->spinMetroidHudWeaponIconOffsetX);
    rI("sWpnIconOfsY",  ui->spinMetroidHudWeaponIconOffsetY);
    rI("sWpnIconPosX",   ui->spinMetroidHudWeaponIconPosX);
    rI("sWpnIconPosY",   ui->spinMetroidHudWeaponIconPosY);
    rC("cWpnIconPos",    ui->comboMetroidHudWeaponIconPosition);
    rC("cWpnIconAnchX",  ui->comboMetroidHudWeaponIconAnchorX);
    rC("cWpnIconAnchY",  ui->comboMetroidHudWeaponIconAnchorY);
    rB("cWpnIconClrOv",  ui->cbMetroidHudWeaponIconColorOverlay);
    // HP Gauge
    rB("cHpGauge",       ui->cbMetroidHudHpGauge);
    rC("cHpGaugeOrient", ui->comboMetroidHudHpGaugeOrientation);
    rI("sHpGaugeLen",    ui->spinMetroidHudHpGaugeLength);
    rI("sHpGaugeW",      ui->spinMetroidHudHpGaugeWidth);
    rI("sHpGaugeOfsX",   ui->spinMetroidHudHpGaugeOffsetX);
    rI("sHpGaugeOfsY",   ui->spinMetroidHudHpGaugeOffsetY);
    rC("cHpGaugeAnch",   ui->comboMetroidHudHpGaugeAnchor);
    rC("cHpGaugePosMode",ui->comboMetroidHudHpGaugePosMode);
    rI("sHpGaugePosX",   ui->spinMetroidHudHpGaugePosX);
    rI("sHpGaugePosY",   ui->spinMetroidHudHpGaugePosY);
    rB("cHpGaugeAutoClr",ui->cbMetroidHudHpGaugeAutoColor);
    rC("cHpGaugeClr",    ui->comboMetroidHudHpGaugeColor);
    rI("sHpGaugeClrR",   ui->spinMetroidHudHpGaugeColorR);
    rI("sHpGaugeClrG",   ui->spinMetroidHudHpGaugeColorG);
    rI("sHpGaugeClrB",   ui->spinMetroidHudHpGaugeColorB);
    // Ammo Gauge
    rB("cAmmoGauge",       ui->cbMetroidHudAmmoGauge);
    rC("cAmmoGaugeOrient", ui->comboMetroidHudAmmoGaugeOrientation);
    rI("sAmmoGaugeLen",    ui->spinMetroidHudAmmoGaugeLength);
    rI("sAmmoGaugeW",      ui->spinMetroidHudAmmoGaugeWidth);
    rI("sAmmoGaugeOfsX",   ui->spinMetroidHudAmmoGaugeOffsetX);
    rI("sAmmoGaugeOfsY",   ui->spinMetroidHudAmmoGaugeOffsetY);
    rC("cAmmoGaugeAnch",   ui->comboMetroidHudAmmoGaugeAnchor);
    rC("cAmmoGaugePosMode",ui->comboMetroidHudAmmoGaugePosMode);
    rI("sAmmoGaugePosX",   ui->spinMetroidHudAmmoGaugePosX);
    rI("sAmmoGaugePosY",   ui->spinMetroidHudAmmoGaugePosY);
    rC("cAmmoGaugeClr",    ui->comboMetroidHudAmmoGaugeColor);
    rI("sAmmoGaugeClrR",   ui->spinMetroidHudAmmoGaugeColorR);
    rI("sAmmoGaugeClrG",   ui->spinMetroidHudAmmoGaugeColorG);
    rI("sAmmoGaugeClrB",   ui->spinMetroidHudAmmoGaugeColorB);
    // Crosshair
    rC("cChClr",      ui->comboMetroidCrosshairColor);
    rI("sChR",        ui->spinMetroidCrosshairR);
    rI("sChG",        ui->spinMetroidCrosshairG);
    rI("sChB",        ui->spinMetroidCrosshairB);
    rB("cChOutline",  ui->cbMetroidCrosshairOutline);
    rD("dChOlOp",     ui->spinMetroidCrosshairOutlineOpacity);
    rI("sChOlThick",  ui->spinMetroidCrosshairOutlineThickness);
    rB("cChDot",      ui->cbMetroidCrosshairCenterDot);
    rD("dChDotOp",    ui->spinMetroidCrosshairDotOpacity);
    rI("sChDotThick", ui->spinMetroidCrosshairDotThickness);
    rB("cChTStyle",   ui->cbMetroidCrosshairTStyle);
    rB("cChInnerShow",  ui->cbMetroidCrosshairInnerShow);
    rD("dChInnerOp",    ui->spinMetroidCrosshairInnerOpacity);
    rI("sChInnerLX",    ui->spinMetroidCrosshairInnerLengthX);
    rI("sChInnerLY",    ui->spinMetroidCrosshairInnerLengthY);
    rI("sChInnerThick", ui->spinMetroidCrosshairInnerThickness);
    rI("sChInnerOfs",   ui->spinMetroidCrosshairInnerOffset);
    rB("cChInnerLink",  ui->cbMetroidCrosshairInnerLinkXY);
    rB("cChOuterShow",  ui->cbMetroidCrosshairOuterShow);
    rD("dChOuterOp",    ui->spinMetroidCrosshairOuterOpacity);
    rI("sChOuterLX",    ui->spinMetroidCrosshairOuterLengthX);
    rI("sChOuterLY",    ui->spinMetroidCrosshairOuterLengthY);
    rI("sChOuterThick", ui->spinMetroidCrosshairOuterThickness);
    rI("sChOuterOfs",   ui->spinMetroidCrosshairOuterOffset);
    rB("cChOuterLink",  ui->cbMetroidCrosshairOuterLinkXY);

    // Restore hex code displays (derived from RGB spins)
    auto updateHex = [](QLineEdit* le, QSpinBox* r, QSpinBox* g, QSpinBox* b) {
        le->setText(QString("#%1%2%3")
            .arg(r->value(),2,16,QChar('0'))
            .arg(g->value(),2,16,QChar('0'))
            .arg(b->value(),2,16,QChar('0')).toUpper());
    };
    updateHex(ui->leMetroidHudMatchStatusColorCode,
              ui->spinMetroidHudMatchStatusColorR, ui->spinMetroidHudMatchStatusColorG, ui->spinMetroidHudMatchStatusColorB);
    updateHex(ui->leMetroidHudMatchStatusLabelColorCode,
              ui->spinMetroidHudMatchStatusLabelColorR, ui->spinMetroidHudMatchStatusLabelColorG, ui->spinMetroidHudMatchStatusLabelColorB);
    updateHex(ui->leMetroidHudMatchStatusValueColorCode,
              ui->spinMetroidHudMatchStatusValueColorR, ui->spinMetroidHudMatchStatusValueColorG, ui->spinMetroidHudMatchStatusValueColorB);
    updateHex(ui->leMetroidHudMatchStatusSepColorCode,
              ui->spinMetroidHudMatchStatusSepColorR, ui->spinMetroidHudMatchStatusSepColorG, ui->spinMetroidHudMatchStatusSepColorB);
    updateHex(ui->leMetroidHudMatchStatusGoalColorCode,
              ui->spinMetroidHudMatchStatusGoalColorR, ui->spinMetroidHudMatchStatusGoalColorG, ui->spinMetroidHudMatchStatusGoalColorB);
    updateHex(ui->leMetroidHudHpGaugeColorCode,
              ui->spinMetroidHudHpGaugeColorR, ui->spinMetroidHudHpGaugeColorG, ui->spinMetroidHudHpGaugeColorB);
    updateHex(ui->leMetroidHudAmmoGaugeColorCode,
              ui->spinMetroidHudAmmoGaugeColorR, ui->spinMetroidHudAmmoGaugeColorG, ui->spinMetroidHudAmmoGaugeColorB);
    updateHex(ui->leMetroidCrosshairColorCode,
              ui->spinMetroidCrosshairR, ui->spinMetroidCrosshairG, ui->spinMetroidCrosshairB);

    // Restore sub-color enable state (depends on combo index: 0 = Overall = disabled)
    auto updateSubEn = [](QComboBox* c, QLineEdit* le, QSpinBox* r, QSpinBox* g, QSpinBox* b) {
        bool en = c->currentIndex() != 0;
        le->setEnabled(en); r->setEnabled(en); g->setEnabled(en); b->setEnabled(en);
    };
    updateSubEn(ui->comboMetroidHudMatchStatusLabelColor,
                ui->leMetroidHudMatchStatusLabelColorCode,
                ui->spinMetroidHudMatchStatusLabelColorR, ui->spinMetroidHudMatchStatusLabelColorG, ui->spinMetroidHudMatchStatusLabelColorB);
    updateSubEn(ui->comboMetroidHudMatchStatusValueColor,
                ui->leMetroidHudMatchStatusValueColorCode,
                ui->spinMetroidHudMatchStatusValueColorR, ui->spinMetroidHudMatchStatusValueColorG, ui->spinMetroidHudMatchStatusValueColorB);
    updateSubEn(ui->comboMetroidHudMatchStatusSepColor,
                ui->leMetroidHudMatchStatusSepColorCode,
                ui->spinMetroidHudMatchStatusSepColorR, ui->spinMetroidHudMatchStatusSepColorG, ui->spinMetroidHudMatchStatusSepColorB);
    updateSubEn(ui->comboMetroidHudMatchStatusGoalColor,
                ui->leMetroidHudMatchStatusGoalColorCode,
                ui->spinMetroidHudMatchStatusGoalColorR, ui->spinMetroidHudMatchStatusGoalColorG, ui->spinMetroidHudMatchStatusGoalColorB);

    m_applyPreviewEnabled = true;
    applyVisualPreview();
}

void MelonPrimeInputConfig::applyVisualPreview()
{
#ifdef MELONPRIME_CUSTOM_HUD
    if (!m_applyPreviewEnabled) return;

    Config::Table& instcfg = emuInstance->getLocalConfig();

    instcfg.SetBool("Metroid.Visual.CustomHUD",              ui->cbMetroidEnableCustomHud->isChecked());
    instcfg.SetBool("Metroid.Visual.InGameAspectRatio",      ui->cbMetroidInGameAspectRatio->isChecked());
    instcfg.SetInt ("Metroid.Visual.InGameAspectRatioMode",  ui->comboMetroidInGameAspectRatioMode->currentIndex());
    instcfg.SetBool("Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame", ui->cbMetroidClipCursorToBottomScreenWhenNotInGame->isChecked());

    instcfg.SetBool("Metroid.Visual.HudMatchStatusShow",     ui->cbMetroidHudMatchStatusShow->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusX",        ui->spinMetroidHudMatchStatusX->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusY",        ui->spinMetroidHudMatchStatusY->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusLabelOfsX",ui->spinMetroidHudMatchStatusLabelOfsX->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusLabelOfsY",ui->spinMetroidHudMatchStatusLabelOfsY->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusLabelPos", ui->comboMetroidHudMatchStatusLabelPos->currentIndex());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelPoints",    ui->leMetroidHudMatchStatusLabelPoints->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelOctoliths", ui->leMetroidHudMatchStatusLabelOctoliths->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelLives",     ui->leMetroidHudMatchStatusLabelLives->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelRingTime",  ui->leMetroidHudMatchStatusLabelRingTime->text().toStdString());
    instcfg.SetString("Metroid.Visual.HudMatchStatusLabelPrimeTime", ui->leMetroidHudMatchStatusLabelPrimeTime->text().toStdString());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusColorR",    ui->spinMetroidHudMatchStatusColorR->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusColorG",    ui->spinMetroidHudMatchStatusColorG->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusColorB",    ui->spinMetroidHudMatchStatusColorB->value());
    instcfg.SetBool("Metroid.Visual.HudMatchStatusLabelColorOverall", ui->comboMetroidHudMatchStatusLabelColor->currentIndex() == 0);
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusLabelColorR", ui->spinMetroidHudMatchStatusLabelColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusLabelColorG", ui->spinMetroidHudMatchStatusLabelColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusLabelColorB", ui->spinMetroidHudMatchStatusLabelColorB->value());
    instcfg.SetBool("Metroid.Visual.HudMatchStatusValueColorOverall", ui->comboMetroidHudMatchStatusValueColor->currentIndex() == 0);
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusValueColorR", ui->spinMetroidHudMatchStatusValueColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusValueColorG", ui->spinMetroidHudMatchStatusValueColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusValueColorB", ui->spinMetroidHudMatchStatusValueColorB->value());
    instcfg.SetBool("Metroid.Visual.HudMatchStatusSepColorOverall", ui->comboMetroidHudMatchStatusSepColor->currentIndex() == 0);
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusSepColorR",   ui->spinMetroidHudMatchStatusSepColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusSepColorG",   ui->spinMetroidHudMatchStatusSepColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusSepColorB",   ui->spinMetroidHudMatchStatusSepColorB->value());
    instcfg.SetBool("Metroid.Visual.HudMatchStatusGoalColorOverall", ui->comboMetroidHudMatchStatusGoalColor->currentIndex() == 0);
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusGoalColorR",  ui->spinMetroidHudMatchStatusGoalColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusGoalColorG",  ui->spinMetroidHudMatchStatusGoalColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudMatchStatusGoalColorB",  ui->spinMetroidHudMatchStatusGoalColorB->value());

    instcfg.SetInt ("Metroid.Visual.HudHpX",              ui->spinMetroidHudHpX->value());
    instcfg.SetInt ("Metroid.Visual.HudHpY",              ui->spinMetroidHudHpY->value());
    instcfg.SetString("Metroid.Visual.HudHpPrefix",       ui->leMetroidHudHpPrefix->text().toStdString());
    instcfg.SetInt ("Metroid.Visual.HudHpAlign",          ui->comboMetroidHudHpAlign->currentIndex());
    instcfg.SetBool("Metroid.Visual.HudHpTextAutoColor",   ui->cbMetroidHudHpTextAutoColor->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudHpTextColorR",      ui->spinMetroidHudHpTextColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudHpTextColorG",      ui->spinMetroidHudHpTextColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudHpTextColorB",      ui->spinMetroidHudHpTextColorB->value());
    instcfg.SetInt ("Metroid.Visual.HudWeaponX",          ui->spinMetroidHudWeaponX->value());
    instcfg.SetInt ("Metroid.Visual.HudWeaponY",          ui->spinMetroidHudWeaponY->value());
    instcfg.SetString("Metroid.Visual.HudAmmoPrefix",     ui->leMetroidHudAmmoPrefix->text().toStdString());
    instcfg.SetInt ("Metroid.Visual.HudAmmoAlign",        ui->comboMetroidHudAmmoAlign->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudAmmoTextColorR",    ui->spinMetroidHudAmmoTextColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoTextColorG",    ui->spinMetroidHudAmmoTextColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoTextColorB",    ui->spinMetroidHudAmmoTextColorB->value());
    instcfg.SetBool("Metroid.Visual.HudWeaponIconShow",   ui->cbMetroidHudWeaponIconShow->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconMode",   ui->comboMetroidHudWeaponIconMode->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconOffsetX",ui->spinMetroidHudWeaponIconOffsetX->value());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconOffsetY",ui->spinMetroidHudWeaponIconOffsetY->value());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconPosX",     ui->spinMetroidHudWeaponIconPosX->value());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconPosY",     ui->spinMetroidHudWeaponIconPosY->value());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconAnchorX",  ui->comboMetroidHudWeaponIconAnchorX->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudWeaponIconAnchorY",  ui->comboMetroidHudWeaponIconAnchorY->currentIndex());
    instcfg.SetBool("Metroid.Visual.HudWeaponIconColorOverlay", ui->cbMetroidHudWeaponIconColorOverlay->isChecked());

    instcfg.SetBool("Metroid.Visual.HudHpGauge",               ui->cbMetroidHudHpGauge->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeOrientation",    ui->comboMetroidHudHpGaugeOrientation->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeLength",         ui->spinMetroidHudHpGaugeLength->value());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeWidth",          ui->spinMetroidHudHpGaugeWidth->value());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeOffsetX",        ui->spinMetroidHudHpGaugeOffsetX->value());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeOffsetY",        ui->spinMetroidHudHpGaugeOffsetY->value());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeAnchor",         ui->comboMetroidHudHpGaugeAnchor->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugePosMode",        ui->comboMetroidHudHpGaugePosMode->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugePosX",           ui->spinMetroidHudHpGaugePosX->value());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugePosY",           ui->spinMetroidHudHpGaugePosY->value());
    instcfg.SetBool("Metroid.Visual.HudHpGaugeAutoColor",      ui->cbMetroidHudHpGaugeAutoColor->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeColorR",         ui->spinMetroidHudHpGaugeColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeColorG",         ui->spinMetroidHudHpGaugeColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudHpGaugeColorB",         ui->spinMetroidHudHpGaugeColorB->value());

    instcfg.SetBool("Metroid.Visual.HudAmmoGauge",             ui->cbMetroidHudAmmoGauge->isChecked());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeOrientation",  ui->comboMetroidHudAmmoGaugeOrientation->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeLength",       ui->spinMetroidHudAmmoGaugeLength->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeWidth",        ui->spinMetroidHudAmmoGaugeWidth->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeOffsetX",      ui->spinMetroidHudAmmoGaugeOffsetX->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeOffsetY",      ui->spinMetroidHudAmmoGaugeOffsetY->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeAnchor",       ui->comboMetroidHudAmmoGaugeAnchor->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugePosMode",      ui->comboMetroidHudAmmoGaugePosMode->currentIndex());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugePosX",         ui->spinMetroidHudAmmoGaugePosX->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugePosY",         ui->spinMetroidHudAmmoGaugePosY->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeColorR",       ui->spinMetroidHudAmmoGaugeColorR->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeColorG",       ui->spinMetroidHudAmmoGaugeColorG->value());
    instcfg.SetInt ("Metroid.Visual.HudAmmoGaugeColorB",       ui->spinMetroidHudAmmoGaugeColorB->value());

    instcfg.SetInt ("Metroid.Visual.CrosshairColorR",          ui->spinMetroidCrosshairR->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairColorG",          ui->spinMetroidCrosshairG->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairColorB",          ui->spinMetroidCrosshairB->value());
    instcfg.SetBool("Metroid.Visual.CrosshairOutline",         ui->cbMetroidCrosshairOutline->isChecked());
    instcfg.SetDouble("Metroid.Visual.CrosshairOutlineOpacity",ui->spinMetroidCrosshairOutlineOpacity->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairOutlineThickness",ui->spinMetroidCrosshairOutlineThickness->value());
    instcfg.SetBool("Metroid.Visual.CrosshairCenterDot",       ui->cbMetroidCrosshairCenterDot->isChecked());
    instcfg.SetDouble("Metroid.Visual.CrosshairDotOpacity",    ui->spinMetroidCrosshairDotOpacity->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairDotThickness",    ui->spinMetroidCrosshairDotThickness->value());
    instcfg.SetBool("Metroid.Visual.CrosshairTStyle",          ui->cbMetroidCrosshairTStyle->isChecked());
    instcfg.SetBool("Metroid.Visual.CrosshairInnerShow",       ui->cbMetroidCrosshairInnerShow->isChecked());
    instcfg.SetDouble("Metroid.Visual.CrosshairInnerOpacity",  ui->spinMetroidCrosshairInnerOpacity->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairInnerLengthX",    ui->spinMetroidCrosshairInnerLengthX->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairInnerLengthY",    ui->spinMetroidCrosshairInnerLengthY->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairInnerThickness",  ui->spinMetroidCrosshairInnerThickness->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairInnerOffset",     ui->spinMetroidCrosshairInnerOffset->value());
    instcfg.SetBool("Metroid.Visual.CrosshairInnerLinkXY",     ui->cbMetroidCrosshairInnerLinkXY->isChecked());
    instcfg.SetBool("Metroid.Visual.CrosshairOuterShow",       ui->cbMetroidCrosshairOuterShow->isChecked());
    instcfg.SetDouble("Metroid.Visual.CrosshairOuterOpacity",  ui->spinMetroidCrosshairOuterOpacity->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairOuterLengthX",    ui->spinMetroidCrosshairOuterLengthX->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairOuterLengthY",    ui->spinMetroidCrosshairOuterLengthY->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairOuterThickness",  ui->spinMetroidCrosshairOuterThickness->value());
    instcfg.SetInt ("Metroid.Visual.CrosshairOuterOffset",     ui->spinMetroidCrosshairOuterOffset->value());
    instcfg.SetBool("Metroid.Visual.CrosshairOuterLinkXY",     ui->cbMetroidCrosshairOuterLinkXY->isChecked());

    MelonPrime::CustomHud_InvalidateConfigCache();
#endif
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
    instcfg.SetInt("Metroid.Visual.HudMatchStatusColorR", ui->spinMetroidHudMatchStatusColorR->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusColorG", ui->spinMetroidHudMatchStatusColorG->value());
    instcfg.SetInt("Metroid.Visual.HudMatchStatusColorB", ui->spinMetroidHudMatchStatusColorB->value());
    // Sub-colors (index 0 = Overall)
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

    // Custom HUD
    instcfg.SetBool("Metroid.Visual.CustomHUD", ui->cbMetroidEnableCustomHud->checkState() == Qt::Checked);

    // Section toggle states
    instcfg.SetBool("Metroid.UI.SectionCrosshair",      ui->btnToggleCrosshair->isChecked());
    instcfg.SetBool("Metroid.UI.SectionInner",          ui->btnToggleInner->isChecked());
    instcfg.SetBool("Metroid.UI.SectionOuter",          ui->btnToggleOuter->isChecked());
    instcfg.SetBool("Metroid.UI.SectionHpPos",          ui->btnToggleHpPos->isChecked());
    instcfg.SetBool("Metroid.UI.SectionWpnPos",         ui->btnToggleWpnPos->isChecked());
    instcfg.SetBool("Metroid.UI.SectionWpnIcon",        ui->btnToggleWpnIcon->isChecked());
    instcfg.SetBool("Metroid.UI.SectionHpGauge",        ui->btnToggleHpGauge->isChecked());
    instcfg.SetBool("Metroid.UI.SectionAmmoGauge",      ui->btnToggleAmmoGauge->isChecked());
    instcfg.SetBool("Metroid.UI.SectionInputSettings",  ui->btnToggleInputSettings->isChecked());
    instcfg.SetBool("Metroid.UI.SectionScreenSync",     ui->btnToggleScreenSync->isChecked());
    instcfg.SetBool("Metroid.UI.SectionCursorClipSettings",  ui->btnToggleCursorClipSettings->isChecked());
    instcfg.SetBool("Metroid.UI.SectionInGameAspectRatio",  ui->btnToggleInGameAspectRatio->isChecked());
    instcfg.SetBool("Metroid.UI.SectionSensitivity",    ui->btnToggleSensitivity->isChecked());
    instcfg.SetBool("Metroid.UI.SectionGameplay",       ui->btnToggleGameplay->isChecked());
    instcfg.SetBool("Metroid.UI.SectionVideo",          ui->btnToggleVideo->isChecked());
    instcfg.SetBool("Metroid.UI.SectionVolume",         ui->btnToggleVolume->isChecked());
    instcfg.SetBool("Metroid.UI.SectionLicense",        ui->btnToggleLicense->isChecked());

    // Crosshair — Color
    instcfg.SetInt("Metroid.Visual.CrosshairColorR", ui->spinMetroidCrosshairR->value());

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

    // Gauge settings — HP
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

    // Gauge settings — Ammo
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

    // P-3: Invalidate cached config so next frame re-reads all values
    MelonPrime::CustomHud_InvalidateConfigCache();
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
    ui->comboMetroidCrosshairColor->setCurrentIndex(21);
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
    ui->comboMetroidCrosshairColor->setCurrentIndex(21);
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
        {56,192,8},    // 8: Sylux Hud Color
        {248,248,88},  // 9: Kanden HUD Color
        {120,240,64},  // 10: Samus HUD
        {40,152,80},   // 11: Samus HUD Outline
        {248,176,24},  // 12: Spire HUD
        {200,80,40},   // 13: Spire HUD Outline
        {248,40,40},   // 14: Trace HUD
        {80,152,208},  // 15: Noxus HUD
        {40,104,152},  // 16: Noxus HUD Outline
        {208,152,56},  // 17: Weavel HUD
        {248,224,128}, // 18: Weavel HUD Outline
        {76,0,252},    // 19: Avium Purple
        {88,224,40},   // 20: Sylux Crosshair Color
    };
    if (index < 0 || index >= 21) return; // Custom = don't change

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

void MelonPrimeInputConfig::resetCrosshairDefaults()
{

    // Color: Red (#FF0000) = preset index 7
    ui->comboMetroidCrosshairColor->setCurrentIndex(7); // triggers RGB update

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

    // HP Position
    ui->comboMetroidHudHpPosition->setCurrentIndex(8); // Custom
    ui->spinMetroidHudHpX->setValue(45);
    ui->spinMetroidHudHpY->setValue(99);
    ui->leMetroidHudHpPrefix->setText("");
    ui->comboMetroidHudHpAlign->setCurrentIndex(2); // Right
    ui->cbMetroidHudHpTextAutoColor->setChecked(true);
    ui->comboMetroidHudHpTextColor->setCurrentIndex(0);
    ui->spinMetroidHudHpTextColorR->setValue(255);
    ui->spinMetroidHudHpTextColorG->setValue(255);
    ui->spinMetroidHudHpTextColorB->setValue(255);
    ui->leMetroidHudHpTextColorCode->setText("#FFFFFF");

    // Weapon Position
    ui->comboMetroidHudWeaponPosition->setCurrentIndex(8); // Custom
    ui->spinMetroidHudWeaponX->setValue(230);
    ui->spinMetroidHudWeaponY->setValue(99);
    ui->leMetroidHudAmmoPrefix->setText("");
    ui->comboMetroidHudAmmoAlign->setCurrentIndex(2); // Right
    ui->comboMetroidHudAmmoTextColor->setCurrentIndex(0);
    ui->spinMetroidHudAmmoTextColorR->setValue(255);
    ui->spinMetroidHudAmmoTextColorG->setValue(255);
    ui->spinMetroidHudAmmoTextColorB->setValue(255);
    ui->leMetroidHudAmmoTextColorCode->setText("#FFFFFF");

    // Weapon Icon
    ui->cbMetroidHudWeaponIconShow->setChecked(true);
    ui->comboMetroidHudWeaponIconMode->setCurrentIndex(1); // Independent
    ui->spinMetroidHudWeaponIconOffsetX->setValue(0);
    ui->spinMetroidHudWeaponIconOffsetY->setValue(10);
    ui->comboMetroidHudWeaponIconPosition->setCurrentIndex(8); // Custom
    ui->spinMetroidHudWeaponIconPosX->setValue(239);
    ui->spinMetroidHudWeaponIconPosY->setValue(149);
    ui->comboMetroidHudWeaponIconAnchorX->setCurrentIndex(1);
    ui->comboMetroidHudWeaponIconAnchorY->setCurrentIndex(1);
    ui->cbMetroidHudWeaponIconColorOverlay->setChecked(true);

    // HP Gauge
    ui->cbMetroidHudHpGauge->setChecked(true);
    ui->comboMetroidHudHpGaugeOrientation->setCurrentIndex(1); // Vertical
    ui->spinMetroidHudHpGaugeLength->setValue(80);
    ui->spinMetroidHudHpGaugeWidth->setValue(3);
    ui->spinMetroidHudHpGaugeOffsetX->setValue(-14);
    ui->spinMetroidHudHpGaugeOffsetY->setValue(1);
    ui->comboMetroidHudHpGaugeAnchor->setCurrentIndex(3);
    ui->comboMetroidHudHpGaugePosMode->setCurrentIndex(1);
    ui->spinMetroidHudHpGaugePosX->setValue(14);
    ui->spinMetroidHudHpGaugePosY->setValue(56);
    ui->cbMetroidHudHpGaugeAutoColor->setChecked(true);
    ui->comboMetroidHudHpGaugeColor->setCurrentIndex(6); // Sylux Hud Color
    ui->spinMetroidHudHpGaugeColorR->setValue(56);
    ui->spinMetroidHudHpGaugeColorG->setValue(192);
    ui->spinMetroidHudHpGaugeColorB->setValue(8);
    ui->leMetroidHudHpGaugeColorCode->setText("#38C008");

    // Ammo Gauge
    ui->cbMetroidHudAmmoGauge->setChecked(true);
    ui->comboMetroidHudAmmoGaugeOrientation->setCurrentIndex(1); // Vertical
    ui->spinMetroidHudAmmoGaugeLength->setValue(80);
    ui->spinMetroidHudAmmoGaugeWidth->setValue(3);
    ui->spinMetroidHudAmmoGaugeOffsetX->setValue(9);
    ui->spinMetroidHudAmmoGaugeOffsetY->setValue(2);
    ui->comboMetroidHudAmmoGaugeAnchor->setCurrentIndex(2);
    ui->comboMetroidHudAmmoGaugePosMode->setCurrentIndex(0);
    ui->spinMetroidHudAmmoGaugePosX->setValue(239);
    ui->spinMetroidHudAmmoGaugePosY->setValue(56);
    ui->comboMetroidHudAmmoGaugeColor->setCurrentIndex(6); // Sylux Hud Color
    ui->spinMetroidHudAmmoGaugeColorR->setValue(56);
    ui->spinMetroidHudAmmoGaugeColorG->setValue(192);
    ui->spinMetroidHudAmmoGaugeColorB->setValue(8);
    ui->leMetroidHudAmmoGaugeColorCode->setText("#38C008");
}

void MelonPrimeInputConfig::resetMatchStatusDefaults()
{

    ui->cbMetroidHudMatchStatusShow->setChecked(true);
    ui->spinMetroidHudMatchStatusX->setValue(20);
    ui->spinMetroidHudMatchStatusY->setValue(19);
    ui->comboMetroidHudMatchStatusLabelPos->setCurrentIndex(0); // Above
    ui->spinMetroidHudMatchStatusLabelOfsX->setValue(0);
    ui->spinMetroidHudMatchStatusLabelOfsY->setValue(1);
    ui->leMetroidHudMatchStatusLabelPoints->setText("points");
    ui->leMetroidHudMatchStatusLabelOctoliths->setText("octoliths");
    ui->leMetroidHudMatchStatusLabelLives->setText("lives left");
    ui->leMetroidHudMatchStatusLabelRingTime->setText("ring time");
    ui->leMetroidHudMatchStatusLabelPrimeTime->setText("prime time");

    // Overall color: White
    ui->comboMetroidHudMatchStatusColor->setCurrentIndex(0);
    ui->spinMetroidHudMatchStatusColorR->setValue(255);
    ui->spinMetroidHudMatchStatusColorG->setValue(255);
    ui->spinMetroidHudMatchStatusColorB->setValue(255);
    ui->leMetroidHudMatchStatusColorCode->setText("#FFFFFF");

    // Sub-colors: all Overall
    ui->comboMetroidHudMatchStatusLabelColor->setCurrentIndex(0);
    ui->comboMetroidHudMatchStatusValueColor->setCurrentIndex(0);
    ui->comboMetroidHudMatchStatusSepColor->setCurrentIndex(0);
    ui->comboMetroidHudMatchStatusGoalColor->setCurrentIndex(0);
}
