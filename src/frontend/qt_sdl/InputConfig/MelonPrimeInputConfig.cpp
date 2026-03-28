/*
    Copyright 2016-2025 melonDS team
    (MelonPrime specific configuration extension)
*/

#include <QGroupBox>
#include <QLabel>
#include <QGridLayout>
#include <QTabWidget>
#include <QSpinBox>
#include <QSlider>
#include <QColor>
#include <QLineEdit>
#include <QComboBox>
#include <QPainter>
#include <QPainterPath>
#include <QColorDialog>
#include <QFontDatabase>
#include <QTimer>

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

    auto hideRedundantValueLabel = [](QLabel* label) {
        if (label) label->hide();
    };
    hideRedundantValueLabel(ui->labelMetroidHudMatchStatusX);
    hideRedundantValueLabel(ui->labelMetroidHudMatchStatusY);
    hideRedundantValueLabel(ui->labelMetroidHudMatchStatusLabelOfsX);
    hideRedundantValueLabel(ui->labelMetroidHudMatchStatusLabelOfsY);
    hideRedundantValueLabel(ui->labelMetroidHudHpX);
    hideRedundantValueLabel(ui->labelMetroidHudHpY);
    hideRedundantValueLabel(ui->labelMetroidHudWeaponX);
    hideRedundantValueLabel(ui->labelMetroidHudWeaponY);
    hideRedundantValueLabel(ui->labelMetroidHudWeaponIconOffsetX);
    hideRedundantValueLabel(ui->labelMetroidHudWeaponIconOffsetY);
    hideRedundantValueLabel(ui->labelMetroidHudWeaponIconPosX);
    hideRedundantValueLabel(ui->labelMetroidHudWeaponIconPosY);
    hideRedundantValueLabel(ui->labelMetroidHudHpGaugeLength);
    hideRedundantValueLabel(ui->labelMetroidHudHpGaugeWidth);
    hideRedundantValueLabel(ui->labelMetroidHudHpGaugeOffsetX);
    hideRedundantValueLabel(ui->labelMetroidHudHpGaugeOffsetY);
    hideRedundantValueLabel(ui->labelMetroidHudHpGaugePosX);
    hideRedundantValueLabel(ui->labelMetroidHudHpGaugePosY);
    hideRedundantValueLabel(ui->labelMetroidHudAmmoGaugeLength);
    hideRedundantValueLabel(ui->labelMetroidHudAmmoGaugeWidth);
    hideRedundantValueLabel(ui->labelMetroidHudAmmoGaugeOffsetX);
    hideRedundantValueLabel(ui->labelMetroidHudAmmoGaugeOffsetY);
    hideRedundantValueLabel(ui->labelMetroidHudAmmoGaugePosX);
    hideRedundantValueLabel(ui->labelMetroidHudAmmoGaugePosY);
    hideRedundantValueLabel(ui->labelMetroidBtmOverlayDstX);
    hideRedundantValueLabel(ui->labelMetroidBtmOverlayDstY);
    hideRedundantValueLabel(ui->labelMetroidBtmOverlayDstSize);
    hideRedundantValueLabel(ui->labelMetroidBtmOverlaySrcRadius);
    hideRedundantValueLabel(ui->labelMetroidCrosshairOutlineThickness);
    hideRedundantValueLabel(ui->labelMetroidCrosshairDotThickness);
    hideRedundantValueLabel(ui->labelMetroidCrosshairInnerLengthX);
    hideRedundantValueLabel(ui->labelMetroidCrosshairInnerLengthY);
    hideRedundantValueLabel(ui->labelMetroidCrosshairInnerThickness);
    hideRedundantValueLabel(ui->labelMetroidCrosshairInnerOffset);
    hideRedundantValueLabel(ui->labelMetroidCrosshairOuterLengthX);
    hideRedundantValueLabel(ui->labelMetroidCrosshairOuterLengthY);
    hideRedundantValueLabel(ui->labelMetroidCrosshairOuterThickness);
    hideRedundantValueLabel(ui->labelMetroidCrosshairOuterOffset);



    Config::Table& instcfg = emuInstance->getLocalConfig();
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

    auto initSliderSync = [this](QSlider* sl, QSpinBox* input, QLabel* lbl, int val) {
        sl->setValue(val);
        if (input)
            input->setValue(val);
        if (lbl)
            lbl->setText(QString::number(val));

        QObject::connect(sl, &QSlider::valueChanged, this, [sl, input, lbl](int v) {
            if (input) {
                const bool old = input->blockSignals(true);
                input->setValue(v);
                input->blockSignals(old);
            }
            if (lbl)
                lbl->setText(QString::number(v));
        });

        if (input) {
            QObject::connect(input, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, sl, input, lbl](int v) {
                const bool old = sl->blockSignals(true);
                sl->setValue(v);
                sl->blockSignals(old);
                if (lbl)
                    lbl->setText(QString::number(v));
                applyVisualPreview();
            });
        }
    };

    auto syncColorPickerUiNow = [this, &instcfg](
        QPushButton* btn,
        const char* cfgKeyR,
        const char* cfgKeyG,
        const char* cfgKeyB,
        QComboBox* combo,
        QLineEdit* lineEdit,
        QSpinBox* spinR,
        QSpinBox* spinG,
        QSpinBox* spinB,
        int customIndex,
        int overallIndex = -1) {
        QObject::connect(btn, &QPushButton::clicked, this, [=, &instcfg]() {
            const int r = instcfg.GetInt(cfgKeyR);
            const int g = instcfg.GetInt(cfgKeyG);
            const int b = instcfg.GetInt(cfgKeyB);

            spinR->blockSignals(true);
            spinG->blockSignals(true);
            spinB->blockSignals(true);
            spinR->setValue(r);
            spinG->setValue(g);
            spinB->setValue(b);
            spinR->blockSignals(false);
            spinG->blockSignals(false);
            spinB->blockSignals(false);

            if (lineEdit) {
                lineEdit->blockSignals(true);
                lineEdit->setText(
                    QString("#%1%2%3")
                        .arg(r, 2, 16, QChar('0'))
                        .arg(g, 2, 16, QChar('0'))
                        .arg(b, 2, 16, QChar('0')).toUpper());
                lineEdit->blockSignals(false);
            }

            if (combo) {
                combo->blockSignals(true);
                combo->setCurrentIndex(overallIndex >= 0 ? overallIndex : customIndex);
                combo->blockSignals(false);
            }

            applyVisualPreview();
        });
    };

    auto bindHexButtonSync = [this](QPushButton* btn, QLineEdit* lineEdit) {
        connect(lineEdit, &QLineEdit::editingFinished, this, [btn, lineEdit]() {
            QColor c(lineEdit->text());
            if (!c.isValid()) return;
            btn->setStyleSheet(QString("background-color: %1;").arg(c.name()));
        });
    };

    auto bindComboButtonSync = [this](QPushButton* btn, QComboBox* combo, QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB) {
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [btn, spinR, spinG, spinB](int) {
            btn->setStyleSheet(QString("background-color: %1;").arg(QColor(spinR->value(), spinG->value(), spinB->value()).name()));
        });
    };




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
    initSliderSync(ui->spinMetroidHudMatchStatusX, ui->inputMetroidHudMatchStatusX, ui->labelMetroidHudMatchStatusX, instcfg.GetInt("Metroid.Visual.HudMatchStatusX"));
    initSliderSync(ui->spinMetroidHudMatchStatusY, ui->inputMetroidHudMatchStatusY, ui->labelMetroidHudMatchStatusY, instcfg.GetInt("Metroid.Visual.HudMatchStatusY"));
    ui->spinMetroidHudMatchStatusLabelOfsX->setValue(instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsX"));
    ui->spinMetroidHudMatchStatusLabelOfsY->setValue(instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsY"));
    ui->comboMetroidHudMatchStatusLabelPos->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelPos"));
    initSliderSync(ui->spinMetroidHudMatchStatusLabelOfsX, ui->inputMetroidHudMatchStatusLabelOfsX, ui->labelMetroidHudMatchStatusLabelOfsX, instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsX"));
    initSliderSync(ui->spinMetroidHudMatchStatusLabelOfsY, ui->inputMetroidHudMatchStatusLabelOfsY, ui->labelMetroidHudMatchStatusLabelOfsY, instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsY"));
    ui->leMetroidHudMatchStatusLabelPoints->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudMatchStatusLabelPoints")));
    ui->leMetroidHudMatchStatusLabelOctoliths->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudMatchStatusLabelOctoliths")));
    ui->leMetroidHudMatchStatusLabelLives->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudMatchStatusLabelLives")));
    ui->leMetroidHudMatchStatusLabelRingTime->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudMatchStatusLabelRingTime")));
    ui->leMetroidHudMatchStatusLabelPrimeTime->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudMatchStatusLabelPrimeTime")));

    // Match Status colors — QPushButton color pickers
    setupColorButton(ui->btnMetroidHudMatchStatusColor,
        "Metroid.Visual.HudMatchStatusColorR", "Metroid.Visual.HudMatchStatusColorG", "Metroid.Visual.HudMatchStatusColorB",
        ui->comboMetroidHudMatchStatusColor, ui->leMetroidHudMatchStatusColorCode,
        ui->spinMetroidHudMatchStatusColorR, ui->spinMetroidHudMatchStatusColorG, ui->spinMetroidHudMatchStatusColorB, 20);
    setupColorButton(ui->btnMetroidHudMatchStatusLabelColor,
        "Metroid.Visual.HudMatchStatusLabelColorR", "Metroid.Visual.HudMatchStatusLabelColorG", "Metroid.Visual.HudMatchStatusLabelColorB",
        ui->comboMetroidHudMatchStatusLabelColor, ui->leMetroidHudMatchStatusLabelColorCode,
        ui->spinMetroidHudMatchStatusLabelColorR, ui->spinMetroidHudMatchStatusLabelColorG, ui->spinMetroidHudMatchStatusLabelColorB, 21);
    setupColorButton(ui->btnMetroidHudMatchStatusValueColor,
        "Metroid.Visual.HudMatchStatusValueColorR", "Metroid.Visual.HudMatchStatusValueColorG", "Metroid.Visual.HudMatchStatusValueColorB",
        ui->comboMetroidHudMatchStatusValueColor, ui->leMetroidHudMatchStatusValueColorCode,
        ui->spinMetroidHudMatchStatusValueColorR, ui->spinMetroidHudMatchStatusValueColorG, ui->spinMetroidHudMatchStatusValueColorB, 21);
    setupColorButton(ui->btnMetroidHudMatchStatusSepColor,
        "Metroid.Visual.HudMatchStatusSepColorR", "Metroid.Visual.HudMatchStatusSepColorG", "Metroid.Visual.HudMatchStatusSepColorB",
        ui->comboMetroidHudMatchStatusSepColor, ui->leMetroidHudMatchStatusSepColorCode,
        ui->spinMetroidHudMatchStatusSepColorR, ui->spinMetroidHudMatchStatusSepColorG, ui->spinMetroidHudMatchStatusSepColorB, 21);
    setupColorButton(ui->btnMetroidHudMatchStatusGoalColor,
        "Metroid.Visual.HudMatchStatusGoalColorR", "Metroid.Visual.HudMatchStatusGoalColorG", "Metroid.Visual.HudMatchStatusGoalColorB",
        ui->comboMetroidHudMatchStatusGoalColor, ui->leMetroidHudMatchStatusGoalColorCode,
        ui->spinMetroidHudMatchStatusGoalColorR, ui->spinMetroidHudMatchStatusGoalColorG, ui->spinMetroidHudMatchStatusGoalColorB, 21);
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
        int idx = 20; // Custom
        for (int i = 0; i < 20; i++) {
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
        if (idx < 0 || idx >= 20) return;
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
        int idx = 21; // Custom
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
    bindHexButtonSync(ui->btnMetroidHudMatchStatusColor, ui->leMetroidHudMatchStatusColorCode);
    bindComboButtonSync(ui->btnMetroidHudMatchStatusColor, ui->comboMetroidHudMatchStatusColor, ui->spinMetroidHudMatchStatusColorR, ui->spinMetroidHudMatchStatusColorG, ui->spinMetroidHudMatchStatusColorB);
    bindHexButtonSync(ui->btnMetroidHudMatchStatusLabelColor, ui->leMetroidHudMatchStatusLabelColorCode);
    bindComboButtonSync(ui->btnMetroidHudMatchStatusLabelColor, ui->comboMetroidHudMatchStatusLabelColor, ui->spinMetroidHudMatchStatusLabelColorR, ui->spinMetroidHudMatchStatusLabelColorG, ui->spinMetroidHudMatchStatusLabelColorB);
    bindHexButtonSync(ui->btnMetroidHudMatchStatusValueColor, ui->leMetroidHudMatchStatusValueColorCode);
    bindComboButtonSync(ui->btnMetroidHudMatchStatusValueColor, ui->comboMetroidHudMatchStatusValueColor, ui->spinMetroidHudMatchStatusValueColorR, ui->spinMetroidHudMatchStatusValueColorG, ui->spinMetroidHudMatchStatusValueColorB);
    bindHexButtonSync(ui->btnMetroidHudMatchStatusSepColor, ui->leMetroidHudMatchStatusSepColorCode);
    bindComboButtonSync(ui->btnMetroidHudMatchStatusSepColor, ui->comboMetroidHudMatchStatusSepColor, ui->spinMetroidHudMatchStatusSepColorR, ui->spinMetroidHudMatchStatusSepColorG, ui->spinMetroidHudMatchStatusSepColorB);
    bindHexButtonSync(ui->btnMetroidHudMatchStatusGoalColor, ui->leMetroidHudMatchStatusGoalColorCode);
    bindComboButtonSync(ui->btnMetroidHudMatchStatusGoalColor, ui->comboMetroidHudMatchStatusGoalColor, ui->spinMetroidHudMatchStatusGoalColorR, ui->spinMetroidHudMatchStatusGoalColorG, ui->spinMetroidHudMatchStatusGoalColorB);

    // Rank & Time HUD — load + color pickers
    ui->cbMetroidHudRankShow->setChecked(instcfg.GetBool("Metroid.Visual.HudRankShow"));
    initSliderSync(ui->spinMetroidHudRankX, ui->inputMetroidHudRankX, nullptr, instcfg.GetInt("Metroid.Visual.HudRankX"));
    initSliderSync(ui->spinMetroidHudRankY, ui->inputMetroidHudRankY, nullptr, instcfg.GetInt("Metroid.Visual.HudRankY"));
    ui->leMetroidHudRankPrefix->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudRankPrefix")));
    ui->cbMetroidHudRankShowOrdinal->setChecked(instcfg.GetBool("Metroid.Visual.HudRankShowOrdinal"));
    ui->leMetroidHudRankSuffix->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudRankSuffix")));
    ui->cbMetroidHudTimeLeftShow->setChecked(instcfg.GetBool("Metroid.Visual.HudTimeLeftShow"));
    initSliderSync(ui->spinMetroidHudTimeLeftX, ui->inputMetroidHudTimeLeftX, nullptr, instcfg.GetInt("Metroid.Visual.HudTimeLeftX"));
    initSliderSync(ui->spinMetroidHudTimeLeftY, ui->inputMetroidHudTimeLeftY, nullptr, instcfg.GetInt("Metroid.Visual.HudTimeLeftY"));
    ui->cbMetroidHudTimeLimitShow->setChecked(instcfg.GetBool("Metroid.Visual.HudTimeLimitShow"));
    initSliderSync(ui->spinMetroidHudTimeLimitX, ui->inputMetroidHudTimeLimitX, nullptr, instcfg.GetInt("Metroid.Visual.HudTimeLimitX"));
    initSliderSync(ui->spinMetroidHudTimeLimitY, ui->inputMetroidHudTimeLimitY, nullptr, instcfg.GetInt("Metroid.Visual.HudTimeLimitY"));
    // Simple 7-preset color picker helper (White/Green/YellowGreen/Yellow/Cyan/Pink/Red/Custom)
    {
        struct Clr { int r, g, b; };
        static const Clr kRT[] = {
            {255,255,255},{0,255,0},{127,255,0},{255,255,0},{0,200,255},{255,105,180},{255,0,0}
        };
        auto setupRtColor = [&](
            QPushButton* btn, QComboBox* combo, QLineEdit* le,
            QSpinBox* spR, QSpinBox* spG, QSpinBox* spB,
            const char* keyR, const char* keyG, const char* keyB)
        {
            setupColorButton(btn, keyR, keyG, keyB, combo, le, spR, spG, spB, 7);
            spR->setValue(instcfg.GetInt(keyR)); spG->setValue(instcfg.GetInt(keyG)); spB->setValue(instcfg.GetInt(keyB));
            int r = spR->value(), g = spG->value(), b = spB->value();
            le->setText(QString("#%1%2%3").arg(r,2,16,QChar('0')).arg(g,2,16,QChar('0')).arg(b,2,16,QChar('0')).toUpper());
            int idx = 7;
            for (int i = 0; i < 7; i++) { if (r==kRT[i].r && g==kRT[i].g && b==kRT[i].b) { idx=i; break; } }
            combo->setCurrentIndex(idx);
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int i) {
                if (i < 0 || i >= 7) return;
                spR->blockSignals(true); spG->blockSignals(true); spB->blockSignals(true);
                spR->setValue(kRT[i].r); spG->setValue(kRT[i].g); spB->setValue(kRT[i].b);
                le->setText(QString("#%1%2%3").arg(kRT[i].r,2,16,QChar('0')).arg(kRT[i].g,2,16,QChar('0')).arg(kRT[i].b,2,16,QChar('0')).toUpper());
                spR->blockSignals(false); spG->blockSignals(false); spB->blockSignals(false);
            });
            auto rgbCh = [=]() {
                combo->blockSignals(true); combo->setCurrentIndex(7); combo->blockSignals(false);
                le->setText(QString("#%1%2%3").arg(spR->value(),2,16,QChar('0')).arg(spG->value(),2,16,QChar('0')).arg(spB->value(),2,16,QChar('0')).toUpper());
            };
            connect(spR, QOverload<int>::of(&QSpinBox::valueChanged), this, rgbCh);
            connect(spG, QOverload<int>::of(&QSpinBox::valueChanged), this, rgbCh);
            connect(spB, QOverload<int>::of(&QSpinBox::valueChanged), this, rgbCh);
            connect(le, &QLineEdit::editingFinished, this, [=]() {
                QColor c(le->text()); if (!c.isValid()) return;
                spR->blockSignals(true); spG->blockSignals(true); spB->blockSignals(true);
                spR->setValue(c.red()); spG->setValue(c.green()); spB->setValue(c.blue());
                combo->blockSignals(true); combo->setCurrentIndex(7); combo->blockSignals(false);
                spR->blockSignals(false); spG->blockSignals(false); spB->blockSignals(false);
            });
            bindHexButtonSync(btn, le);
            bindComboButtonSync(btn, combo, spR, spG, spB);
        };
        setupRtColor(ui->btnMetroidHudRankColor, ui->comboMetroidHudRankColor, ui->leMetroidHudRankColorCode,
            ui->spinMetroidHudRankColorR, ui->spinMetroidHudRankColorG, ui->spinMetroidHudRankColorB,
            "Metroid.Visual.HudRankColorR", "Metroid.Visual.HudRankColorG", "Metroid.Visual.HudRankColorB");
        setupRtColor(ui->btnMetroidHudTimeLeftColor, ui->comboMetroidHudTimeLeftColor, ui->leMetroidHudTimeLeftColorCode,
            ui->spinMetroidHudTimeLeftColorR, ui->spinMetroidHudTimeLeftColorG, ui->spinMetroidHudTimeLeftColorB,
            "Metroid.Visual.HudTimeLeftColorR", "Metroid.Visual.HudTimeLeftColorG", "Metroid.Visual.HudTimeLeftColorB");
        setupRtColor(ui->btnMetroidHudTimeLimitColor, ui->comboMetroidHudTimeLimitColor, ui->leMetroidHudTimeLimitColorCode,
            ui->spinMetroidHudTimeLimitColorR, ui->spinMetroidHudTimeLimitColorG, ui->spinMetroidHudTimeLimitColorB,
            "Metroid.Visual.HudTimeLimitColorR", "Metroid.Visual.HudTimeLimitColorG", "Metroid.Visual.HudTimeLimitColorB");
    }

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

    // HP & Ammo section (inside Custom HUD tab)
    setupToggle(ui->btnToggleHpAmmo,    ui->sectionHpAmmo,    "HP & AMMO",      "Metroid.UI.SectionHpAmmo");
    setupToggle(ui->btnToggleHpPos,     ui->sectionHpPos,     "HP NUMBER POSITION",    "Metroid.UI.SectionHpPos");
    setupToggle(ui->btnToggleWpnPos,    ui->sectionWpnPos,    "AMMO NUMBER POSITION",  "Metroid.UI.SectionWpnPos");
    setupToggle(ui->btnToggleWpnIcon,   ui->sectionWpnIcon,   "WEAPON ICON",    "Metroid.UI.SectionWpnIcon");
    setupToggle(ui->btnToggleHpGauge,   ui->sectionHpGauge,   "HP GAUGE",       "Metroid.UI.SectionHpGauge");
    setupToggle(ui->btnToggleAmmoGauge, ui->sectionAmmoGauge, "AMMO GAUGE",     "Metroid.UI.SectionAmmoGauge");
    // Match Status section (inside Custom HUD tab)
    setupToggle(ui->btnToggleMatchStatus, ui->sectionMatchStatus, "MATCH STATUS HUD", "Metroid.UI.SectionMatchStatus");
    setupToggle(ui->btnToggleMatchStatusScore, ui->sectionMatchStatusScore, "SCORE", "Metroid.UI.SectionMatchStatusScore");
    setupToggle(ui->btnToggleRankTime,   ui->sectionRankTime,   "RANK & TIME HUD","Metroid.UI.SectionRankTime");
    setupToggle(ui->btnToggleRankHud,    ui->sectionRankHud,    "RANK",       "Metroid.UI.SectionRankHud");
    setupToggle(ui->btnToggleTimeLeftHud, ui->sectionTimeLeftHud, "TIME LEFT",  "Metroid.UI.SectionTimeLeftHud");
    setupToggle(ui->btnToggleTimeLimitHud, ui->sectionTimeLimitHud, "TIME LIMIT", "Metroid.UI.SectionTimeLimitHud");
    // HUD Radar section (inside Custom HUD tab)
    setupToggle(ui->btnToggleHudRadar,  ui->sectionHudRadar,  "HUD RADAR",      "Metroid.UI.SectionHudRadar");
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

    // HUD Radar

    // HUD Radar

    // Crosshair — Color (QPushButton color picker)
    setupColorButton(ui->btnMetroidCrosshairColor,
        "Metroid.Visual.CrosshairColorR", "Metroid.Visual.CrosshairColorG", "Metroid.Visual.CrosshairColorB",
        ui->comboMetroidCrosshairColor, ui->leMetroidCrosshairColorCode,
        ui->spinMetroidCrosshairR, ui->spinMetroidCrosshairG, ui->spinMetroidCrosshairB, 21);

    // Helper to init a slider and its value label
    auto initSlider = [this](QSlider* sl, QLabel* lbl, int val) {
        sl->setValue(val);
        lbl->setText(QString::number(val));

        QSpinBox* sb = this->findChild<QSpinBox*>(QString("input") + sl->objectName().mid(4));
        if (sb)
        {
            sb->blockSignals(true);
            sb->setValue(val);
            sb->blockSignals(false);

            QObject::connect(sl, &QSlider::valueChanged, sb, [sb](int v) {
                sb->blockSignals(true);
                sb->setValue(v);
                sb->blockSignals(false);
            });
            QObject::connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), sl, [sl](int v) {
                sl->blockSignals(true);
                sl->setValue(v);
                sl->blockSignals(false);
                emit sl->valueChanged(v);
            });
        }

        QObject::connect(sl, &QSlider::valueChanged, lbl, [lbl](int v) { lbl->setText(QString::number(v)); });
    };

    // HUD element positions
    initSliderSync(ui->spinMetroidHudHpX, ui->inputMetroidHudHpX, ui->labelMetroidHudHpX, instcfg.GetInt("Metroid.Visual.HudHpX"));
    initSliderSync(ui->spinMetroidHudHpY, ui->inputMetroidHudHpY, ui->labelMetroidHudHpY, instcfg.GetInt("Metroid.Visual.HudHpY"));
    ui->leMetroidHudHpPrefix->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudHpPrefix")));
    ui->comboMetroidHudHpAlign->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudHpAlign"));
    initSliderSync(ui->spinMetroidHudWeaponX, ui->inputMetroidHudWeaponX, ui->labelMetroidHudWeaponX, instcfg.GetInt("Metroid.Visual.HudWeaponX"));
    initSliderSync(ui->spinMetroidHudWeaponY, ui->inputMetroidHudWeaponY, ui->labelMetroidHudWeaponY, instcfg.GetInt("Metroid.Visual.HudWeaponY"));
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
    initSliderSync(ui->spinMetroidHudWeaponIconOffsetX, ui->inputMetroidHudWeaponIconOffsetX, ui->labelMetroidHudWeaponIconOffsetX, instcfg.GetInt("Metroid.Visual.HudWeaponIconOffsetX"));
    initSliderSync(ui->spinMetroidHudWeaponIconOffsetY, ui->inputMetroidHudWeaponIconOffsetY, ui->labelMetroidHudWeaponIconOffsetY, instcfg.GetInt("Metroid.Visual.HudWeaponIconOffsetY"));
    initSliderSync(ui->spinMetroidHudWeaponIconPosX, ui->inputMetroidHudWeaponIconPosX, ui->labelMetroidHudWeaponIconPosX, instcfg.GetInt("Metroid.Visual.HudWeaponIconPosX"));
    initSliderSync(ui->spinMetroidHudWeaponIconPosY, ui->inputMetroidHudWeaponIconPosY, ui->labelMetroidHudWeaponIconPosY, instcfg.GetInt("Metroid.Visual.HudWeaponIconPosY"));
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
        int idx = 21; // Custom
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
    bindHexButtonSync(ui->btnMetroidHudHpTextColor, ui->leMetroidHudHpTextColorCode);
    bindComboButtonSync(ui->btnMetroidHudHpTextColor, ui->comboMetroidHudHpTextColor, ui->spinMetroidHudHpTextColorR, ui->spinMetroidHudHpTextColorG, ui->spinMetroidHudHpTextColorB);
    bindHexButtonSync(ui->btnMetroidHudAmmoTextColor, ui->leMetroidHudAmmoTextColorCode);
    bindComboButtonSync(ui->btnMetroidHudAmmoTextColor, ui->comboMetroidHudAmmoTextColor, ui->spinMetroidHudAmmoTextColorR, ui->spinMetroidHudAmmoTextColorG, ui->spinMetroidHudAmmoTextColorB);

    setupColorButton(ui->btnMetroidHudHpTextColor,
        "Metroid.Visual.HudHpTextColorR", "Metroid.Visual.HudHpTextColorG", "Metroid.Visual.HudHpTextColorB",
        ui->comboMetroidHudHpTextColor, ui->leMetroidHudHpTextColorCode,
        ui->spinMetroidHudHpTextColorR, ui->spinMetroidHudHpTextColorG, ui->spinMetroidHudHpTextColorB, 20);
    setupColorButton(ui->btnMetroidHudAmmoTextColor,
        "Metroid.Visual.HudAmmoTextColorR", "Metroid.Visual.HudAmmoTextColorG", "Metroid.Visual.HudAmmoTextColorB",
        ui->comboMetroidHudAmmoTextColor, ui->leMetroidHudAmmoTextColorCode,
        ui->spinMetroidHudAmmoTextColorR, ui->spinMetroidHudAmmoTextColorG, ui->spinMetroidHudAmmoTextColorB, 20);

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
        if (idx < 0 || idx >= 20) return;
        ui->spinMetroidHudWeaponIconPosX->blockSignals(true);
        ui->spinMetroidHudWeaponIconPosY->blockSignals(true);
        ui->spinMetroidHudWeaponIconPosX->setValue(presets[idx].x);
        ui->spinMetroidHudWeaponIconPosY->setValue(presets[idx].y);
        ui->labelMetroidHudWeaponIconPosX->setText(QString::number(presets[idx].x));
        ui->labelMetroidHudWeaponIconPosY->setText(QString::number(presets[idx].y));
        ui->spinMetroidHudWeaponIconPosX->blockSignals(false);
        ui->spinMetroidHudWeaponIconPosY->blockSignals(false);
    });
    connect(ui->spinMetroidHudWeaponIconPosX, &QSlider::valueChanged, this, [this]() {
        ui->comboMetroidHudWeaponIconPosition->blockSignals(true);
        ui->comboMetroidHudWeaponIconPosition->setCurrentIndex(8);
        ui->comboMetroidHudWeaponIconPosition->blockSignals(false);
    });
    connect(ui->spinMetroidHudWeaponIconPosY, &QSlider::valueChanged, this, [this]() {
        ui->comboMetroidHudWeaponIconPosition->blockSignals(true);
        ui->comboMetroidHudWeaponIconPosition->setCurrentIndex(8);
        ui->comboMetroidHudWeaponIconPosition->blockSignals(false);
    });

    // Gauge settings — HP
    ui->cbMetroidHudHpGauge->setChecked(instcfg.GetBool("Metroid.Visual.HudHpGauge"));
    ui->comboMetroidHudHpGaugeOrientation->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudHpGaugeOrientation"));
    initSliderSync(ui->spinMetroidHudHpGaugeLength, ui->inputMetroidHudHpGaugeLength, ui->labelMetroidHudHpGaugeLength, instcfg.GetInt("Metroid.Visual.HudHpGaugeLength"));
    initSliderSync(ui->spinMetroidHudHpGaugeWidth,  ui->inputMetroidHudHpGaugeWidth,  ui->labelMetroidHudHpGaugeWidth,  instcfg.GetInt("Metroid.Visual.HudHpGaugeWidth"));
    initSliderSync(ui->spinMetroidHudHpGaugeOffsetX, ui->inputMetroidHudHpGaugeOffsetX, ui->labelMetroidHudHpGaugeOffsetX, instcfg.GetInt("Metroid.Visual.HudHpGaugeOffsetX"));
    initSliderSync(ui->spinMetroidHudHpGaugeOffsetY, ui->inputMetroidHudHpGaugeOffsetY, ui->labelMetroidHudHpGaugeOffsetY, instcfg.GetInt("Metroid.Visual.HudHpGaugeOffsetY"));
    ui->comboMetroidHudHpGaugeAnchor->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudHpGaugeAnchor"));
    ui->comboMetroidHudHpGaugePosMode->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudHpGaugePosMode"));
    initSliderSync(ui->spinMetroidHudHpGaugePosX, ui->inputMetroidHudHpGaugePosX, ui->labelMetroidHudHpGaugePosX, instcfg.GetInt("Metroid.Visual.HudHpGaugePosX"));
    initSliderSync(ui->spinMetroidHudHpGaugePosY, ui->inputMetroidHudHpGaugePosY, ui->labelMetroidHudHpGaugePosY, instcfg.GetInt("Metroid.Visual.HudHpGaugePosY"));
    ui->cbMetroidHudHpGaugeAutoColor->setChecked(instcfg.GetBool("Metroid.Visual.HudHpGaugeAutoColor"));
    // HP Gauge color — QPushButton color picker
    setupColorButton(ui->btnMetroidHudHpGaugeColor,
        "Metroid.Visual.HudHpGaugeColorR", "Metroid.Visual.HudHpGaugeColorG", "Metroid.Visual.HudHpGaugeColorB",
        ui->comboMetroidHudHpGaugeColor, ui->leMetroidHudHpGaugeColorCode,
        ui->spinMetroidHudHpGaugeColorR, ui->spinMetroidHudHpGaugeColorG, ui->spinMetroidHudHpGaugeColorB, 19);
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
        if (idx < 0 || idx >= 20) return;
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
    initSliderSync(ui->spinMetroidHudAmmoGaugeLength, ui->inputMetroidHudAmmoGaugeLength, ui->labelMetroidHudAmmoGaugeLength, instcfg.GetInt("Metroid.Visual.HudAmmoGaugeLength"));
    initSliderSync(ui->spinMetroidHudAmmoGaugeWidth,  ui->inputMetroidHudAmmoGaugeWidth,  ui->labelMetroidHudAmmoGaugeWidth,  instcfg.GetInt("Metroid.Visual.HudAmmoGaugeWidth"));
    initSliderSync(ui->spinMetroidHudAmmoGaugeOffsetX, ui->inputMetroidHudAmmoGaugeOffsetX, ui->labelMetroidHudAmmoGaugeOffsetX, instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetX"));
    initSliderSync(ui->spinMetroidHudAmmoGaugeOffsetY, ui->inputMetroidHudAmmoGaugeOffsetY, ui->labelMetroidHudAmmoGaugeOffsetY, instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetY"));
    ui->comboMetroidHudAmmoGaugeAnchor->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudAmmoGaugeAnchor"));
    ui->comboMetroidHudAmmoGaugePosMode->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudAmmoGaugePosMode"));
    initSliderSync(ui->spinMetroidHudAmmoGaugePosX, ui->inputMetroidHudAmmoGaugePosX, ui->labelMetroidHudAmmoGaugePosX, instcfg.GetInt("Metroid.Visual.HudAmmoGaugePosX"));
    initSliderSync(ui->spinMetroidHudAmmoGaugePosY, ui->inputMetroidHudAmmoGaugePosY, ui->labelMetroidHudAmmoGaugePosY, instcfg.GetInt("Metroid.Visual.HudAmmoGaugePosY"));
    // Ammo Gauge color — QPushButton color picker
    setupColorButton(ui->btnMetroidHudAmmoGaugeColor,
        "Metroid.Visual.HudAmmoGaugeColorR", "Metroid.Visual.HudAmmoGaugeColorG", "Metroid.Visual.HudAmmoGaugeColorB",
        ui->comboMetroidHudAmmoGaugeColor, ui->leMetroidHudAmmoGaugeColorCode,
        ui->spinMetroidHudAmmoGaugeColorR, ui->spinMetroidHudAmmoGaugeColorG, ui->spinMetroidHudAmmoGaugeColorB, 19);
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
        if (idx < 0 || idx >= 20) return;
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
    bindHexButtonSync(ui->btnMetroidHudHpGaugeColor, ui->leMetroidHudHpGaugeColorCode);
    bindComboButtonSync(ui->btnMetroidHudHpGaugeColor, ui->comboMetroidHudHpGaugeColor, ui->spinMetroidHudHpGaugeColorR, ui->spinMetroidHudHpGaugeColorG, ui->spinMetroidHudHpGaugeColorB);
    bindHexButtonSync(ui->btnMetroidHudAmmoGaugeColor, ui->leMetroidHudAmmoGaugeColorCode);
    bindComboButtonSync(ui->btnMetroidHudAmmoGaugeColor, ui->comboMetroidHudAmmoGaugeColor, ui->spinMetroidHudAmmoGaugeColorR, ui->spinMetroidHudAmmoGaugeColorG, ui->spinMetroidHudAmmoGaugeColorB);

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
        ui->labelMetroidHudHpX->setText(QString::number(presets[idx].x));
        ui->labelMetroidHudHpY->setText(QString::number(presets[idx].y));
        ui->spinMetroidHudHpX->blockSignals(false);
        ui->spinMetroidHudHpY->blockSignals(false);
    });
    // HP X/Y manual change → switch to Custom
    connect(ui->spinMetroidHudHpX, &QSlider::valueChanged, this, [this]() {
        ui->comboMetroidHudHpPosition->blockSignals(true);
        ui->comboMetroidHudHpPosition->setCurrentIndex(8);
        ui->comboMetroidHudHpPosition->blockSignals(false);
    });
    connect(ui->spinMetroidHudHpY, &QSlider::valueChanged, this, [this]() {
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
        ui->labelMetroidHudWeaponX->setText(QString::number(presets[idx].x));
        ui->labelMetroidHudWeaponY->setText(QString::number(presets[idx].y));
        ui->spinMetroidHudWeaponX->blockSignals(false);
        ui->spinMetroidHudWeaponY->blockSignals(false);
    });
    // Weapon X/Y manual change → switch to Custom
    connect(ui->spinMetroidHudWeaponX, &QSlider::valueChanged, this, [this]() {
        ui->comboMetroidHudWeaponPosition->blockSignals(true);
        ui->comboMetroidHudWeaponPosition->setCurrentIndex(8);
        ui->comboMetroidHudWeaponPosition->blockSignals(false);
    });
    connect(ui->spinMetroidHudWeaponY, &QSlider::valueChanged, this, [this]() {
        ui->comboMetroidHudWeaponPosition->blockSignals(true);
        ui->comboMetroidHudWeaponPosition->setCurrentIndex(8);
        ui->comboMetroidHudWeaponPosition->blockSignals(false);
    });

    // HUD Radar

    // HUD Radar

    // Crosshair — Color (continued)
    int chR = instcfg.GetInt("Metroid.Visual.CrosshairColorR");
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
    bindHexButtonSync(ui->btnMetroidCrosshairColor, ui->leMetroidCrosshairColorCode);
    bindComboButtonSync(ui->btnMetroidCrosshairColor, ui->comboMetroidCrosshairColor, ui->spinMetroidCrosshairR, ui->spinMetroidCrosshairG, ui->spinMetroidCrosshairB);


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

    // HUD Radar

    // HUD Radar

    // Crosshair — General
    ui->cbMetroidCrosshairOutline->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairOutline"));
    ui->spinMetroidCrosshairOutlineOpacity->setValue(instcfg.GetDouble("Metroid.Visual.CrosshairOutlineOpacity"));
    {
        int olThick = instcfg.GetInt("Metroid.Visual.CrosshairOutlineThickness");
        initSliderSync(ui->sliderMetroidCrosshairOutlineThickness, ui->spinMetroidCrosshairOutlineThickness, ui->labelMetroidCrosshairOutlineThickness, olThick > 0 ? olThick : 1);
    }
    ui->cbMetroidCrosshairCenterDot->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairCenterDot"));
    ui->spinMetroidCrosshairDotOpacity->setValue(instcfg.GetDouble("Metroid.Visual.CrosshairDotOpacity"));
    {
        int dotThick = instcfg.GetInt("Metroid.Visual.CrosshairDotThickness");
        initSliderSync(ui->sliderMetroidCrosshairDotThickness, ui->spinMetroidCrosshairDotThickness, ui->labelMetroidCrosshairDotThickness, dotThick > 0 ? dotThick : 1);
    }
    ui->cbMetroidCrosshairTStyle->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairTStyle"));

    // HUD Radar

    // HUD Radar

    // Crosshair — Inner Lines
    ui->cbMetroidCrosshairInnerShow->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairInnerShow"));
    ui->spinMetroidCrosshairInnerOpacity->setValue(instcfg.GetDouble("Metroid.Visual.CrosshairInnerOpacity"));
    initSliderSync(ui->sliderMetroidCrosshairInnerLengthX, ui->spinMetroidCrosshairInnerLengthX, ui->labelMetroidCrosshairInnerLengthX, instcfg.GetInt("Metroid.Visual.CrosshairInnerLengthX"));
    initSliderSync(ui->sliderMetroidCrosshairInnerLengthY, ui->spinMetroidCrosshairInnerLengthY, ui->labelMetroidCrosshairInnerLengthY, instcfg.GetInt("Metroid.Visual.CrosshairInnerLengthY"));
    {
        int innerThick = instcfg.GetInt("Metroid.Visual.CrosshairInnerThickness");
        initSliderSync(ui->sliderMetroidCrosshairInnerThickness, ui->spinMetroidCrosshairInnerThickness, ui->labelMetroidCrosshairInnerThickness, innerThick > 0 ? innerThick : 1);
    }
    initSliderSync(ui->sliderMetroidCrosshairInnerOffset, ui->spinMetroidCrosshairInnerOffset, ui->labelMetroidCrosshairInnerOffset, instcfg.GetInt("Metroid.Visual.CrosshairInnerOffset"));
    ui->cbMetroidCrosshairInnerLinkXY->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairInnerLinkXY"));

    // HUD Radar

    // HUD Radar

    // Crosshair — Outer Lines
    ui->cbMetroidCrosshairOuterShow->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairOuterShow"));
    ui->spinMetroidCrosshairOuterOpacity->setValue(instcfg.GetDouble("Metroid.Visual.CrosshairOuterOpacity"));
    initSliderSync(ui->sliderMetroidCrosshairOuterLengthX, ui->spinMetroidCrosshairOuterLengthX, ui->labelMetroidCrosshairOuterLengthX, instcfg.GetInt("Metroid.Visual.CrosshairOuterLengthX"));
    initSliderSync(ui->sliderMetroidCrosshairOuterLengthY, ui->spinMetroidCrosshairOuterLengthY, ui->labelMetroidCrosshairOuterLengthY, instcfg.GetInt("Metroid.Visual.CrosshairOuterLengthY"));
    {
        int outerThick = instcfg.GetInt("Metroid.Visual.CrosshairOuterThickness");
        initSliderSync(ui->sliderMetroidCrosshairOuterThickness, ui->spinMetroidCrosshairOuterThickness, ui->labelMetroidCrosshairOuterThickness, outerThick > 0 ? outerThick : 1);
    }
    initSliderSync(ui->sliderMetroidCrosshairOuterOffset, ui->spinMetroidCrosshairOuterOffset, ui->labelMetroidCrosshairOuterOffset, instcfg.GetInt("Metroid.Visual.CrosshairOuterOffset"));
    ui->cbMetroidCrosshairOuterLinkXY->setChecked(instcfg.GetBool("Metroid.Visual.CrosshairOuterLinkXY"));

    // Link X/Y sync: when linked, changing X updates Y and vice versa
    connect(ui->spinMetroidCrosshairInnerLengthX, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (ui->cbMetroidCrosshairInnerLinkXY->isChecked()) {
            ui->spinMetroidCrosshairInnerLengthY->blockSignals(true);
            ui->spinMetroidCrosshairInnerLengthY->setValue(val);
            ui->spinMetroidCrosshairInnerLengthY->blockSignals(false);
            ui->sliderMetroidCrosshairInnerLengthY->blockSignals(true);
            ui->sliderMetroidCrosshairInnerLengthY->setValue(val);
            ui->sliderMetroidCrosshairInnerLengthY->blockSignals(false);
            ui->labelMetroidCrosshairInnerLengthY->setText(QString::number(val));
        }
    });
    connect(ui->spinMetroidCrosshairInnerLengthY, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (ui->cbMetroidCrosshairInnerLinkXY->isChecked()) {
            ui->spinMetroidCrosshairInnerLengthX->blockSignals(true);
            ui->spinMetroidCrosshairInnerLengthX->setValue(val);
            ui->spinMetroidCrosshairInnerLengthX->blockSignals(false);
            ui->sliderMetroidCrosshairInnerLengthX->blockSignals(true);
            ui->sliderMetroidCrosshairInnerLengthX->setValue(val);
            ui->sliderMetroidCrosshairInnerLengthX->blockSignals(false);
            ui->labelMetroidCrosshairInnerLengthX->setText(QString::number(val));
        }
    });
    connect(ui->spinMetroidCrosshairOuterLengthX, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (ui->cbMetroidCrosshairOuterLinkXY->isChecked()) {
            ui->spinMetroidCrosshairOuterLengthY->blockSignals(true);
            ui->spinMetroidCrosshairOuterLengthY->setValue(val);
            ui->spinMetroidCrosshairOuterLengthY->blockSignals(false);
            ui->sliderMetroidCrosshairOuterLengthY->blockSignals(true);
            ui->sliderMetroidCrosshairOuterLengthY->setValue(val);
            ui->sliderMetroidCrosshairOuterLengthY->blockSignals(false);
            ui->labelMetroidCrosshairOuterLengthY->setText(QString::number(val));
        }
    });
    connect(ui->spinMetroidCrosshairOuterLengthY, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (ui->cbMetroidCrosshairOuterLinkXY->isChecked()) {
            ui->spinMetroidCrosshairOuterLengthX->blockSignals(true);
            ui->spinMetroidCrosshairOuterLengthX->setValue(val);
            ui->spinMetroidCrosshairOuterLengthX->blockSignals(false);
            ui->sliderMetroidCrosshairOuterLengthX->blockSignals(true);
            ui->sliderMetroidCrosshairOuterLengthX->setValue(val);
            ui->sliderMetroidCrosshairOuterLengthX->blockSignals(false);
            ui->labelMetroidCrosshairOuterLengthX->setText(QString::number(val));
        }
    });

    // --- Live visual preview ---

    auto prvI = [&](QObject* w) {
        if (auto sb = qobject_cast<QSpinBox*>(w))
            connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, &MelonPrimeInputConfig::applyVisualPreview);
        else if (auto sl = qobject_cast<QSlider*>(w))
            connect(sl, &QSlider::valueChanged, this, [this](int) { applyVisualPreview(); });
    };
    auto prvSl = [&](QSlider* w) {
        connect(w, &QSlider::valueChanged, this, [this](int) { applyVisualPreview(); });
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
    prvSl(ui->spinMetroidHudMatchStatusX);         prvSl(ui->spinMetroidHudMatchStatusY);
    prvSl(ui->spinMetroidHudMatchStatusLabelOfsX); prvSl(ui->spinMetroidHudMatchStatusLabelOfsY);
    prvC(ui->comboMetroidHudMatchStatusLabelPos);
    prvE(ui->leMetroidHudMatchStatusLabelPoints);    prvE(ui->leMetroidHudMatchStatusLabelOctoliths);
    prvE(ui->leMetroidHudMatchStatusLabelLives);     prvE(ui->leMetroidHudMatchStatusLabelRingTime);
    prvE(ui->leMetroidHudMatchStatusLabelPrimeTime);
    // (color button clicks already call applyVisualPreview via setupColorButton)
    // HP/Weapon positions
    prvSl(ui->spinMetroidHudHpX);    prvSl(ui->spinMetroidHudHpY);    prvE(ui->leMetroidHudHpPrefix);    prvC(ui->comboMetroidHudHpAlign);
    prvB(ui->cbMetroidHudHpTextAutoColor); prvC(ui->comboMetroidHudHpTextColor);
    prvI(ui->spinMetroidHudHpTextColorR); prvI(ui->spinMetroidHudHpTextColorG); prvI(ui->spinMetroidHudHpTextColorB);
    prvSl(ui->spinMetroidHudWeaponX); prvSl(ui->spinMetroidHudWeaponY); prvE(ui->leMetroidHudAmmoPrefix); prvC(ui->comboMetroidHudAmmoAlign);
    prvC(ui->comboMetroidHudAmmoTextColor);
    prvI(ui->spinMetroidHudAmmoTextColorR); prvI(ui->spinMetroidHudAmmoTextColorG); prvI(ui->spinMetroidHudAmmoTextColorB);
    prvB(ui->cbMetroidHudWeaponIconShow);  prvC(ui->comboMetroidHudWeaponIconMode);
    prvSl(ui->spinMetroidHudWeaponIconOffsetX); prvSl(ui->spinMetroidHudWeaponIconOffsetY);
    prvSl(ui->spinMetroidHudWeaponIconPosX);   prvSl(ui->spinMetroidHudWeaponIconPosY);
    prvC(ui->comboMetroidHudWeaponIconAnchorX); prvC(ui->comboMetroidHudWeaponIconAnchorY); prvB(ui->cbMetroidHudWeaponIconColorOverlay);
    // HP Gauge
    prvB(ui->cbMetroidHudHpGauge);
    prvC(ui->comboMetroidHudHpGaugeOrientation);
    prvSl(ui->spinMetroidHudHpGaugeLength); prvSl(ui->spinMetroidHudHpGaugeWidth);
    prvSl(ui->spinMetroidHudHpGaugeOffsetX); prvSl(ui->spinMetroidHudHpGaugeOffsetY);
    prvC(ui->comboMetroidHudHpGaugeAnchor); prvC(ui->comboMetroidHudHpGaugePosMode);
    prvSl(ui->spinMetroidHudHpGaugePosX);   prvSl(ui->spinMetroidHudHpGaugePosY);
    prvB(ui->cbMetroidHudHpGaugeAutoColor);
    // (HP gauge color button clicks already call applyVisualPreview via setupColorButton)
    // Ammo Gauge
    prvB(ui->cbMetroidHudAmmoGauge);
    prvC(ui->comboMetroidHudAmmoGaugeOrientation);
    prvSl(ui->spinMetroidHudAmmoGaugeLength); prvSl(ui->spinMetroidHudAmmoGaugeWidth);
    prvSl(ui->spinMetroidHudAmmoGaugeOffsetX); prvSl(ui->spinMetroidHudAmmoGaugeOffsetY);
    prvC(ui->comboMetroidHudAmmoGaugeAnchor); prvC(ui->comboMetroidHudAmmoGaugePosMode);
    prvSl(ui->spinMetroidHudAmmoGaugePosX);   prvSl(ui->spinMetroidHudAmmoGaugePosY);
    // (Ammo gauge color button clicks already call applyVisualPreview via setupColorButton)
    // HUD Radar
    prvB(ui->cbMetroidBtmOverlayEnable);
    prvSl(ui->spinMetroidBtmOverlayDstX); prvSl(ui->spinMetroidBtmOverlayDstY); prvSl(ui->spinMetroidBtmOverlayDstSize);
    prvD(ui->spinMetroidBtmOverlayOpacity);
    prvSl(ui->spinMetroidBtmOverlaySrcRadius);
    // Match Status colors
    prvC(ui->comboMetroidHudMatchStatusColor);
    prvI(ui->spinMetroidHudMatchStatusColorR); prvI(ui->spinMetroidHudMatchStatusColorG); prvI(ui->spinMetroidHudMatchStatusColorB);
    prvE(ui->leMetroidHudMatchStatusColorCode);
    prvC(ui->comboMetroidHudMatchStatusLabelColor); prvE(ui->leMetroidHudMatchStatusLabelColorCode);
    prvI(ui->spinMetroidHudMatchStatusLabelColorR); prvI(ui->spinMetroidHudMatchStatusLabelColorG); prvI(ui->spinMetroidHudMatchStatusLabelColorB);
    prvC(ui->comboMetroidHudMatchStatusValueColor); prvE(ui->leMetroidHudMatchStatusValueColorCode);
    prvI(ui->spinMetroidHudMatchStatusValueColorR); prvI(ui->spinMetroidHudMatchStatusValueColorG); prvI(ui->spinMetroidHudMatchStatusValueColorB);
    prvC(ui->comboMetroidHudMatchStatusSepColor); prvE(ui->leMetroidHudMatchStatusSepColorCode);
    prvI(ui->spinMetroidHudMatchStatusSepColorR); prvI(ui->spinMetroidHudMatchStatusSepColorG); prvI(ui->spinMetroidHudMatchStatusSepColorB);
    prvC(ui->comboMetroidHudMatchStatusGoalColor); prvE(ui->leMetroidHudMatchStatusGoalColorCode);
    prvI(ui->spinMetroidHudMatchStatusGoalColorR); prvI(ui->spinMetroidHudMatchStatusGoalColorG); prvI(ui->spinMetroidHudMatchStatusGoalColorB);
    // Crosshair
    // (Crosshair color button clicks already call applyVisualPreview via setupColorButton)
    prvB(ui->cbMetroidCrosshairOutline);
    prvD(ui->spinMetroidCrosshairOutlineOpacity);
    prvSl(ui->sliderMetroidCrosshairOutlineThickness);
    prvB(ui->cbMetroidCrosshairCenterDot);
    prvD(ui->spinMetroidCrosshairDotOpacity);
    prvSl(ui->sliderMetroidCrosshairDotThickness);
    prvB(ui->cbMetroidCrosshairTStyle);
    prvB(ui->cbMetroidCrosshairInnerShow);
    prvD(ui->spinMetroidCrosshairInnerOpacity);
    prvSl(ui->sliderMetroidCrosshairInnerLengthX); prvSl(ui->sliderMetroidCrosshairInnerLengthY);
    prvSl(ui->sliderMetroidCrosshairInnerThickness); prvSl(ui->sliderMetroidCrosshairInnerOffset);
    prvB(ui->cbMetroidCrosshairInnerLinkXY);
    prvB(ui->cbMetroidCrosshairOuterShow);
    prvD(ui->spinMetroidCrosshairOuterOpacity);
    prvSl(ui->sliderMetroidCrosshairOuterLengthX); prvSl(ui->sliderMetroidCrosshairOuterLengthY);
    prvSl(ui->sliderMetroidCrosshairOuterThickness); prvSl(ui->sliderMetroidCrosshairOuterOffset);
    prvB(ui->cbMetroidCrosshairOuterLinkXY);

    // HUD Radar
    ui->cbMetroidBtmOverlayEnable->setChecked(instcfg.GetBool("Metroid.Visual.BtmOverlayEnable"));
    initSliderSync(ui->spinMetroidBtmOverlayDstX,    ui->inputMetroidBtmOverlayDstX,    ui->labelMetroidBtmOverlayDstX,    instcfg.GetInt("Metroid.Visual.BtmOverlayDstX"));
    initSliderSync(ui->spinMetroidBtmOverlayDstY,    ui->inputMetroidBtmOverlayDstY,    ui->labelMetroidBtmOverlayDstY,    instcfg.GetInt("Metroid.Visual.BtmOverlayDstY"));
    initSliderSync(ui->spinMetroidBtmOverlayDstSize, ui->inputMetroidBtmOverlayDstSize, ui->labelMetroidBtmOverlayDstSize, instcfg.GetInt("Metroid.Visual.BtmOverlayDstSize"));
    ui->spinMetroidBtmOverlayOpacity->setValue(instcfg.GetDouble("Metroid.Visual.BtmOverlayOpacity"));
    ui->sliderMetroidBtmOverlayOpacity->setValue(qRound(instcfg.GetDouble("Metroid.Visual.BtmOverlayOpacity") * 100));
    initSliderSync(ui->spinMetroidBtmOverlaySrcRadius,  ui->inputMetroidBtmOverlaySrcRadius,  ui->labelMetroidBtmOverlaySrcRadius,  instcfg.GetInt("Metroid.Visual.BtmOverlaySrcRadius"));

    // Connect radar preview updates
    connect(ui->cbMetroidBtmOverlayEnable, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) { updateRadarPreview(); });
    connect(ui->spinMetroidBtmOverlayDstX,    &QSlider::valueChanged, this, [this](int) { updateRadarPreview(); });
    connect(ui->spinMetroidBtmOverlayDstY,    &QSlider::valueChanged, this, [this](int) { updateRadarPreview(); });
    connect(ui->spinMetroidBtmOverlayDstSize, &QSlider::valueChanged, this, [this](int) { updateRadarPreview(); });
    connect(ui->spinMetroidBtmOverlayOpacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        ui->sliderMetroidBtmOverlayOpacity->blockSignals(true);
        ui->sliderMetroidBtmOverlayOpacity->setValue(qRound(val * 100));
        ui->sliderMetroidBtmOverlayOpacity->blockSignals(false);
        updateRadarPreview();
    });
    connect(ui->sliderMetroidBtmOverlayOpacity, &QSlider::valueChanged, this, [this](int val) {
        ui->spinMetroidBtmOverlayOpacity->blockSignals(true);
        ui->spinMetroidBtmOverlayOpacity->setValue(val / 100.0);
        ui->spinMetroidBtmOverlayOpacity->blockSignals(false);
        updateRadarPreview();
    });

    auto syncColorPickerUi = [this](QPushButton* btn,
                                    const char* cfgR, const char* cfgG, const char* cfgB,
                                    QComboBox* combo, QLineEdit* lineEdit,
                                    QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB,
                                    int customIndex)
    {
        auto refreshButton = [btn, spinR, spinG, spinB]() {
            btn->setStyleSheet(QString("background-color: %1;").arg(QColor(spinR->value(), spinG->value(), spinB->value()).name()));
        };

        connect(btn, &QPushButton::clicked, this, [=]() {
            Config::Table& cfg = emuInstance->getLocalConfig();
            int r = cfg.GetInt(cfgR);
            int g = cfg.GetInt(cfgG);
            int b = cfg.GetInt(cfgB);

            spinR->blockSignals(true); spinG->blockSignals(true); spinB->blockSignals(true);
            spinR->setValue(r); spinG->setValue(g); spinB->setValue(b);
            spinR->blockSignals(false); spinG->blockSignals(false); spinB->blockSignals(false);

            if (lineEdit)
                lineEdit->setText(QString("#%1%2%3").arg(r,2,16,QChar('0')).arg(g,2,16,QChar('0')).arg(b,2,16,QChar('0')).toUpper());
            if (combo)
            {
                combo->blockSignals(true);
                combo->setCurrentIndex(customIndex);
                combo->blockSignals(false);
            }
            refreshButton();
            applyVisualPreview();
        });

        auto onRgbChanged = [=]() {
            if (lineEdit)
                lineEdit->setText(QString("#%1%2%3").arg(spinR->value(),2,16,QChar('0')).arg(spinG->value(),2,16,QChar('0')).arg(spinB->value(),2,16,QChar('0')).toUpper());
            refreshButton();
        };
        connect(spinR, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int) { onRgbChanged(); });
        connect(spinG, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int) { onRgbChanged(); });
        connect(spinB, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int) { onRgbChanged(); });
        refreshButton();
    };


    snapshotVisualConfig();
    updateRadarPreview();
    updateCrosshairPreview();
    updateHpAmmoPreview();
    updateMatchStatusPreview();

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

void MelonPrimeInputConfig::updateRadarPreview()
{
    QWidget* preview = ui->widgetRadarPreview;
    if (!preview) return;

    const int pw = preview->width();
    const int ph = preview->height();

    QPixmap pixmap(pw, ph);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Draw top screen rectangle (256x192 scaled to fit preview)
    const float dsW = 256.0f, dsH = 192.0f;
    const float scale = std::min(static_cast<float>(pw) / dsW, static_cast<float>(ph) / dsH);
    const float offX = (pw - dsW * scale) / 2.0f;
    const float offY = (ph - dsH * scale) / 2.0f;

    QRectF screenRect(offX, offY, dsW * scale, dsH * scale);
    p.setPen(QPen(QColor(100, 100, 100), 1));
    p.setBrush(QColor(30, 30, 40));
    p.drawRect(screenRect);

    // Draw "TOP SCREEN" label
    p.setPen(QColor(80, 80, 80));
    p.setFont(QFont("sans-serif", 8));
    p.drawText(screenRect, Qt::AlignCenter, "TOP SCREEN");

    // Draw radar circle overlay
    bool enabled = ui->cbMetroidBtmOverlayEnable->isChecked();
    int dstX = ui->spinMetroidBtmOverlayDstX->value();
    int dstY = ui->spinMetroidBtmOverlayDstY->value();
    int dstSize = ui->spinMetroidBtmOverlayDstSize->value();
    double opacity = ui->spinMetroidBtmOverlayOpacity->value();

    if (enabled)
    {
        float cx = offX + (dstX + dstSize / 2.0f) * scale;
        float cy = offY + (dstY + dstSize / 2.0f) * scale;
        float r = (dstSize / 2.0f) * scale;

        QColor fillColor(0, 200, 80, static_cast<int>(opacity * 180));
        QColor borderColor(0, 255, 100, static_cast<int>(opacity * 255));

        p.setPen(QPen(borderColor, 2));
        p.setBrush(fillColor);
        p.drawEllipse(QPointF(cx, cy), r, r);

        // Cross at center
        p.setPen(QPen(borderColor, 1));
        p.drawLine(QPointF(cx - 4, cy), QPointF(cx + 4, cy));
        p.drawLine(QPointF(cx, cy - 4), QPointF(cx, cy + 4));
    }

    p.end();

    // Set as background of the preview widget
    QPalette pal = preview->palette();
    pal.setBrush(QPalette::Window, pixmap);
    preview->setPalette(pal);
    preview->setAutoFillBackground(true);
    preview->update();
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

    Config::Table& instcfg = emuInstance->getLocalConfig();

    auto sI  = [&](const char* k, QObject* w) {
        if (auto sb = qobject_cast<QSpinBox*>(w)) s[k] = sb->value();
        else if (auto sl = qobject_cast<QSlider*>(w)) s[k] = sl->value();
    };
    auto sSl = [&](const char* k, QSlider* w)         { s[k] = w->value(); };
    auto sD  = [&](const char* k, QDoubleSpinBox* w)  { s[k] = w->value(); };
    auto sB  = [&](const char* k, QCheckBox* w)       { s[k] = w->isChecked(); };
    auto sC  = [&](const char* k, QComboBox* w)       { s[k] = w->currentIndex(); };
    auto sE  = [&](const char* k, QLineEdit* w)       { s[k] = w->text(); };
    // Snapshot a color from config (since colors are now stored directly in config via button pickers)
    auto sCfgI = [&](const char* snapKey, const char* cfgKey) { s[snapKey] = instcfg.GetInt(cfgKey); };

    sB("cCustomHud",       ui->cbMetroidEnableCustomHud);
    sB("cAspectRatio",     ui->cbMetroidInGameAspectRatio);
    sC("cAspectRatioMode", ui->comboMetroidInGameAspectRatioMode);
    // Match Status
    sB("cMatchShow",  ui->cbMetroidHudMatchStatusShow);
    sSl("sMatchX",     ui->spinMetroidHudMatchStatusX);
    sSl("sMatchY",     ui->spinMetroidHudMatchStatusY);
    sSl("sMatchLOfsX", ui->spinMetroidHudMatchStatusLabelOfsX);
    sSl("sMatchLOfsY", ui->spinMetroidHudMatchStatusLabelOfsY);
    sC("cMatchLPos",  ui->comboMetroidHudMatchStatusLabelPos);
    sE("eMatchLP",    ui->leMetroidHudMatchStatusLabelPoints);
    sE("eMatchLO",    ui->leMetroidHudMatchStatusLabelOctoliths);
    sE("eMatchLL",    ui->leMetroidHudMatchStatusLabelLives);
    sE("eMatchLR",    ui->leMetroidHudMatchStatusLabelRingTime);
    sE("eMatchLPT",   ui->leMetroidHudMatchStatusLabelPrimeTime);
    sC("cMatchClr", ui->comboMetroidHudMatchStatusColor);
    sE("eMatchClr", ui->leMetroidHudMatchStatusColorCode);
    sI("sMatchClrSpinR", ui->spinMetroidHudMatchStatusColorR);
    sI("sMatchClrSpinG", ui->spinMetroidHudMatchStatusColorG);
    sI("sMatchClrSpinB", ui->spinMetroidHudMatchStatusColorB);
    sC("cMatchLblClr", ui->comboMetroidHudMatchStatusLabelColor);
    sE("eMatchLblClr", ui->leMetroidHudMatchStatusLabelColorCode);
    sI("sMatchLblClrSpinR", ui->spinMetroidHudMatchStatusLabelColorR);
    sI("sMatchLblClrSpinG", ui->spinMetroidHudMatchStatusLabelColorG);
    sI("sMatchLblClrSpinB", ui->spinMetroidHudMatchStatusLabelColorB);
    sC("cMatchValClr", ui->comboMetroidHudMatchStatusValueColor);
    sE("eMatchValClr", ui->leMetroidHudMatchStatusValueColorCode);
    sI("sMatchValClrSpinR", ui->spinMetroidHudMatchStatusValueColorR);
    sI("sMatchValClrSpinG", ui->spinMetroidHudMatchStatusValueColorG);
    sI("sMatchValClrSpinB", ui->spinMetroidHudMatchStatusValueColorB);
    sC("cMatchSepClr", ui->comboMetroidHudMatchStatusSepColor);
    sE("eMatchSepClr", ui->leMetroidHudMatchStatusSepColorCode);
    sI("sMatchSepClrSpinR", ui->spinMetroidHudMatchStatusSepColorR);
    sI("sMatchSepClrSpinG", ui->spinMetroidHudMatchStatusSepColorG);
    sI("sMatchSepClrSpinB", ui->spinMetroidHudMatchStatusSepColorB);
    sC("cMatchGolClr", ui->comboMetroidHudMatchStatusGoalColor);
    sE("eMatchGolClr", ui->leMetroidHudMatchStatusGoalColorCode);
    sI("sMatchGolClrSpinR", ui->spinMetroidHudMatchStatusGoalColorR);
    sI("sMatchGolClrSpinG", ui->spinMetroidHudMatchStatusGoalColorG);
    sI("sMatchGolClrSpinB", ui->spinMetroidHudMatchStatusGoalColorB);
    // Match Status colors (from config)
    sCfgI("sMatchClrR",  "Metroid.Visual.HudMatchStatusColorR");
    sCfgI("sMatchClrG",  "Metroid.Visual.HudMatchStatusColorG");
    sCfgI("sMatchClrB",  "Metroid.Visual.HudMatchStatusColorB");
    sCfgI("sMatchLblClrR", "Metroid.Visual.HudMatchStatusLabelColorR");
    sCfgI("sMatchLblClrG", "Metroid.Visual.HudMatchStatusLabelColorG");
    sCfgI("sMatchLblClrB", "Metroid.Visual.HudMatchStatusLabelColorB");
    sCfgI("sMatchValClrR", "Metroid.Visual.HudMatchStatusValueColorR");
    sCfgI("sMatchValClrG", "Metroid.Visual.HudMatchStatusValueColorG");
    sCfgI("sMatchValClrB", "Metroid.Visual.HudMatchStatusValueColorB");
    sCfgI("sMatchSepClrR", "Metroid.Visual.HudMatchStatusSepColorR");
    sCfgI("sMatchSepClrG", "Metroid.Visual.HudMatchStatusSepColorG");
    sCfgI("sMatchSepClrB", "Metroid.Visual.HudMatchStatusSepColorB");
    sCfgI("sMatchGolClrR", "Metroid.Visual.HudMatchStatusGoalColorR");
    sCfgI("sMatchGolClrG", "Metroid.Visual.HudMatchStatusGoalColorG");
    sCfgI("sMatchGolClrB", "Metroid.Visual.HudMatchStatusGoalColorB");
    // HP/Weapon
    sSl("sHpX",  ui->spinMetroidHudHpX);       sSl("sHpY",  ui->spinMetroidHudHpY);
    sE("eHpPfx", ui->leMetroidHudHpPrefix);
    sC("cHpAlign", ui->comboMetroidHudHpAlign);
    sB("cHpTxtAuto", ui->cbMetroidHudHpTextAutoColor);
    sC("cHpTxtClr", ui->comboMetroidHudHpTextColor);
    sI("sHpTxtClrR", ui->spinMetroidHudHpTextColorR);
    sI("sHpTxtClrG", ui->spinMetroidHudHpTextColorG);
    sI("sHpTxtClrB", ui->spinMetroidHudHpTextColorB);
    sE("eHpTxtClr",  ui->leMetroidHudHpTextColorCode);
    sCfgI("sCfgHpTxtClrR", "Metroid.Visual.HudHpTextColorR");
    sCfgI("sCfgHpTxtClrG", "Metroid.Visual.HudHpTextColorG");
    sCfgI("sCfgHpTxtClrB", "Metroid.Visual.HudHpTextColorB");
    sI("sWpnX", ui->spinMetroidHudWeaponX);    sI("sWpnY", ui->spinMetroidHudWeaponY);
    sE("eAmmoPfx", ui->leMetroidHudAmmoPrefix);
    sC("cAmmoAlign", ui->comboMetroidHudAmmoAlign);
    sC("cAmmoTxtClr", ui->comboMetroidHudAmmoTextColor);
    sI("sAmmoTxtClrR", ui->spinMetroidHudAmmoTextColorR);
    sI("sAmmoTxtClrG", ui->spinMetroidHudAmmoTextColorG);
    sI("sAmmoTxtClrB", ui->spinMetroidHudAmmoTextColorB);
    sE("eAmmoTxtClr",  ui->leMetroidHudAmmoTextColorCode);
    sCfgI("sCfgAmmoTxtClrR", "Metroid.Visual.HudAmmoTextColorR");
    sCfgI("sCfgAmmoTxtClrG", "Metroid.Visual.HudAmmoTextColorG");
    sCfgI("sCfgAmmoTxtClrB", "Metroid.Visual.HudAmmoTextColorB");
    sC("cHpPos",  ui->comboMetroidHudHpPosition);
    sC("cWpnPos", ui->comboMetroidHudWeaponPosition);
    sB("cWpnIconShow",  ui->cbMetroidHudWeaponIconShow);
    sC("cWpnIconMode",  ui->comboMetroidHudWeaponIconMode);
    sSl("sWpnIconOfsX",  ui->spinMetroidHudWeaponIconOffsetX);
    sSl("sWpnIconOfsY",  ui->spinMetroidHudWeaponIconOffsetY);
    sSl("sWpnIconPosX",   ui->spinMetroidHudWeaponIconPosX);
    sSl("sWpnIconPosY",   ui->spinMetroidHudWeaponIconPosY);
    sC("cWpnIconPos",    ui->comboMetroidHudWeaponIconPosition);
    sC("cWpnIconAnchX",  ui->comboMetroidHudWeaponIconAnchorX);
    sC("cWpnIconAnchY",  ui->comboMetroidHudWeaponIconAnchorY);
    sB("cWpnIconClrOv",  ui->cbMetroidHudWeaponIconColorOverlay);
    // HP Gauge
    sB("cHpGauge",       ui->cbMetroidHudHpGauge);
    sC("cHpGaugeOrient", ui->comboMetroidHudHpGaugeOrientation);
    sSl("sHpGaugeLen",    ui->spinMetroidHudHpGaugeLength);
    sSl("sHpGaugeW",      ui->spinMetroidHudHpGaugeWidth);
    sSl("sHpGaugeOfsX",   ui->spinMetroidHudHpGaugeOffsetX);
    sSl("sHpGaugeOfsY",   ui->spinMetroidHudHpGaugeOffsetY);
    sC("cHpGaugeAnch",   ui->comboMetroidHudHpGaugeAnchor);
    sC("cHpGaugePosMode",ui->comboMetroidHudHpGaugePosMode);
    sSl("sHpGaugePosX",   ui->spinMetroidHudHpGaugePosX);
    sSl("sHpGaugePosY",   ui->spinMetroidHudHpGaugePosY);
    sB("cHpGaugeAutoClr",ui->cbMetroidHudHpGaugeAutoColor);
    sC("cHpGaugeClr", ui->comboMetroidHudHpGaugeColor);
    sE("eHpGaugeClr", ui->leMetroidHudHpGaugeColorCode);
    sI("sHpGaugeClrSpinR", ui->spinMetroidHudHpGaugeColorR);
    sI("sHpGaugeClrSpinG", ui->spinMetroidHudHpGaugeColorG);
    sI("sHpGaugeClrSpinB", ui->spinMetroidHudHpGaugeColorB);
    sCfgI("sHpGaugeClrR", "Metroid.Visual.HudHpGaugeColorR");
    sCfgI("sHpGaugeClrG", "Metroid.Visual.HudHpGaugeColorG");
    sCfgI("sHpGaugeClrB", "Metroid.Visual.HudHpGaugeColorB");
    // Ammo Gauge
    sB("cAmmoGauge",       ui->cbMetroidHudAmmoGauge);
    sC("cAmmoGaugeOrient", ui->comboMetroidHudAmmoGaugeOrientation);
    sSl("sAmmoGaugeLen",    ui->spinMetroidHudAmmoGaugeLength);
    sSl("sAmmoGaugeW",      ui->spinMetroidHudAmmoGaugeWidth);
    sSl("sAmmoGaugeOfsX",   ui->spinMetroidHudAmmoGaugeOffsetX);
    sSl("sAmmoGaugeOfsY",   ui->spinMetroidHudAmmoGaugeOffsetY);
    sC("cAmmoGaugeAnch",   ui->comboMetroidHudAmmoGaugeAnchor);
    sC("cAmmoGaugePosMode",ui->comboMetroidHudAmmoGaugePosMode);
    sSl("sAmmoGaugePosX",   ui->spinMetroidHudAmmoGaugePosX);
    sSl("sAmmoGaugePosY",   ui->spinMetroidHudAmmoGaugePosY);
    sC("cAmmoGaugeClr", ui->comboMetroidHudAmmoGaugeColor);
    sE("eAmmoGaugeClr", ui->leMetroidHudAmmoGaugeColorCode);
    sI("sAmmoGaugeClrSpinR", ui->spinMetroidHudAmmoGaugeColorR);
    sI("sAmmoGaugeClrSpinG", ui->spinMetroidHudAmmoGaugeColorG);
    sI("sAmmoGaugeClrSpinB", ui->spinMetroidHudAmmoGaugeColorB);
    sCfgI("sAmmoGaugeClrR", "Metroid.Visual.HudAmmoGaugeColorR");
    sCfgI("sAmmoGaugeClrG", "Metroid.Visual.HudAmmoGaugeColorG");
    sCfgI("sAmmoGaugeClrB", "Metroid.Visual.HudAmmoGaugeColorB");
    // HUD Radar
    sB("cRadarEnable", ui->cbMetroidBtmOverlayEnable);
    sSl("sRadarDstX", ui->spinMetroidBtmOverlayDstX);
    sSl("sRadarDstY", ui->spinMetroidBtmOverlayDstY);
    sSl("sRadarSize", ui->spinMetroidBtmOverlayDstSize);
    sD("dRadarOpacity", ui->spinMetroidBtmOverlayOpacity);
    sSl("sRadarSrcR",  ui->spinMetroidBtmOverlaySrcRadius);
    // Rank & Time colors (from config)
    sI("sRankClrSpinR", ui->spinMetroidHudRankColorR);
    sI("sRankClrSpinG", ui->spinMetroidHudRankColorG);
    sI("sRankClrSpinB", ui->spinMetroidHudRankColorB);
    sCfgI("sRankClrR", "Metroid.Visual.HudRankColorR");
    sCfgI("sRankClrG", "Metroid.Visual.HudRankColorG");
    sCfgI("sRankClrB", "Metroid.Visual.HudRankColorB");
    sI("sTimeLeftClrSpinR", ui->spinMetroidHudTimeLeftColorR);
    sI("sTimeLeftClrSpinG", ui->spinMetroidHudTimeLeftColorG);
    sI("sTimeLeftClrSpinB", ui->spinMetroidHudTimeLeftColorB);
    sCfgI("sTimeLeftClrR", "Metroid.Visual.HudTimeLeftColorR");
    sCfgI("sTimeLeftClrG", "Metroid.Visual.HudTimeLeftColorG");
    sCfgI("sTimeLeftClrB", "Metroid.Visual.HudTimeLeftColorB");
    sI("sTimeLimitClrSpinR", ui->spinMetroidHudTimeLimitColorR);
    sI("sTimeLimitClrSpinG", ui->spinMetroidHudTimeLimitColorG);
    sI("sTimeLimitClrSpinB", ui->spinMetroidHudTimeLimitColorB);
    sCfgI("sTimeLimitClrR", "Metroid.Visual.HudTimeLimitColorR");
    sCfgI("sTimeLimitClrG", "Metroid.Visual.HudTimeLimitColorG");
    sCfgI("sTimeLimitClrB", "Metroid.Visual.HudTimeLimitColorB");
    // Crosshair
    sCfgI("sChR", "Metroid.Visual.CrosshairColorR");
    sCfgI("sChG", "Metroid.Visual.CrosshairColorG");
    sCfgI("sChB", "Metroid.Visual.CrosshairColorB");
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
    auto rSl = [&](const char* k, QSlider* w, QSpinBox* input, QLabel* lbl) {
        auto it = s.find(k); if (it == s.end()) return;
        int v = it->toInt();
        w->blockSignals(true); w->setValue(v); w->blockSignals(false);
        if (input) {
            input->blockSignals(true);
            input->setValue(v);
            input->blockSignals(false);
        }
        lbl->setText(QString::number(v));
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
    rSl("sMatchX",     ui->spinMetroidHudMatchStatusX,    ui->inputMetroidHudMatchStatusX,    ui->labelMetroidHudMatchStatusX);
    rSl("sMatchY",     ui->spinMetroidHudMatchStatusY,    ui->inputMetroidHudMatchStatusY,    ui->labelMetroidHudMatchStatusY);
    rSl("sMatchLOfsX", ui->spinMetroidHudMatchStatusLabelOfsX, ui->inputMetroidHudMatchStatusLabelOfsX, ui->labelMetroidHudMatchStatusLabelOfsX);
    rSl("sMatchLOfsY", ui->spinMetroidHudMatchStatusLabelOfsY, ui->inputMetroidHudMatchStatusLabelOfsY, ui->labelMetroidHudMatchStatusLabelOfsY);
    rC("cMatchLPos",  ui->comboMetroidHudMatchStatusLabelPos);
    rE("eMatchLP",    ui->leMetroidHudMatchStatusLabelPoints);
    rE("eMatchLO",    ui->leMetroidHudMatchStatusLabelOctoliths);
    rE("eMatchLL",    ui->leMetroidHudMatchStatusLabelLives);
    rE("eMatchLR",    ui->leMetroidHudMatchStatusLabelRingTime);
    rE("eMatchLPT",   ui->leMetroidHudMatchStatusLabelPrimeTime);
    rC("cMatchClr", ui->comboMetroidHudMatchStatusColor);
    rE("eMatchClr", ui->leMetroidHudMatchStatusColorCode);
    rI("sMatchClrSpinR", ui->spinMetroidHudMatchStatusColorR);
    rI("sMatchClrSpinG", ui->spinMetroidHudMatchStatusColorG);
    rI("sMatchClrSpinB", ui->spinMetroidHudMatchStatusColorB);
    rC("cMatchLblClr", ui->comboMetroidHudMatchStatusLabelColor);
    rE("eMatchLblClr", ui->leMetroidHudMatchStatusLabelColorCode);
    rI("sMatchLblClrSpinR", ui->spinMetroidHudMatchStatusLabelColorR);
    rI("sMatchLblClrSpinG", ui->spinMetroidHudMatchStatusLabelColorG);
    rI("sMatchLblClrSpinB", ui->spinMetroidHudMatchStatusLabelColorB);
    rC("cMatchValClr", ui->comboMetroidHudMatchStatusValueColor);
    rE("eMatchValClr", ui->leMetroidHudMatchStatusValueColorCode);
    rI("sMatchValClrSpinR", ui->spinMetroidHudMatchStatusValueColorR);
    rI("sMatchValClrSpinG", ui->spinMetroidHudMatchStatusValueColorG);
    rI("sMatchValClrSpinB", ui->spinMetroidHudMatchStatusValueColorB);
    rC("cMatchSepClr", ui->comboMetroidHudMatchStatusSepColor);
    rE("eMatchSepClr", ui->leMetroidHudMatchStatusSepColorCode);
    rI("sMatchSepClrSpinR", ui->spinMetroidHudMatchStatusSepColorR);
    rI("sMatchSepClrSpinG", ui->spinMetroidHudMatchStatusSepColorG);
    rI("sMatchSepClrSpinB", ui->spinMetroidHudMatchStatusSepColorB);
    rC("cMatchGolClr", ui->comboMetroidHudMatchStatusGoalColor);
    rE("eMatchGolClr", ui->leMetroidHudMatchStatusGoalColorCode);
    rI("sMatchGolClrSpinR", ui->spinMetroidHudMatchStatusGoalColorR);
    rI("sMatchGolClrSpinG", ui->spinMetroidHudMatchStatusGoalColorG);
    rI("sMatchGolClrSpinB", ui->spinMetroidHudMatchStatusGoalColorB);
    // HP/Weapon
    rSl("sHpX",  ui->spinMetroidHudHpX,  ui->inputMetroidHudHpX,  ui->labelMetroidHudHpX);
    rSl("sHpY",  ui->spinMetroidHudHpY,  ui->inputMetroidHudHpY,  ui->labelMetroidHudHpY);
    rE("eHpPfx", ui->leMetroidHudHpPrefix);
    rC("cHpAlign", ui->comboMetroidHudHpAlign);
    rB("cHpTxtAuto", ui->cbMetroidHudHpTextAutoColor);
    rC("cHpTxtClr", ui->comboMetroidHudHpTextColor);
    rI("sHpTxtClrR", ui->spinMetroidHudHpTextColorR);
    rI("sHpTxtClrG", ui->spinMetroidHudHpTextColorG);
    rI("sHpTxtClrB", ui->spinMetroidHudHpTextColorB);
    rE("eHpTxtClr",  ui->leMetroidHudHpTextColorCode);
    rSl("sWpnX", ui->spinMetroidHudWeaponX, ui->inputMetroidHudWeaponX, ui->labelMetroidHudWeaponX);
    rSl("sWpnY", ui->spinMetroidHudWeaponY, ui->inputMetroidHudWeaponY, ui->labelMetroidHudWeaponY);
    rE("eAmmoPfx", ui->leMetroidHudAmmoPrefix);
    rC("cAmmoAlign", ui->comboMetroidHudAmmoAlign);
    rC("cAmmoTxtClr", ui->comboMetroidHudAmmoTextColor);
    rI("sAmmoTxtClrR", ui->spinMetroidHudAmmoTextColorR);
    rI("sAmmoTxtClrG", ui->spinMetroidHudAmmoTextColorG);
    rI("sAmmoTxtClrB", ui->spinMetroidHudAmmoTextColorB);
    rE("eAmmoTxtClr",  ui->leMetroidHudAmmoTextColorCode);
    rC("cHpPos",  ui->comboMetroidHudHpPosition);
    rC("cWpnPos", ui->comboMetroidHudWeaponPosition);
    rB("cWpnIconShow",  ui->cbMetroidHudWeaponIconShow);
    rC("cWpnIconMode",  ui->comboMetroidHudWeaponIconMode);
    rSl("sWpnIconOfsX",  ui->spinMetroidHudWeaponIconOffsetX, ui->inputMetroidHudWeaponIconOffsetX, ui->labelMetroidHudWeaponIconOffsetX);
    rSl("sWpnIconOfsY",  ui->spinMetroidHudWeaponIconOffsetY, ui->inputMetroidHudWeaponIconOffsetY, ui->labelMetroidHudWeaponIconOffsetY);
    rSl("sWpnIconPosX",  ui->spinMetroidHudWeaponIconPosX,    ui->inputMetroidHudWeaponIconPosX,    ui->labelMetroidHudWeaponIconPosX);
    rSl("sWpnIconPosY",  ui->spinMetroidHudWeaponIconPosY,    ui->inputMetroidHudWeaponIconPosY,    ui->labelMetroidHudWeaponIconPosY);
    rC("cWpnIconPos",    ui->comboMetroidHudWeaponIconPosition);
    rC("cWpnIconAnchX",  ui->comboMetroidHudWeaponIconAnchorX);
    rC("cWpnIconAnchY",  ui->comboMetroidHudWeaponIconAnchorY);
    rB("cWpnIconClrOv",  ui->cbMetroidHudWeaponIconColorOverlay);
    // HP Gauge
    rB("cHpGauge",       ui->cbMetroidHudHpGauge);
    rC("cHpGaugeOrient", ui->comboMetroidHudHpGaugeOrientation);
    rSl("sHpGaugeLen",   ui->spinMetroidHudHpGaugeLength,  ui->inputMetroidHudHpGaugeLength,  ui->labelMetroidHudHpGaugeLength);
    rSl("sHpGaugeW",     ui->spinMetroidHudHpGaugeWidth,   ui->inputMetroidHudHpGaugeWidth,   ui->labelMetroidHudHpGaugeWidth);
    rSl("sHpGaugeOfsX",  ui->spinMetroidHudHpGaugeOffsetX, ui->inputMetroidHudHpGaugeOffsetX, ui->labelMetroidHudHpGaugeOffsetX);
    rSl("sHpGaugeOfsY",  ui->spinMetroidHudHpGaugeOffsetY, ui->inputMetroidHudHpGaugeOffsetY, ui->labelMetroidHudHpGaugeOffsetY);
    rC("cHpGaugeAnch",   ui->comboMetroidHudHpGaugeAnchor);
    rC("cHpGaugePosMode",ui->comboMetroidHudHpGaugePosMode);
    rSl("sHpGaugePosX",  ui->spinMetroidHudHpGaugePosX,    ui->inputMetroidHudHpGaugePosX,    ui->labelMetroidHudHpGaugePosX);
    rSl("sHpGaugePosY",  ui->spinMetroidHudHpGaugePosY,    ui->inputMetroidHudHpGaugePosY,    ui->labelMetroidHudHpGaugePosY);
    rB("cHpGaugeAutoClr",ui->cbMetroidHudHpGaugeAutoColor);
    rC("cHpGaugeClr", ui->comboMetroidHudHpGaugeColor);
    rE("eHpGaugeClr", ui->leMetroidHudHpGaugeColorCode);
    rI("sHpGaugeClrSpinR", ui->spinMetroidHudHpGaugeColorR);
    rI("sHpGaugeClrSpinG", ui->spinMetroidHudHpGaugeColorG);
    rI("sHpGaugeClrSpinB", ui->spinMetroidHudHpGaugeColorB);
    // Ammo Gauge
    rB("cAmmoGauge",       ui->cbMetroidHudAmmoGauge);
    rC("cAmmoGaugeOrient", ui->comboMetroidHudAmmoGaugeOrientation);
    rSl("sAmmoGaugeLen",   ui->spinMetroidHudAmmoGaugeLength,  ui->inputMetroidHudAmmoGaugeLength,  ui->labelMetroidHudAmmoGaugeLength);
    rSl("sAmmoGaugeW",     ui->spinMetroidHudAmmoGaugeWidth,   ui->inputMetroidHudAmmoGaugeWidth,   ui->labelMetroidHudAmmoGaugeWidth);
    rSl("sAmmoGaugeOfsX",  ui->spinMetroidHudAmmoGaugeOffsetX, ui->inputMetroidHudAmmoGaugeOffsetX, ui->labelMetroidHudAmmoGaugeOffsetX);
    rSl("sAmmoGaugeOfsY",  ui->spinMetroidHudAmmoGaugeOffsetY, ui->inputMetroidHudAmmoGaugeOffsetY, ui->labelMetroidHudAmmoGaugeOffsetY);
    rC("cAmmoGaugeAnch",   ui->comboMetroidHudAmmoGaugeAnchor);
    rC("cAmmoGaugePosMode",ui->comboMetroidHudAmmoGaugePosMode);
    rSl("sAmmoGaugePosX",  ui->spinMetroidHudAmmoGaugePosX,    ui->inputMetroidHudAmmoGaugePosX,    ui->labelMetroidHudAmmoGaugePosX);
    rSl("sAmmoGaugePosY",  ui->spinMetroidHudAmmoGaugePosY,    ui->inputMetroidHudAmmoGaugePosY,    ui->labelMetroidHudAmmoGaugePosY);
    rC("cAmmoGaugeClr", ui->comboMetroidHudAmmoGaugeColor);
    rE("eAmmoGaugeClr", ui->leMetroidHudAmmoGaugeColorCode);
    rI("sAmmoGaugeClrSpinR", ui->spinMetroidHudAmmoGaugeColorR);
    rI("sAmmoGaugeClrSpinG", ui->spinMetroidHudAmmoGaugeColorG);
    rI("sAmmoGaugeClrSpinB", ui->spinMetroidHudAmmoGaugeColorB);
    // HUD Radar
    rB("cRadarEnable", ui->cbMetroidBtmOverlayEnable);
    rSl("sRadarDstX", ui->spinMetroidBtmOverlayDstX, ui->inputMetroidBtmOverlayDstX, ui->labelMetroidBtmOverlayDstX);
    rSl("sRadarDstY", ui->spinMetroidBtmOverlayDstY, ui->inputMetroidBtmOverlayDstY, ui->labelMetroidBtmOverlayDstY);
    rSl("sRadarSize", ui->spinMetroidBtmOverlayDstSize, ui->inputMetroidBtmOverlayDstSize, ui->labelMetroidBtmOverlayDstSize);
    rD("dRadarOpacity", ui->spinMetroidBtmOverlayOpacity);
    ui->sliderMetroidBtmOverlayOpacity->blockSignals(true);
    ui->sliderMetroidBtmOverlayOpacity->setValue(qRound(ui->spinMetroidBtmOverlayOpacity->value() * 100));
    ui->sliderMetroidBtmOverlayOpacity->blockSignals(false);
    rSl("sRadarSrcR",  ui->spinMetroidBtmOverlaySrcRadius,  ui->inputMetroidBtmOverlaySrcRadius,  ui->labelMetroidBtmOverlaySrcRadius);
    // Rank & Time colors
    rI("sRankClrSpinR", ui->spinMetroidHudRankColorR);
    rI("sRankClrSpinG", ui->spinMetroidHudRankColorG);
    rI("sRankClrSpinB", ui->spinMetroidHudRankColorB);
    rI("sTimeLeftClrSpinR", ui->spinMetroidHudTimeLeftColorR);
    rI("sTimeLeftClrSpinG", ui->spinMetroidHudTimeLeftColorG);
    rI("sTimeLeftClrSpinB", ui->spinMetroidHudTimeLeftColorB);
    rI("sTimeLimitClrSpinR", ui->spinMetroidHudTimeLimitColorR);
    rI("sTimeLimitClrSpinG", ui->spinMetroidHudTimeLimitColorG);
    rI("sTimeLimitClrSpinB", ui->spinMetroidHudTimeLimitColorB);
    // Crosshair
    rI("sChR", ui->spinMetroidCrosshairR);
    rI("sChG", ui->spinMetroidCrosshairG);
    rI("sChB", ui->spinMetroidCrosshairB);
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
    // Sync crosshair sliders to their restored spinbox values
    {
        auto syncSl = [](QSlider* sl, QSpinBox* sb, QLabel* lbl) {
            sl->blockSignals(true); sl->setValue(sb->value()); sl->blockSignals(false);
            lbl->setText(QString::number(sb->value()));
        };
        syncSl(ui->sliderMetroidCrosshairOutlineThickness, ui->spinMetroidCrosshairOutlineThickness, ui->labelMetroidCrosshairOutlineThickness);
        syncSl(ui->sliderMetroidCrosshairDotThickness,     ui->spinMetroidCrosshairDotThickness,     ui->labelMetroidCrosshairDotThickness);
        syncSl(ui->sliderMetroidCrosshairInnerLengthX,     ui->spinMetroidCrosshairInnerLengthX,     ui->labelMetroidCrosshairInnerLengthX);
        syncSl(ui->sliderMetroidCrosshairInnerLengthY,     ui->spinMetroidCrosshairInnerLengthY,     ui->labelMetroidCrosshairInnerLengthY);
        syncSl(ui->sliderMetroidCrosshairInnerThickness,   ui->spinMetroidCrosshairInnerThickness,   ui->labelMetroidCrosshairInnerThickness);
        syncSl(ui->sliderMetroidCrosshairInnerOffset,      ui->spinMetroidCrosshairInnerOffset,      ui->labelMetroidCrosshairInnerOffset);
        syncSl(ui->sliderMetroidCrosshairOuterLengthX,     ui->spinMetroidCrosshairOuterLengthX,     ui->labelMetroidCrosshairOuterLengthX);
        syncSl(ui->sliderMetroidCrosshairOuterLengthY,     ui->spinMetroidCrosshairOuterLengthY,     ui->labelMetroidCrosshairOuterLengthY);
        syncSl(ui->sliderMetroidCrosshairOuterThickness,   ui->spinMetroidCrosshairOuterThickness,   ui->labelMetroidCrosshairOuterThickness);
        syncSl(ui->sliderMetroidCrosshairOuterOffset,      ui->spinMetroidCrosshairOuterOffset,      ui->labelMetroidCrosshairOuterOffset);
    }

    // Restore color values from snapshot into config and update button backgrounds
    {
        Config::Table& instcfg = emuInstance->getLocalConfig();
        auto restoreColor = [&](QPushButton* btn, const char* kR, const char* kG, const char* kB,
                                const char* cfgR, const char* cfgG, const char* cfgB) {
            auto itR = s.find(kR), itG = s.find(kG), itB = s.find(kB);
            if (itR == s.end()) return;
            int r = itR->toInt(), g = itG->toInt(), b = itB->toInt();
            instcfg.SetInt(cfgR, r); instcfg.SetInt(cfgG, g); instcfg.SetInt(cfgB, b);
            btn->setStyleSheet(QString("background-color: %1;").arg(QColor(r, g, b).name()));
        };
        restoreColor(ui->btnMetroidCrosshairColor, "sChR", "sChG", "sChB",
            "Metroid.Visual.CrosshairColorR", "Metroid.Visual.CrosshairColorG", "Metroid.Visual.CrosshairColorB");
        restoreColor(ui->btnMetroidHudHpTextColor, "sCfgHpTxtClrR", "sCfgHpTxtClrG", "sCfgHpTxtClrB",
            "Metroid.Visual.HudHpTextColorR", "Metroid.Visual.HudHpTextColorG", "Metroid.Visual.HudHpTextColorB");
        restoreColor(ui->btnMetroidHudAmmoTextColor, "sCfgAmmoTxtClrR", "sCfgAmmoTxtClrG", "sCfgAmmoTxtClrB",
            "Metroid.Visual.HudAmmoTextColorR", "Metroid.Visual.HudAmmoTextColorG", "Metroid.Visual.HudAmmoTextColorB");
        restoreColor(ui->btnMetroidHudHpGaugeColor, "sHpGaugeClrR", "sHpGaugeClrG", "sHpGaugeClrB",
            "Metroid.Visual.HudHpGaugeColorR", "Metroid.Visual.HudHpGaugeColorG", "Metroid.Visual.HudHpGaugeColorB");
        restoreColor(ui->btnMetroidHudAmmoGaugeColor, "sAmmoGaugeClrR", "sAmmoGaugeClrG", "sAmmoGaugeClrB",
            "Metroid.Visual.HudAmmoGaugeColorR", "Metroid.Visual.HudAmmoGaugeColorG", "Metroid.Visual.HudAmmoGaugeColorB");
        restoreColor(ui->btnMetroidHudMatchStatusColor, "sMatchClrR", "sMatchClrG", "sMatchClrB",
            "Metroid.Visual.HudMatchStatusColorR", "Metroid.Visual.HudMatchStatusColorG", "Metroid.Visual.HudMatchStatusColorB");
        restoreColor(ui->btnMetroidHudMatchStatusLabelColor, "sMatchLblClrR", "sMatchLblClrG", "sMatchLblClrB",
            "Metroid.Visual.HudMatchStatusLabelColorR", "Metroid.Visual.HudMatchStatusLabelColorG", "Metroid.Visual.HudMatchStatusLabelColorB");
        restoreColor(ui->btnMetroidHudMatchStatusValueColor, "sMatchValClrR", "sMatchValClrG", "sMatchValClrB",
            "Metroid.Visual.HudMatchStatusValueColorR", "Metroid.Visual.HudMatchStatusValueColorG", "Metroid.Visual.HudMatchStatusValueColorB");
        restoreColor(ui->btnMetroidHudMatchStatusSepColor, "sMatchSepClrR", "sMatchSepClrG", "sMatchSepClrB",
            "Metroid.Visual.HudMatchStatusSepColorR", "Metroid.Visual.HudMatchStatusSepColorG", "Metroid.Visual.HudMatchStatusSepColorB");
        restoreColor(ui->btnMetroidHudMatchStatusGoalColor, "sMatchGolClrR", "sMatchGolClrG", "sMatchGolClrB",
            "Metroid.Visual.HudMatchStatusGoalColorR", "Metroid.Visual.HudMatchStatusGoalColorG", "Metroid.Visual.HudMatchStatusGoalColorB");
        restoreColor(ui->btnMetroidHudRankColor, "sRankClrR", "sRankClrG", "sRankClrB",
            "Metroid.Visual.HudRankColorR", "Metroid.Visual.HudRankColorG", "Metroid.Visual.HudRankColorB");
        restoreColor(ui->btnMetroidHudTimeLeftColor, "sTimeLeftClrR", "sTimeLeftClrG", "sTimeLeftClrB",
            "Metroid.Visual.HudTimeLeftColorR", "Metroid.Visual.HudTimeLeftColorG", "Metroid.Visual.HudTimeLeftColorB");
        restoreColor(ui->btnMetroidHudTimeLimitColor, "sTimeLimitClrR", "sTimeLimitClrG", "sTimeLimitClrB",
            "Metroid.Visual.HudTimeLimitColorR", "Metroid.Visual.HudTimeLimitColorG", "Metroid.Visual.HudTimeLimitColorB");
    }

    m_applyPreviewEnabled = true;
    applyVisualPreview();
}

void MelonPrimeInputConfig::applyVisualPreview()
{
#ifdef MELONPRIME_CUSTOM_HUD
    if (!m_applyPreviewEnabled || m_applyPreviewActive) return;
    m_applyPreviewActive = true;

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
    instcfg.SetInt("Metroid.Visual.HudHpGaugeColorR", ui->spinMetroidHudHpGaugeColorR->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeColorG", ui->spinMetroidHudHpGaugeColorG->value());
    instcfg.SetInt("Metroid.Visual.HudHpGaugeColorB", ui->spinMetroidHudHpGaugeColorB->value());

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
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorR", ui->spinMetroidHudAmmoGaugeColorR->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorG", ui->spinMetroidHudAmmoGaugeColorG->value());
    instcfg.SetInt("Metroid.Visual.HudAmmoGaugeColorB", ui->spinMetroidHudAmmoGaugeColorB->value());
    instcfg.SetInt("Metroid.Visual.CrosshairColorR", ui->spinMetroidCrosshairR->value());
    instcfg.SetInt("Metroid.Visual.CrosshairColorG", ui->spinMetroidCrosshairG->value());
    instcfg.SetInt("Metroid.Visual.CrosshairColorB", ui->spinMetroidCrosshairB->value());
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

    // Bottom Screen Overlay
    instcfg.SetBool  ("Metroid.Visual.BtmOverlayEnable",     ui->cbMetroidBtmOverlayEnable->isChecked());
    instcfg.SetInt   ("Metroid.Visual.BtmOverlayDstX",       ui->spinMetroidBtmOverlayDstX->value());
    instcfg.SetInt   ("Metroid.Visual.BtmOverlayDstY",       ui->spinMetroidBtmOverlayDstY->value());
    instcfg.SetInt   ("Metroid.Visual.BtmOverlayDstSize",    ui->spinMetroidBtmOverlayDstSize->value());
    instcfg.SetDouble("Metroid.Visual.BtmOverlayOpacity",    ui->spinMetroidBtmOverlayOpacity->value());
    instcfg.SetInt   ("Metroid.Visual.BtmOverlaySrcRadius",  ui->spinMetroidBtmOverlaySrcRadius->value());

    MelonPrime::CustomHud_InvalidateConfigCache();

    updateRadarPreview();
    updateCrosshairPreview();
    updateHpAmmoPreview();
    updateMatchStatusPreview();
    m_applyPreviewActive = false;
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
    // Color values are already in config (set by color button handlers)

    // Rank & Time HUD
    instcfg.SetBool("Metroid.Visual.HudRankShow",        ui->cbMetroidHudRankShow->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudRankX",            ui->spinMetroidHudRankX->value());
    instcfg.SetInt("Metroid.Visual.HudRankY",            ui->spinMetroidHudRankY->value());
    instcfg.SetString("Metroid.Visual.HudRankPrefix",    ui->leMetroidHudRankPrefix->text().toStdString());
    instcfg.SetBool("Metroid.Visual.HudRankShowOrdinal", ui->cbMetroidHudRankShowOrdinal->checkState() == Qt::Checked);
    instcfg.SetString("Metroid.Visual.HudRankSuffix",    ui->leMetroidHudRankSuffix->text().toStdString());
    instcfg.SetInt("Metroid.Visual.HudRankColorR",       ui->spinMetroidHudRankColorR->value());
    instcfg.SetInt("Metroid.Visual.HudRankColorG",       ui->spinMetroidHudRankColorG->value());
    instcfg.SetInt("Metroid.Visual.HudRankColorB",       ui->spinMetroidHudRankColorB->value());
    instcfg.SetBool("Metroid.Visual.HudTimeLeftShow",    ui->cbMetroidHudTimeLeftShow->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudTimeLeftX",        ui->spinMetroidHudTimeLeftX->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLeftY",        ui->spinMetroidHudTimeLeftY->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLeftColorR",   ui->spinMetroidHudTimeLeftColorR->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLeftColorG",   ui->spinMetroidHudTimeLeftColorG->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLeftColorB",   ui->spinMetroidHudTimeLeftColorB->value());
    instcfg.SetBool("Metroid.Visual.HudTimeLimitShow",   ui->cbMetroidHudTimeLimitShow->checkState() == Qt::Checked);
    instcfg.SetInt("Metroid.Visual.HudTimeLimitX",       ui->spinMetroidHudTimeLimitX->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLimitY",       ui->spinMetroidHudTimeLimitY->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLimitColorR",  ui->spinMetroidHudTimeLimitColorR->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLimitColorG",  ui->spinMetroidHudTimeLimitColorG->value());
    instcfg.SetInt("Metroid.Visual.HudTimeLimitColorB",  ui->spinMetroidHudTimeLimitColorB->value());

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
    instcfg.SetInt("Metroid.Visual.CrosshairColorR", ui->spinMetroidCrosshairR->value());
    instcfg.SetInt("Metroid.Visual.CrosshairColorG", ui->spinMetroidCrosshairG->value());
    instcfg.SetInt("Metroid.Visual.CrosshairColorB", ui->spinMetroidCrosshairB->value());

    // HUD Radar

    // HUD Radar

    // Crosshair — General
    instcfg.SetBool("Metroid.Visual.CrosshairOutline", ui->cbMetroidCrosshairOutline->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairOutlineOpacity", ui->spinMetroidCrosshairOutlineOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairOutlineThickness", ui->spinMetroidCrosshairOutlineThickness->value());
    instcfg.SetBool("Metroid.Visual.CrosshairCenterDot", ui->cbMetroidCrosshairCenterDot->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairDotOpacity", ui->spinMetroidCrosshairDotOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairDotThickness", ui->spinMetroidCrosshairDotThickness->value());
    instcfg.SetBool("Metroid.Visual.CrosshairTStyle", ui->cbMetroidCrosshairTStyle->checkState() == Qt::Checked);

    // HUD Radar

    // HUD Radar

    // Crosshair — Inner Lines
    instcfg.SetBool("Metroid.Visual.CrosshairInnerShow", ui->cbMetroidCrosshairInnerShow->checkState() == Qt::Checked);
    instcfg.SetDouble("Metroid.Visual.CrosshairInnerOpacity", ui->spinMetroidCrosshairInnerOpacity->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerLengthX", ui->spinMetroidCrosshairInnerLengthX->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerLengthY", ui->spinMetroidCrosshairInnerLengthY->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerThickness", ui->spinMetroidCrosshairInnerThickness->value());
    instcfg.SetInt("Metroid.Visual.CrosshairInnerOffset", ui->spinMetroidCrosshairInnerOffset->value());
    instcfg.SetBool("Metroid.Visual.CrosshairInnerLinkXY", ui->cbMetroidCrosshairInnerLinkXY->checkState() == Qt::Checked);

    // HUD Radar

    // HUD Radar

    // Crosshair — Outer Lines
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

void MelonPrimeInputConfig::setupColorButton(QPushButton* btn, const QString& configKeyR, const QString& configKeyG, const QString& configKeyB,
    QComboBox* combo, QLineEdit* lineEdit, QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB, int customIndex, int overallIndex)
{
    Config::Table& instcfg = emuInstance->getLocalConfig();
    int r = instcfg.GetInt(configKeyR.toStdString().c_str());
    int g = instcfg.GetInt(configKeyG.toStdString().c_str());
    int b = instcfg.GetInt(configKeyB.toStdString().c_str());

    btn->setStyleSheet(QString("background-color: %1;").arg(QColor(r, g, b).name()));

    connect(btn, &QPushButton::clicked, this, [this, btn, configKeyR, configKeyG, configKeyB, combo, lineEdit, spinR, spinG, spinB, customIndex, overallIndex]() {
        if (m_colorDialogOpen) return;
        m_colorDialogOpen = true;
        Config::Table& cfg = emuInstance->getLocalConfig();
        int curR = spinR ? spinR->value() : cfg.GetInt(configKeyR.toStdString().c_str());
        int curG = spinG ? spinG->value() : cfg.GetInt(configKeyG.toStdString().c_str());
        int curB = spinB ? spinB->value() : cfg.GetInt(configKeyB.toStdString().c_str());
        QColor initial(curR, curG, curB);
        QColorDialog dlg(initial, this);
        dlg.setOptions(QColorDialog::DontUseNativeDialog);
        dlg.raise();
        dlg.activateWindow();
        bool _accepted = (dlg.exec() == QDialog::Accepted);
        m_colorDialogOpen = false;
        if (!_accepted) return;
        QColor chosen = dlg.selectedColor();

        cfg.SetInt(configKeyR.toStdString().c_str(), chosen.red());
        cfg.SetInt(configKeyG.toStdString().c_str(), chosen.green());
        cfg.SetInt(configKeyB.toStdString().c_str(), chosen.blue());

        if (spinR && spinG && spinB) {
            spinR->blockSignals(true);
            spinG->blockSignals(true);
            spinB->blockSignals(true);
            spinR->setValue(chosen.red());
            spinG->setValue(chosen.green());
            spinB->setValue(chosen.blue());
            spinR->blockSignals(false);
            spinG->blockSignals(false);
            spinB->blockSignals(false);
        }

        if (lineEdit) {
            lineEdit->blockSignals(true);
            lineEdit->setText(QString("#%1%2%3")
                .arg(chosen.red(), 2, 16, QChar('0'))
                .arg(chosen.green(), 2, 16, QChar('0'))
                .arg(chosen.blue(), 2, 16, QChar('0')).toUpper());
            lineEdit->blockSignals(false);
        }

        if (combo && customIndex >= 0) {
            const int nextIndex = overallIndex >= 0 ? overallIndex : customIndex;
            combo->blockSignals(true);
            combo->setCurrentIndex(nextIndex);
            combo->blockSignals(false);

            const bool enableColorEditors = nextIndex != 0;
            if (lineEdit) lineEdit->setEnabled(enableColorEditors);
            if (spinR) spinR->setEnabled(enableColorEditors);
            if (spinG) spinG->setEnabled(enableColorEditors);
            if (spinB) spinB->setEnabled(enableColorEditors);
        }

        btn->setStyleSheet(QString("background-color: %1;").arg(chosen.name()));
        // Defer applyVisualPreview to the next event loop iteration so the dialog's
        // event loop has fully unwound before drawScreen() is called.
        QTimer::singleShot(0, this, [this]{ applyVisualPreview(); });
    });
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
        lbl->setText(QString::number(v));
    };

    // HP Position
    ui->comboMetroidHudHpPosition->setCurrentIndex(8); // Custom
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
    ui->comboMetroidHudWeaponPosition->setCurrentIndex(8); // Custom
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
    ui->comboMetroidHudWeaponIconPosition->setCurrentIndex(8); // Custom
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
        lbl->setText(QString::number(v));
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

void MelonPrimeInputConfig::updateCrosshairPreview()
{
    QWidget* preview = ui->widgetCrosshairPreview;
    if (!preview) return;

    Config::Table& instcfg = emuInstance->getLocalConfig();

    const int pw = preview->width();
    const int ph = preview->height();
    if (pw < 2 || ph < 2) return;

    QPixmap pixmap(pw, ph);
    pixmap.fill(QColor(30, 30, 40));

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    int cx = pw / 2;
    int cy = ph / 2;

    int chR = instcfg.GetInt("Metroid.Visual.CrosshairColorR");
    int chG = instcfg.GetInt("Metroid.Visual.CrosshairColorG");
    int chB = instcfg.GetInt("Metroid.Visual.CrosshairColorB");
    QColor chColor(chR, chG, chB);

    bool showOutline = instcfg.GetBool("Metroid.Visual.CrosshairOutline");
    double outlineOp = instcfg.GetDouble("Metroid.Visual.CrosshairOutlineOpacity");
    int outlineThick = instcfg.GetInt("Metroid.Visual.CrosshairOutlineThickness");
    bool showDot = instcfg.GetBool("Metroid.Visual.CrosshairCenterDot");
    double dotOp = instcfg.GetDouble("Metroid.Visual.CrosshairDotOpacity");
    int dotThick = instcfg.GetInt("Metroid.Visual.CrosshairDotThickness");
    bool tStyle = instcfg.GetBool("Metroid.Visual.CrosshairTStyle");

    bool showInner = instcfg.GetBool("Metroid.Visual.CrosshairInnerShow");
    double innerOp = instcfg.GetDouble("Metroid.Visual.CrosshairInnerOpacity");
    int innerLX = instcfg.GetInt("Metroid.Visual.CrosshairInnerLengthX");
    int innerLY = instcfg.GetInt("Metroid.Visual.CrosshairInnerLengthY");
    int innerThick = instcfg.GetInt("Metroid.Visual.CrosshairInnerThickness");
    int innerOfs = instcfg.GetInt("Metroid.Visual.CrosshairInnerOffset");

    bool showOuter = instcfg.GetBool("Metroid.Visual.CrosshairOuterShow");
    double outerOp = instcfg.GetDouble("Metroid.Visual.CrosshairOuterOpacity");
    int outerLX = instcfg.GetInt("Metroid.Visual.CrosshairOuterLengthX");
    int outerLY = instcfg.GetInt("Metroid.Visual.CrosshairOuterLengthY");
    int outerThick = instcfg.GetInt("Metroid.Visual.CrosshairOuterThickness");
    int outerOfs = instcfg.GetInt("Metroid.Visual.CrosshairOuterOffset");

    // Scale factor for visibility in preview
    const int S = 3;

    // Helper: draw a crosshair arm set (4 lines or 3 for T-style)
    auto drawArms = [&](int lenX, int lenY, int thick, int offset, double opacity, bool isOutline) {
        QColor c = isOutline ? QColor(0,0,0, static_cast<int>(opacity * 255)) : chColor;
        if (!isOutline) c.setAlpha(static_cast<int>(opacity * 255));
        int extra = isOutline ? outlineThick : 0;
        int t = (thick + extra * 2) * S;
        int ofsS = offset * S;

        p.setPen(Qt::NoPen);
        p.setBrush(c);

        // Right
        p.drawRect(cx + ofsS, cy - t/2, lenX * S, t);
        // Left
        p.drawRect(cx - ofsS - lenX * S, cy - t/2, lenX * S, t);
        // Down
        p.drawRect(cx - t/2, cy + ofsS, t, lenY * S);
        // Up (skip if T-style)
        if (!tStyle)
            p.drawRect(cx - t/2, cy - ofsS - lenY * S, t, lenY * S);
    };

    // Draw outline behind lines
    if (showOutline) {
        if (showOuter) drawArms(outerLX, outerLY, outerThick, outerOfs, outlineOp, true);
        if (showInner) drawArms(innerLX, innerLY, innerThick, innerOfs, outlineOp, true);
    }
    // Draw outer lines
    if (showOuter) drawArms(outerLX, outerLY, outerThick, outerOfs, outerOp, false);
    // Draw inner lines
    if (showInner) drawArms(innerLX, innerLY, innerThick, innerOfs, innerOp, false);

    // Center dot
    if (showDot) {
        QColor dc = chColor;
        dc.setAlpha(static_cast<int>(dotOp * 255));
        int ds = dotThick * S;
        if (showOutline) {
            QColor oc(0, 0, 0, static_cast<int>(outlineOp * 255));
            int os = (dotThick + outlineThick * 2) * S;
            p.setPen(Qt::NoPen);
            p.setBrush(oc);
            p.drawRect(cx - os/2, cy - os/2, os, os);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(dc);
        p.drawRect(cx - ds/2, cy - ds/2, ds, ds);
    }

    p.end();
    QPalette pal = preview->palette();
    pal.setBrush(QPalette::Window, pixmap);
    preview->setPalette(pal);
    preview->setAutoFillBackground(true);
    preview->update();
}

void MelonPrimeInputConfig::updateHpAmmoPreview()
{
    QWidget* preview = ui->widgetHpAmmoPreview;
    if (!preview) return;

    Config::Table& instcfg = emuInstance->getLocalConfig();

    const int pw = preview->width();
    const int ph = preview->height();
    if (pw < 2 || ph < 2) return;

    QPixmap pixmap(pw, ph);
    pixmap.fill(QColor(30, 30, 40));

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    // DS top screen is 256x192. Scale to fit preview.
    const float dsW = 256.0f, dsH = 192.0f;
    const float scale = std::min(static_cast<float>(pw) / dsW, static_cast<float>(ph) / dsH);
    const float offX = (pw - dsW * scale) / 2.0f;
    const float offY = (ph - dsH * scale) / 2.0f;

    // Draw screen border
    p.setPen(QPen(QColor(80, 80, 80), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(offX, offY, dsW * scale, dsH * scale));

    // Use the same font as the actual game HUD (:/mph-font, pixel size 6)
    {
        QFont hudFont;
        int fontId = QFontDatabase::addApplicationFont(":/mph-font");
        if (fontId >= 0) {
            QStringList families = QFontDatabase::applicationFontFamilies(fontId);
            if (!families.isEmpty())
                hudFont = QFont(families.at(0));
        }
        hudFont.setPixelSize(std::max(1, static_cast<int>(6.0f * scale)));
        hudFont.setStyleStrategy(QFont::NoAntialias);
        hudFont.setHintingPreference(QFont::PreferFullHinting);
        p.setFont(hudFont);
    }

    // HP text
    // hpY is the text baseline in DS coords (matches DrawCachedText/DrawHP in the game).
    // drawText(QPointF, text) uses Y as baseline, so map directly: no extra font offset needed.
    int hpX = instcfg.GetInt("Metroid.Visual.HudHpX");
    int hpY = instcfg.GetInt("Metroid.Visual.HudHpY");
    int hpAlign = instcfg.GetInt("Metroid.Visual.HudHpAlign");
    int hpR = instcfg.GetInt("Metroid.Visual.HudHpTextColorR");
    int hpG = instcfg.GetInt("Metroid.Visual.HudHpTextColorG");
    int hpB = instcfg.GetInt("Metroid.Visual.HudHpTextColorB");
    int hpGaugeR = instcfg.GetInt("Metroid.Visual.HudHpGaugeColorR");
    int hpGaugeG = instcfg.GetInt("Metroid.Visual.HudHpGaugeColorG");
    int hpGaugeB = instcfg.GetInt("Metroid.Visual.HudHpGaugeColorB");
    QString hpText = QString::fromStdString(instcfg.GetString("Metroid.Visual.HudHpPrefix")) + "199";
    p.setPen(QColor(hpR, hpG, hpB));
    float hpSy = offY + hpY * scale;
    int hpTextW = p.fontMetrics().horizontalAdvance(hpText);
    float hpSx = offX + hpX * scale;
    if (hpAlign == 1) hpSx -= hpTextW / 2.0f;
    else if (hpAlign == 2) hpSx -= hpTextW;
    p.drawText(QPointF(hpSx, hpSy), hpText);

    // HP gauge bar — mirrors CalcGaugePos() from MelonPrimeCustomHud.cpp
    if (instcfg.GetBool("Metroid.Visual.HudHpGauge")) {
        int gLen   = instcfg.GetInt("Metroid.Visual.HudHpGaugeLength");
        int gWid   = instcfg.GetInt("Metroid.Visual.HudHpGaugeWidth");
        int gOfsX  = instcfg.GetInt("Metroid.Visual.HudHpGaugeOffsetX");
        int gOfsY  = instcfg.GetInt("Metroid.Visual.HudHpGaugeOffsetY");
        int orient = instcfg.GetInt("Metroid.Visual.HudHpGaugeOrientation");
        int anchor = instcfg.GetInt("Metroid.Visual.HudHpGaugeAnchor");
        int posMode= instcfg.GetInt("Metroid.Visual.HudHpGaugePosMode");
        float gx, gy;
        if (posMode == 1) {
            gx = offX + instcfg.GetInt("Metroid.Visual.HudHpGaugePosX") * scale;
            gy = offY + instcfg.GetInt("Metroid.Visual.HudHpGaugePosY") * scale;
        } else {
            const QFontMetrics fm = p.fontMetrics();
            float tH = fm.height(), tW = static_cast<float>(hpTextW);
            float gL = gLen * scale, gW = gWid * scale;
            float ox = gOfsX * scale, oy = gOfsY * scale;
            switch (anchor) {
            case 1: gx = hpSx + ox; gy = hpSy - tH - (orient==0?gW:gL) + oy; break;
            case 2: gx = hpSx + tW + ox; gy = hpSy - tH/2.f - (orient==0?gW:gL)/2.f + oy; break;
            case 3: gx = hpSx - (orient==0?gL:gW) + ox; gy = hpSy - tH/2.f - (orient==0?gW:gL)/2.f + oy; break;
            case 4: gx = hpSx + tW/2.f - (orient==0?gL:gW)/2.f + ox; gy = hpSy - tH/2.f - (orient==0?gW:gL)/2.f + oy; break;
            default: gx = hpSx + ox; gy = hpSy + 2 * scale + oy; break;
            }
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(hpGaugeR, hpGaugeG, hpGaugeB));
        if (orient == 0) // Horizontal
            p.drawRect(QRectF(gx, gy, gLen * scale, gWid * scale));
        else // Vertical
            p.drawRect(QRectF(gx, gy, gWid * scale, gLen * scale));
    }

    // Weapon icon
    if (instcfg.GetBool("Metroid.Visual.HudWeaponIconShow")) {
        static QPixmap s_missileIcon;
        if (s_missileIcon.isNull())
            s_missileIcon.load(":/mph-icon-missile");
        if (!s_missileIcon.isNull()) {
            int iconPosX = instcfg.GetInt("Metroid.Visual.HudWeaponIconPosX");
            int iconPosY = instcfg.GetInt("Metroid.Visual.HudWeaponIconPosY");
            float iconSx = offX + iconPosX * scale;
            float iconSy = offY + iconPosY * scale;
            int iconW = static_cast<int>(s_missileIcon.width() * scale);
            int iconH = static_cast<int>(s_missileIcon.height() * scale);
            p.drawPixmap(QRectF(iconSx - iconW / 2.0f, iconSy - iconH / 2.0f, iconW, iconH),
                         s_missileIcon, s_missileIcon.rect());
        }
    }

    // Ammo text — same baseline logic as HP
    int wpnX = instcfg.GetInt("Metroid.Visual.HudWeaponX");
    int wpnY = instcfg.GetInt("Metroid.Visual.HudWeaponY");
    int ammoAlign = instcfg.GetInt("Metroid.Visual.HudAmmoAlign");
    int amR = instcfg.GetInt("Metroid.Visual.HudAmmoTextColorR");
    int amG = instcfg.GetInt("Metroid.Visual.HudAmmoTextColorG");
    int amB = instcfg.GetInt("Metroid.Visual.HudAmmoTextColorB");
    int amGaugeR = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeColorR");
    int amGaugeG = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeColorG");
    int amGaugeB = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeColorB");
    QString ammoText = QString::fromStdString(instcfg.GetString("Metroid.Visual.HudAmmoPrefix")) + "30";
    p.setPen(QColor(amR, amG, amB));
    float wpnSy = offY + wpnY * scale;
    int ammoTextW = p.fontMetrics().horizontalAdvance(ammoText);
    float wpnSx = offX + wpnX * scale;
    if (ammoAlign == 1) wpnSx -= ammoTextW / 2.0f;
    else if (ammoAlign == 2) wpnSx -= ammoTextW;
    p.drawText(QPointF(wpnSx, wpnSy), ammoText);

    // Ammo gauge bar — mirrors CalcGaugePos() from MelonPrimeCustomHud.cpp
    if (instcfg.GetBool("Metroid.Visual.HudAmmoGauge")) {
        int gLen   = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeLength");
        int gWid   = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeWidth");
        int gOfsX  = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetX");
        int gOfsY  = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetY");
        int orient = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeOrientation");
        int anchor = instcfg.GetInt("Metroid.Visual.HudAmmoGaugeAnchor");
        int posMode= instcfg.GetInt("Metroid.Visual.HudAmmoGaugePosMode");
        float gx, gy;
        if (posMode == 1) {
            gx = offX + instcfg.GetInt("Metroid.Visual.HudAmmoGaugePosX") * scale;
            gy = offY + instcfg.GetInt("Metroid.Visual.HudAmmoGaugePosY") * scale;
        } else {
            const QFontMetrics fm = p.fontMetrics();
            float tH = fm.height(), tW = static_cast<float>(ammoTextW);
            float gL = gLen * scale, gW = gWid * scale;
            float ox = gOfsX * scale, oy = gOfsY * scale;
            switch (anchor) {
            case 1: gx = wpnSx + ox; gy = wpnSy - tH - (orient==0?gW:gL) + oy; break;
            case 2: gx = wpnSx + tW + ox; gy = wpnSy - tH/2.f - (orient==0?gW:gL)/2.f + oy; break;
            case 3: gx = wpnSx - (orient==0?gL:gW) + ox; gy = wpnSy - tH/2.f - (orient==0?gW:gL)/2.f + oy; break;
            case 4: gx = wpnSx + tW/2.f - (orient==0?gL:gW)/2.f + ox; gy = wpnSy - tH/2.f - (orient==0?gW:gL)/2.f + oy; break;
            default: gx = wpnSx + ox; gy = wpnSy + 2 * scale + oy; break;
            }
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(amGaugeR, amGaugeG, amGaugeB));
        if (orient == 0)
            p.drawRect(QRectF(gx, gy, gLen * scale, gWid * scale));
        else
            p.drawRect(QRectF(gx, gy, gWid * scale, gLen * scale));
    }

    p.end();
    QPalette pal = preview->palette();
    pal.setBrush(QPalette::Window, pixmap);
    preview->setPalette(pal);
    preview->setAutoFillBackground(true);
    preview->update();
}

void MelonPrimeInputConfig::updateMatchStatusPreview()
{
    QWidget* preview = ui->widgetMatchStatusPreview;
    if (!preview) return;

    Config::Table& instcfg = emuInstance->getLocalConfig();

    const int pw = preview->width();
    const int ph = preview->height();
    if (pw < 2 || ph < 2) return;

    QPixmap pixmap(pw, ph);
    pixmap.fill(QColor(30, 30, 40));

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, false);

    const float dsW = 256.0f, dsH = 192.0f;
    const float scale = std::min(static_cast<float>(pw) / dsW, static_cast<float>(ph) / dsH);
    const float offX = (pw - dsW * scale) / 2.0f;
    const float offY = (ph - dsH * scale) / 2.0f;
    constexpr int kHudFontSize = 6;

    p.setPen(QPen(QColor(80, 80, 80), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(offX, offY, dsW * scale, dsH * scale));

    QFont hudFont;
    int fontId = QFontDatabase::addApplicationFont(":/mph-font");
    if (fontId >= 0) {
        QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (!families.isEmpty())
            hudFont = QFont(families.at(0));
    }
    hudFont.setPixelSize(std::max(1, static_cast<int>(kHudFontSize * scale)));
    hudFont.setStyleStrategy(QFont::NoAntialias);
    hudFont.setHintingPreference(QFont::PreferFullHinting);
    p.setFont(hudFont);

    auto formatTimeText = [](int seconds) {
        int safeSeconds = std::max(0, seconds);
        return QString("%1:%2")
            .arg(safeSeconds / 60)
            .arg(safeSeconds % 60, 2, 10, QLatin1Char('0'));
    };

    auto formatMinuteText = [](int minutes) {
        return QString("%1:00").arg(std::max(0, minutes));
    };

    auto buildRankText = [&instcfg]() {
        constexpr const char* ordinals[] = { "st", "nd", "rd", "th" };
        constexpr int rankIndex = 0;
        QString text = QString::fromStdString(instcfg.GetString("Metroid.Visual.HudRankPrefix"));
        text += QString::number(rankIndex + 1);
        if (instcfg.GetBool("Metroid.Visual.HudRankShowOrdinal"))
            text += QString::fromLatin1(ordinals[rankIndex]);
        text += QString::fromStdString(instcfg.GetString("Metroid.Visual.HudRankSuffix"));
        return text;
    };

    auto drawPreviewText = [&](const QString& text, int x, int y, const QColor& color) {
        p.setPen(color);
        p.drawText(QPointF(offX + x * scale, offY + y * scale), text);
    };

    if (!instcfg.GetBool("Metroid.Visual.HudMatchStatusShow")) {
        p.setPen(QColor(80, 80, 80));
        p.drawText(QRectF(offX, offY, dsW * scale, dsH * scale), Qt::AlignCenter, "HIDDEN");
    } else {
        const int msX = instcfg.GetInt("Metroid.Visual.HudMatchStatusX");
        const int msY = instcfg.GetInt("Metroid.Visual.HudMatchStatusY");

        QColor valueClr(
            instcfg.GetInt("Metroid.Visual.HudMatchStatusValueColorR"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusValueColorG"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusValueColorB"));
        QColor sepClr(
            instcfg.GetInt("Metroid.Visual.HudMatchStatusSepColorR"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusSepColorG"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusSepColorB"));
        QColor goalClr(
            instcfg.GetInt("Metroid.Visual.HudMatchStatusGoalColorR"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusGoalColorG"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusGoalColorB"));
        QColor labelClr(
            instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelColorR"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelColorG"),
            instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelColorB"));

        const float sx = offX + msX * scale;
        const float sy = offY + msY * scale;

        const QString labelText = QString::fromStdString(instcfg.GetString("Metroid.Visual.HudMatchStatusLabelPoints"));
        const int labelPos = instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelPos");
        const int labelOfsX = instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsX");
        const int labelOfsY = instcfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsY");
        // Mirror game logic: base position depends on labelPos (±10 DS px), then add user offset
        float lx, ly;
        switch (labelPos) {
        case 0:  lx = sx;               ly = sy - 10 * scale; break; // above
        case 1:  lx = sx;               ly = sy + 10 * scale; break; // below
        case 2:  lx = sx - 50 * scale;  ly = sy;              break; // left
        case 3:  lx = sx + 50 * scale;  ly = sy;              break; // right
        default: lx = sx;               ly = sy;              break; // same
        }
        lx += labelOfsX * scale;
        ly += labelOfsY * scale;

        p.setPen(labelClr);
        p.drawText(QPointF(lx, ly), labelText);

        p.setPen(valueClr);
        p.drawText(QPointF(sx, sy), "3");
        float xOfs = static_cast<float>(p.fontMetrics().horizontalAdvance("3"));
        p.setPen(sepClr);
        p.drawText(QPointF(sx + xOfs, sy), "/");
        xOfs += static_cast<float>(p.fontMetrics().horizontalAdvance("/"));
        p.setPen(goalClr);
        p.drawText(QPointF(sx + xOfs, sy), "7");
    }

    if (instcfg.GetBool("Metroid.Visual.HudRankShow")) {
        drawPreviewText(
            buildRankText(),
            instcfg.GetInt("Metroid.Visual.HudRankX"),
            instcfg.GetInt("Metroid.Visual.HudRankY"),
            QColor(
                instcfg.GetInt("Metroid.Visual.HudRankColorR"),
                instcfg.GetInt("Metroid.Visual.HudRankColorG"),
                instcfg.GetInt("Metroid.Visual.HudRankColorB")));
    }

    if (instcfg.GetBool("Metroid.Visual.HudTimeLeftShow")) {
        drawPreviewText(
            formatTimeText(5 * 60),
            instcfg.GetInt("Metroid.Visual.HudTimeLeftX"),
            instcfg.GetInt("Metroid.Visual.HudTimeLeftY"),
            QColor(
                instcfg.GetInt("Metroid.Visual.HudTimeLeftColorR"),
                instcfg.GetInt("Metroid.Visual.HudTimeLeftColorG"),
                instcfg.GetInt("Metroid.Visual.HudTimeLeftColorB")));
    }

    if (instcfg.GetBool("Metroid.Visual.HudTimeLimitShow")) {
        drawPreviewText(
            formatMinuteText(7),
            instcfg.GetInt("Metroid.Visual.HudTimeLimitX"),
            instcfg.GetInt("Metroid.Visual.HudTimeLimitY"),
            QColor(
                instcfg.GetInt("Metroid.Visual.HudTimeLimitColorR"),
                instcfg.GetInt("Metroid.Visual.HudTimeLimitColorG"),
                instcfg.GetInt("Metroid.Visual.HudTimeLimitColorB")));
    }

    p.end();
    QPalette pal = preview->palette();
    pal.setBrush(QPalette::Window, pixmap);
    preview->setPalette(pal);
    preview->setAutoFillBackground(true);
    preview->update();
}














