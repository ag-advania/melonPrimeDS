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

// InputConfigDialog must be fully defined before including MapButton.h.
// MapButton accesses parentDialog directly, so a forward declaration is not enough.
#include "InputConfigDialog.h" 

#include "MapButton.h"
#include "Platform.h"
#include "VideoSettingsDialog.h"
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeCustomHud.h"
#endif

using namespace melonDS;

namespace {

enum class HudColorPresetId
{
    White,
    Green,
    YellowGreen,
    GreenYellow,
    Yellow,
    PureCyan,
    HudCyan,
    Pink,
    Red,
    Orange,
    SamusHud,
    SamusHudOutline,
    KandenHud,
    SpireHud,
    SpireHudOutline,
    TraceHud,
    NoxusHud,
    NoxusHudOutline,
    SyluxHud,
    SyluxCrosshair,
    WeavelHud,
    WeavelHudOutline,
    AviumPurple,
};

struct PresetColor
{
    int r;
    int g;
    int b;
};

struct PositionPreset
{
    int x;
    int y;
};

static const PresetColor kHudColorPalette[] = {
    {255,255,255}, // White
    {0,255,0},     // Green
    {127,255,0},   // YellowGreen
    {191,255,0},   // GreenYellow
    {255,255,0},   // Yellow
    {0,255,255},   // PureCyan
    {0,200,255},   // HudCyan
    {255,105,180}, // Pink
    {255,0,0},     // Red
    {255,165,0},   // Orange
    {120,240,64},  // SamusHud
    {40,152,80},   // SamusHudOutline
    {248,248,88},  // KandenHud
    {248,176,24},  // SpireHud
    {200,80,40},   // SpireHudOutline
    {248,40,40},   // TraceHud
    {80,152,208},  // NoxusHud
    {40,104,152},  // NoxusHudOutline
    {56,192,8},    // SyluxHud
    {88,224,40},   // SyluxCrosshair
    {208,152,56},  // WeavelHud
    {248,224,128}, // WeavelHudOutline
    {76,0,252},    // AviumPurple
};

static const HudColorPresetId kUnifiedHudColorPresets[] = {
    HudColorPresetId::White,
    HudColorPresetId::Green,
    HudColorPresetId::YellowGreen,
    HudColorPresetId::GreenYellow,
    HudColorPresetId::Yellow,
    HudColorPresetId::PureCyan,
    HudColorPresetId::HudCyan,
    HudColorPresetId::Pink,
    HudColorPresetId::Red,
    HudColorPresetId::Orange,
    HudColorPresetId::SamusHud,
    HudColorPresetId::SamusHudOutline,
    HudColorPresetId::KandenHud,
    HudColorPresetId::SpireHud,
    HudColorPresetId::SpireHudOutline,
    HudColorPresetId::TraceHud,
    HudColorPresetId::NoxusHud,
    HudColorPresetId::NoxusHudOutline,
    HudColorPresetId::SyluxHud,
    HudColorPresetId::SyluxCrosshair,
    HudColorPresetId::WeavelHud,
    HudColorPresetId::WeavelHudOutline,
    HudColorPresetId::AviumPurple,
};

template <typename T, size_t N>
constexpr int ArrayCount(const T (&)[N])
{
    return static_cast<int>(N);
}

constexpr int kHudColorPresetCount = ArrayCount(kUnifiedHudColorPresets);
constexpr int kHudColorCustomIndex = kHudColorPresetCount;
constexpr int kHudColorOverallIndex = 0;
constexpr int kHudColorSubColorPresetIndexOffset = 1;
constexpr int kHudColorSubColorCustomIndex = kHudColorSubColorPresetIndexOffset + kHudColorPresetCount;
constexpr int kHudPositionPresetCount = 8;
constexpr int kHudPositionCustomIndex = kHudPositionPresetCount;

static const PositionPreset kHudHpPositionPresets[] = {
    {4,8}, {120,8}, {220,8}, {220,96}, {220,188}, {120,188}, {4,188}, {4,96}
};

static const PositionPreset kHudWeaponPositionPresets[] = {
    {4,2}, {120,2}, {226,2}, {226,82}, {226,164}, {120,164}, {4,164}, {4,82}
};

static_assert(ArrayCount(kHudColorPalette) == kHudColorPresetCount,
    "kHudColorPalette must match the unified HUD preset list");
static_assert(static_cast<int>(HudColorPresetId::AviumPurple) + 1 == kHudColorPresetCount,
    "HudColorPresetId and the unified HUD preset list must stay in sync");

const PresetColor& getPresetColor(HudColorPresetId id)
{
    return kHudColorPalette[static_cast<int>(id)];
}

QString formatColorHex(int r, int g, int b)
{
    return QString("#%1%2%3").arg(r, 2, 16, QChar('0')).arg(g, 2, 16, QChar('0')).arg(b, 2, 16, QChar('0')).toUpper();
}

void setBlockedSpinValue(QSpinBox* spin, int value)
{
    const bool old = spin->blockSignals(true);
    spin->setValue(value);
    spin->blockSignals(old);
}

void setColorSpinValues(QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB, int r, int g, int b)
{
    setBlockedSpinValue(spinR, r);
    setBlockedSpinValue(spinG, g);
    setBlockedSpinValue(spinB, b);
}

void setBlockedComboIndex(QComboBox* combo, int index)
{
    const bool old = combo->blockSignals(true);
    combo->setCurrentIndex(index);
    combo->blockSignals(old);
}

void hideWidgets(std::initializer_list<QWidget*> widgets)
{
    for (QWidget* widget : widgets) {
        if (widget) {
            widget->hide();
        }
    }
}

int findPositionPresetIndex(const PositionPreset* presets, int x, int y, int defaultIndex)
{
    for (int i = 0; i < kHudPositionPresetCount; ++i) {
        if (x == presets[i].x && y == presets[i].y) {
            return i;
        }
    }
    return defaultIndex;
}

int findPresetColorIndex(const HudColorPresetId* presetOrder, int presetCount, int r, int g, int b, int defaultIndex, int presetIndexOffset = 0)
{
    for (int i = 0; i < presetCount; ++i) {
        const PresetColor& preset = getPresetColor(presetOrder[i]);
        if (r == preset.r && g == preset.g && b == preset.b) {
            return i + presetIndexOffset;
        }
    }
    return defaultIndex;
}

void syncColorFromPresetSelection(QComboBox* combo, QLineEdit* lineEdit, QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB,
    const HudColorPresetId* presetOrder, int presetCount, int comboIndex, int presetIndexOffset = 0)
{
    const int presetIndex = comboIndex - presetIndexOffset;
    if (presetIndex < 0 || presetIndex >= presetCount) return;

    const PresetColor& preset = getPresetColor(presetOrder[presetIndex]);
    setColorSpinValues(spinR, spinG, spinB, preset.r, preset.g, preset.b);
    lineEdit->setText(formatColorHex(preset.r, preset.g, preset.b));
}

void syncColorFromRgbEditors(QComboBox* combo, QLineEdit* lineEdit, QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB, int customIndex)
{
    setBlockedComboIndex(combo, customIndex);
    lineEdit->setText(formatColorHex(spinR->value(), spinG->value(), spinB->value()));
}

void syncColorFromHexEditor(QComboBox* combo, QLineEdit* lineEdit, QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB, int customIndex)
{
    QColor color(lineEdit->text());
    if (!color.isValid()) return;

    setColorSpinValues(spinR, spinG, spinB, color.red(), color.green(), color.blue());
    setBlockedComboIndex(combo, customIndex);
}

void bindPresetColorSync(QObject* owner, QComboBox* combo, QLineEdit* lineEdit, QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB,
    const HudColorPresetId* presetOrder, int presetCount, int customIndex, int presetIndexOffset = 0)
{
    combo->setCurrentIndex(findPresetColorIndex(
        presetOrder,
        presetCount,
        spinR->value(),
        spinG->value(),
        spinB->value(),
        customIndex,
        presetIndexOffset));

    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), owner,
        [=](int comboIndex) {
            syncColorFromPresetSelection(combo, lineEdit, spinR, spinG, spinB, presetOrder, presetCount, comboIndex, presetIndexOffset);
        });

    const auto rgbChanged = [=]() {
        syncColorFromRgbEditors(combo, lineEdit, spinR, spinG, spinB, customIndex);
    };
    QObject::connect(spinR, QOverload<int>::of(&QSpinBox::valueChanged), owner, rgbChanged);
    QObject::connect(spinG, QOverload<int>::of(&QSpinBox::valueChanged), owner, rgbChanged);
    QObject::connect(spinB, QOverload<int>::of(&QSpinBox::valueChanged), owner, rgbChanged);

    QObject::connect(lineEdit, &QLineEdit::editingFinished, owner,
        [=]() {
            syncColorFromHexEditor(combo, lineEdit, spinR, spinG, spinB, customIndex);
        });
}

} // namespace

MelonPrimeInputConfig::MelonPrimeInputConfig(EmuInstance* emu, QWidget* parent) :
    QWidget(parent),
    ui(new Ui::MelonPrimeInputConfig),
    emuInstance(emu)
{
    ui->setupUi(this);

    hideWidgets({
        ui->labelMetroidHudMatchStatusX,
        ui->labelMetroidHudMatchStatusY,
        ui->labelMetroidHudMatchStatusLabelOfsX,
        ui->labelMetroidHudMatchStatusLabelOfsY,
        ui->labelMetroidHudHpX,
        ui->labelMetroidHudHpY,
        ui->labelMetroidHudWeaponX,
        ui->labelMetroidHudWeaponY,
        ui->labelMetroidHudWeaponIconOffsetX,
        ui->labelMetroidHudWeaponIconOffsetY,
        ui->labelMetroidHudWeaponIconPosX,
        ui->labelMetroidHudWeaponIconPosY,
        ui->labelMetroidHudHpGaugeLength,
        ui->labelMetroidHudHpGaugeWidth,
        ui->labelMetroidHudHpGaugeOffsetX,
        ui->labelMetroidHudHpGaugeOffsetY,
        ui->labelMetroidHudHpGaugePosX,
        ui->labelMetroidHudHpGaugePosY,
        ui->labelMetroidHudAmmoGaugeLength,
        ui->labelMetroidHudAmmoGaugeWidth,
        ui->labelMetroidHudAmmoGaugeOffsetX,
        ui->labelMetroidHudAmmoGaugeOffsetY,
        ui->labelMetroidHudAmmoGaugePosX,
        ui->labelMetroidHudAmmoGaugePosY,
        ui->labelMetroidBtmOverlayDstX,
        ui->labelMetroidBtmOverlayDstY,
        ui->labelMetroidBtmOverlayDstSize,
        ui->labelMetroidBtmOverlaySrcRadius,
        ui->labelMetroidCrosshairOutlineThickness,
        ui->labelMetroidCrosshairDotThickness,
        ui->labelMetroidCrosshairInnerLengthX,
        ui->labelMetroidCrosshairInnerLengthY,
        ui->labelMetroidCrosshairInnerThickness,
        ui->labelMetroidCrosshairInnerOffset,
        ui->labelMetroidCrosshairOuterLengthX,
        ui->labelMetroidCrosshairOuterLengthY,
        ui->labelMetroidCrosshairOuterThickness,
        ui->labelMetroidCrosshairOuterOffset,
    });

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

    auto bindHexButtonSync = [this](QPushButton* btn, QLineEdit* lineEdit) {
        connect(lineEdit, &QLineEdit::editingFinished, this, [btn, lineEdit]() {
            QColor c(lineEdit->text());
            if (!c.isValid()) return;
            btn->setStyleSheet(QString("background-color: %1;").arg(c.name()));
        });
    };

    auto bindComboButtonSync = [this](QPushButton* btn, QComboBox* combo, QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB) {
        auto updateButtonColor = [btn, spinR, spinG, spinB]() {
            btn->setStyleSheet(QString("background-color: %1;").arg(QColor(spinR->value(), spinG->value(), spinB->value()).name()));
        };
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [updateButtonColor](int) {
            updateButtonColor();
        });
        connect(spinR, QOverload<int>::of(&QSpinBox::valueChanged), this, [updateButtonColor](int) {
            updateButtonColor();
        });
        connect(spinG, QOverload<int>::of(&QSpinBox::valueChanged), this, [updateButtonColor](int) {
            updateButtonColor();
        });
        connect(spinB, QOverload<int>::of(&QSpinBox::valueChanged), this, [updateButtonColor](int) {
            updateButtonColor();
        });
        updateButtonColor();
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

    // Match Status colors - QPushButton color pickers
    setupColorButton(ui->btnMetroidHudMatchStatusColor,
        "Metroid.Visual.HudMatchStatusColorR", "Metroid.Visual.HudMatchStatusColorG", "Metroid.Visual.HudMatchStatusColorB",
        ui->comboMetroidHudMatchStatusColor, ui->leMetroidHudMatchStatusColorCode,
        ui->spinMetroidHudMatchStatusColorR, ui->spinMetroidHudMatchStatusColorG, ui->spinMetroidHudMatchStatusColorB, kHudColorCustomIndex);
    setupColorButton(ui->btnMetroidHudMatchStatusLabelColor,
        "Metroid.Visual.HudMatchStatusLabelColorR", "Metroid.Visual.HudMatchStatusLabelColorG", "Metroid.Visual.HudMatchStatusLabelColorB",
        ui->comboMetroidHudMatchStatusLabelColor, ui->leMetroidHudMatchStatusLabelColorCode,
        ui->spinMetroidHudMatchStatusLabelColorR, ui->spinMetroidHudMatchStatusLabelColorG, ui->spinMetroidHudMatchStatusLabelColorB, kHudColorSubColorCustomIndex);
    setupColorButton(ui->btnMetroidHudMatchStatusValueColor,
        "Metroid.Visual.HudMatchStatusValueColorR", "Metroid.Visual.HudMatchStatusValueColorG", "Metroid.Visual.HudMatchStatusValueColorB",
        ui->comboMetroidHudMatchStatusValueColor, ui->leMetroidHudMatchStatusValueColorCode,
        ui->spinMetroidHudMatchStatusValueColorR, ui->spinMetroidHudMatchStatusValueColorG, ui->spinMetroidHudMatchStatusValueColorB, kHudColorSubColorCustomIndex);
    setupColorButton(ui->btnMetroidHudMatchStatusSepColor,
        "Metroid.Visual.HudMatchStatusSepColorR", "Metroid.Visual.HudMatchStatusSepColorG", "Metroid.Visual.HudMatchStatusSepColorB",
        ui->comboMetroidHudMatchStatusSepColor, ui->leMetroidHudMatchStatusSepColorCode,
        ui->spinMetroidHudMatchStatusSepColorR, ui->spinMetroidHudMatchStatusSepColorG, ui->spinMetroidHudMatchStatusSepColorB, kHudColorSubColorCustomIndex);
    setupColorButton(ui->btnMetroidHudMatchStatusGoalColor,
        "Metroid.Visual.HudMatchStatusGoalColorR", "Metroid.Visual.HudMatchStatusGoalColorG", "Metroid.Visual.HudMatchStatusGoalColorB",
        ui->comboMetroidHudMatchStatusGoalColor, ui->leMetroidHudMatchStatusGoalColorCode,
        ui->spinMetroidHudMatchStatusGoalColorR, ui->spinMetroidHudMatchStatusGoalColorG, ui->spinMetroidHudMatchStatusGoalColorB, kHudColorSubColorCustomIndex);
    // Match Status color
    ui->spinMetroidHudMatchStatusColorR->setValue(instcfg.GetInt("Metroid.Visual.HudMatchStatusColorR"));
    ui->spinMetroidHudMatchStatusColorG->setValue(instcfg.GetInt("Metroid.Visual.HudMatchStatusColorG"));
    ui->spinMetroidHudMatchStatusColorB->setValue(instcfg.GetInt("Metroid.Visual.HudMatchStatusColorB"));
    ui->leMetroidHudMatchStatusColorCode->setText(formatColorHex(
        ui->spinMetroidHudMatchStatusColorR->value(),
        ui->spinMetroidHudMatchStatusColorG->value(),
        ui->spinMetroidHudMatchStatusColorB->value()));
    bindPresetColorSync(this,
        ui->comboMetroidHudMatchStatusColor,
        ui->leMetroidHudMatchStatusColorCode,
        ui->spinMetroidHudMatchStatusColorR,
        ui->spinMetroidHudMatchStatusColorG,
        ui->spinMetroidHudMatchStatusColorB,
        kUnifiedHudColorPresets,
        kHudColorPresetCount,
        kHudColorCustomIndex);

    // Sub-color helper: sets up load, combo↔RGB sync, and enable/disable for one part
    // comboIdx 0 = "Overall" (useOverall=true); 1..kHudColorPresetCount = presets; kHudColorSubColorCustomIndex = Custom
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
        int idx = kHudColorSubColorCustomIndex; // Custom
        if (useOverall) {
            idx = kHudColorOverallIndex;
        } else {
            for (int i = 0; i < kHudColorPresetCount; i++) {
                const PresetColor& preset = getPresetColor(kUnifiedHudColorPresets[i]);
                if (cfgR == preset.r && cfgG == preset.g && cfgB == preset.b)
                    { idx = i + kHudColorSubColorPresetIndexOffset; break; }
            }
        }
        combo->setCurrentIndex(idx);
        setEnabled(idx != kHudColorOverallIndex);

        // Combo -> RGB + hex + enable/disable
        QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int i) {
            setEnabled(i != kHudColorOverallIndex);
            if (i <= kHudColorOverallIndex || i > kHudColorPresetCount) return; // Overall or Custom: don't change RGB
            spR->blockSignals(true); spG->blockSignals(true); spB->blockSignals(true);
            spR->setValue(getPresetColor(kUnifiedHudColorPresets[i - kHudColorSubColorPresetIndexOffset]).r);
            spG->setValue(getPresetColor(kUnifiedHudColorPresets[i - kHudColorSubColorPresetIndexOffset]).g);
            spB->setValue(getPresetColor(kUnifiedHudColorPresets[i - kHudColorSubColorPresetIndexOffset]).b);
            le->setText(QString("#%1%2%3")
                .arg(getPresetColor(kUnifiedHudColorPresets[i - kHudColorSubColorPresetIndexOffset]).r,2,16,QChar('0'))
                .arg(getPresetColor(kUnifiedHudColorPresets[i - kHudColorSubColorPresetIndexOffset]).g,2,16,QChar('0'))
                .arg(getPresetColor(kUnifiedHudColorPresets[i - kHudColorSubColorPresetIndexOffset]).b,2,16,QChar('0')).toUpper());
            spR->blockSignals(false); spG->blockSignals(false); spB->blockSignals(false);
        });
        // RGB -> hex + switch to Custom
        auto rgbChanged = [=]() {
            combo->blockSignals(true); combo->setCurrentIndex(kHudColorSubColorCustomIndex); combo->blockSignals(false);
            le->setText(QString("#%1%2%3")
                .arg(spR->value(),2,16,QChar('0'))
                .arg(spG->value(),2,16,QChar('0'))
                .arg(spB->value(),2,16,QChar('0')).toUpper());
        };
        QObject::connect(spR, QOverload<int>::of(&QSpinBox::valueChanged), this, rgbChanged);
        QObject::connect(spG, QOverload<int>::of(&QSpinBox::valueChanged), this, rgbChanged);
        QObject::connect(spB, QOverload<int>::of(&QSpinBox::valueChanged), this, rgbChanged);
        // Hex -> RGB + switch to Custom
        QObject::connect(le, &QLineEdit::editingFinished, this, [=]() {
            QColor c(le->text());
            if (!c.isValid()) return;
            spR->blockSignals(true); spG->blockSignals(true); spB->blockSignals(true);
            spR->setValue(c.red()); spG->setValue(c.green()); spB->setValue(c.blue());
            combo->blockSignals(true); combo->setCurrentIndex(kHudColorSubColorCustomIndex); combo->blockSignals(false);
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

    // Rank & Time HUD - load + color pickers
    ui->cbMetroidHudRankShow->setChecked(instcfg.GetBool("Metroid.Visual.HudRankShow"));
    initSliderSync(ui->spinMetroidHudRankX, ui->inputMetroidHudRankX, nullptr, instcfg.GetInt("Metroid.Visual.HudRankX"));
    initSliderSync(ui->spinMetroidHudRankY, ui->inputMetroidHudRankY, nullptr, instcfg.GetInt("Metroid.Visual.HudRankY"));
    ui->leMetroidHudRankPrefix->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudRankPrefix")));
    ui->cbMetroidHudRankShowOrdinal->setChecked(instcfg.GetBool("Metroid.Visual.HudRankShowOrdinal"));
    ui->leMetroidHudRankSuffix->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudRankSuffix")));
    ui->comboMetroidHudRankAlign->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudRankAlign"));
    ui->cbMetroidHudTimeLeftShow->setChecked(instcfg.GetBool("Metroid.Visual.HudTimeLeftShow"));
    initSliderSync(ui->spinMetroidHudTimeLeftX, ui->inputMetroidHudTimeLeftX, nullptr, instcfg.GetInt("Metroid.Visual.HudTimeLeftX"));
    initSliderSync(ui->spinMetroidHudTimeLeftY, ui->inputMetroidHudTimeLeftY, nullptr, instcfg.GetInt("Metroid.Visual.HudTimeLeftY"));
    ui->comboMetroidHudTimeLeftAlign->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudTimeLeftAlign"));
    ui->cbMetroidHudTimeLimitShow->setChecked(instcfg.GetBool("Metroid.Visual.HudTimeLimitShow"));
    initSliderSync(ui->spinMetroidHudTimeLimitX, ui->inputMetroidHudTimeLimitX, nullptr, instcfg.GetInt("Metroid.Visual.HudTimeLimitX"));
    initSliderSync(ui->spinMetroidHudTimeLimitY, ui->inputMetroidHudTimeLimitY, nullptr, instcfg.GetInt("Metroid.Visual.HudTimeLimitY"));
    ui->comboMetroidHudTimeLimitAlign->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudTimeLimitAlign"));
    // Rank/Time color sync shares the same preset <-> RGB <-> hex path.
    {
        auto setupRtColor = [&](QPushButton* btn, QComboBox* combo, QLineEdit* le,
            QSpinBox* spR, QSpinBox* spG, QSpinBox* spB,
            const char* keyR, const char* keyG, const char* keyB)
        {
            setupColorButton(btn, keyR, keyG, keyB, combo, le, spR, spG, spB, kHudColorCustomIndex);
            spR->setValue(instcfg.GetInt(keyR));
            spG->setValue(instcfg.GetInt(keyG));
            spB->setValue(instcfg.GetInt(keyB));
            le->setText(formatColorHex(spR->value(), spG->value(), spB->value()));
            bindPresetColorSync(this, combo, le, spR, spG, spB, kUnifiedHudColorPresets, kHudColorPresetCount, kHudColorCustomIndex);
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
        setupRtColor(ui->btnMetroidHudBombLeftColor, ui->comboMetroidHudBombLeftColor, ui->leMetroidHudBombLeftColorCode,
            ui->spinMetroidHudBombLeftColorR, ui->spinMetroidHudBombLeftColorG, ui->spinMetroidHudBombLeftColorB,
            "Metroid.Visual.HudBombLeftColorR", "Metroid.Visual.HudBombLeftColorG", "Metroid.Visual.HudBombLeftColorB");
    }
    ui->cbMetroidHudBombLeftShow->setChecked(instcfg.GetBool("Metroid.Visual.HudBombLeftShow"));
    initSliderSync(ui->spinMetroidHudBombLeftX, ui->inputMetroidHudBombLeftX, nullptr, instcfg.GetInt("Metroid.Visual.HudBombLeftX"));
    initSliderSync(ui->spinMetroidHudBombLeftY, ui->inputMetroidHudBombLeftY, nullptr, instcfg.GetInt("Metroid.Visual.HudBombLeftY"));
    ui->comboMetroidHudBombLeftAlign->setCurrentIndex(instcfg.GetInt("Metroid.Visual.HudBombLeftAlign"));
    ui->leMetroidHudBombLeftPrefix->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudBombLeftPrefix")));
    ui->leMetroidHudBombLeftSuffix->setText(QString::fromStdString(instcfg.GetString("Metroid.Visual.HudBombLeftSuffix")));

    // Reset buttons
    connect(ui->btnResetMatchStatusDefaults, &QPushButton::clicked, this, &MelonPrimeInputConfig::resetMatchStatusDefaults);
    connect(ui->btnResetRankTimeDefaults,    &QPushButton::clicked, this, &MelonPrimeInputConfig::resetRankTimeDefaults);

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
    setupToggle(ui->btnToggleBombLeft,     ui->sectionBombLeft,     "BOMB LEFT",  "Metroid.UI.SectionBombLeft");
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

    // Crosshair - Color (QPushButton color picker)
    setupColorButton(ui->btnMetroidCrosshairColor,
        "Metroid.Visual.CrosshairColorR", "Metroid.Visual.CrosshairColorG", "Metroid.Visual.CrosshairColorB",
        ui->comboMetroidCrosshairColor, ui->leMetroidCrosshairColorCode,
        ui->spinMetroidCrosshairR, ui->spinMetroidCrosshairG, ui->spinMetroidCrosshairB, kHudColorCustomIndex);

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

    auto setupTextColorPreset = [this](QComboBox* combo, QLineEdit* lineEdit,
                                       QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB) {
        bindPresetColorSync(this, combo, lineEdit, spinR, spinG, spinB, kUnifiedHudColorPresets, kHudColorPresetCount, kHudColorCustomIndex);
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
        ui->spinMetroidHudHpTextColorR, ui->spinMetroidHudHpTextColorG, ui->spinMetroidHudHpTextColorB, kHudColorCustomIndex);
    setupColorButton(ui->btnMetroidHudAmmoTextColor,
        "Metroid.Visual.HudAmmoTextColorR", "Metroid.Visual.HudAmmoTextColorG", "Metroid.Visual.HudAmmoTextColorB",
        ui->comboMetroidHudAmmoTextColor, ui->leMetroidHudAmmoTextColorCode,
        ui->spinMetroidHudAmmoTextColorR, ui->spinMetroidHudAmmoTextColorG, ui->spinMetroidHudAmmoTextColorB, kHudColorCustomIndex);

    // Icon independent position preset detection
    {
        struct Pos { int x, y; };
        static const Pos presets[] = {
            {4,2}, {120,2}, {226,2}, {226,82}, {226,174}, {120,174}, {4,174}, {4,82}
        };
        int ix = ui->spinMetroidHudWeaponIconPosX->value();
        int iy = ui->spinMetroidHudWeaponIconPosY->value();
        int idx = kHudPositionCustomIndex;
        for (int i = 0; i < kHudPositionPresetCount; i++) {
            if (ix == presets[i].x && iy == presets[i].y) { idx = i; break; }
        }
        ui->comboMetroidHudWeaponIconPosition->setCurrentIndex(idx);
    }
    connect(ui->comboMetroidHudWeaponIconPosition, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        struct Pos { int x, y; };
        static const Pos presets[] = {
            {4,2}, {120,2}, {226,2}, {226,82}, {226,174}, {120,174}, {4,174}, {4,82}
        };
        if (idx < 0 || idx >= kHudPositionPresetCount) return;
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
        ui->comboMetroidHudWeaponIconPosition->setCurrentIndex(kHudPositionCustomIndex);
        ui->comboMetroidHudWeaponIconPosition->blockSignals(false);
    });
    connect(ui->spinMetroidHudWeaponIconPosY, &QSlider::valueChanged, this, [this]() {
        ui->comboMetroidHudWeaponIconPosition->blockSignals(true);
        ui->comboMetroidHudWeaponIconPosition->setCurrentIndex(kHudPositionCustomIndex);
        ui->comboMetroidHudWeaponIconPosition->blockSignals(false);
    });

    // Gauge settings - HP
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
    // HP Gauge color - QPushButton color picker
    setupColorButton(ui->btnMetroidHudHpGaugeColor,
        "Metroid.Visual.HudHpGaugeColorR", "Metroid.Visual.HudHpGaugeColorG", "Metroid.Visual.HudHpGaugeColorB",
        ui->comboMetroidHudHpGaugeColor, ui->leMetroidHudHpGaugeColorCode,
        ui->spinMetroidHudHpGaugeColorR, ui->spinMetroidHudHpGaugeColorG, ui->spinMetroidHudHpGaugeColorB, kHudColorCustomIndex);
    ui->spinMetroidHudHpGaugeColorR->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeColorR"));
    ui->spinMetroidHudHpGaugeColorG->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeColorG"));
    ui->spinMetroidHudHpGaugeColorB->setValue(instcfg.GetInt("Metroid.Visual.HudHpGaugeColorB"));
    ui->leMetroidHudHpGaugeColorCode->setText(
        QString("#%1%2%3")
            .arg(ui->spinMetroidHudHpGaugeColorR->value(), 2, 16, QChar('0'))
            .arg(ui->spinMetroidHudHpGaugeColorG->value(), 2, 16, QChar('0'))
            .arg(ui->spinMetroidHudHpGaugeColorB->value(), 2, 16, QChar('0')).toUpper());

    bindPresetColorSync(this,
        ui->comboMetroidHudHpGaugeColor,
        ui->leMetroidHudHpGaugeColorCode,
        ui->spinMetroidHudHpGaugeColorR,
        ui->spinMetroidHudHpGaugeColorG,
        ui->spinMetroidHudHpGaugeColorB,
        kUnifiedHudColorPresets,
        kHudColorPresetCount,
        kHudColorCustomIndex);

    // Gauge settings - Ammo
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
    // Ammo Gauge color - QPushButton color picker
    setupColorButton(ui->btnMetroidHudAmmoGaugeColor,
        "Metroid.Visual.HudAmmoGaugeColorR", "Metroid.Visual.HudAmmoGaugeColorG", "Metroid.Visual.HudAmmoGaugeColorB",
        ui->comboMetroidHudAmmoGaugeColor, ui->leMetroidHudAmmoGaugeColorCode,
        ui->spinMetroidHudAmmoGaugeColorR, ui->spinMetroidHudAmmoGaugeColorG, ui->spinMetroidHudAmmoGaugeColorB, kHudColorCustomIndex);
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

    bindPresetColorSync(this,
        ui->comboMetroidHudAmmoGaugeColor,
        ui->leMetroidHudAmmoGaugeColorCode,
        ui->spinMetroidHudAmmoGaugeColorR,
        ui->spinMetroidHudAmmoGaugeColorG,
        ui->spinMetroidHudAmmoGaugeColorB,
        kUnifiedHudColorPresets,
        kHudColorPresetCount,
        kHudColorCustomIndex);
    bindHexButtonSync(ui->btnMetroidHudHpGaugeColor, ui->leMetroidHudHpGaugeColorCode);
    bindComboButtonSync(ui->btnMetroidHudHpGaugeColor, ui->comboMetroidHudHpGaugeColor, ui->spinMetroidHudHpGaugeColorR, ui->spinMetroidHudHpGaugeColorG, ui->spinMetroidHudHpGaugeColorB);
    bindHexButtonSync(ui->btnMetroidHudAmmoGaugeColor, ui->leMetroidHudAmmoGaugeColorCode);
    bindComboButtonSync(ui->btnMetroidHudAmmoGaugeColor, ui->comboMetroidHudAmmoGaugeColor, ui->spinMetroidHudAmmoGaugeColorR, ui->spinMetroidHudAmmoGaugeColorG, ui->spinMetroidHudAmmoGaugeColorB);

    // HUD position presets: detect current preset from X/Y values
    // 0=TopLeft 1=TopCenter 2=TopRight 3=Right 4=BottomRight 5=BottomCenter 6=BottomLeft 7=Left 8=Custom
    ui->comboMetroidHudHpPosition->setCurrentIndex(findPositionPresetIndex(
        kHudHpPositionPresets,
        ui->spinMetroidHudHpX->value(),
        ui->spinMetroidHudHpY->value(),
        kHudPositionCustomIndex));
    ui->comboMetroidHudWeaponPosition->setCurrentIndex(findPositionPresetIndex(
        kHudWeaponPositionPresets,
        ui->spinMetroidHudWeaponX->value(),
        ui->spinMetroidHudWeaponY->value(),
        kHudPositionCustomIndex));

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

    // HP position preset -> update X/Y
    connect(ui->comboMetroidHudHpPosition, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        struct Pos { int x, y; };
        static const Pos presets[] = {
            {4,8}, {120,8}, {220,8}, {220,96}, {220,188}, {120,188}, {4,188}, {4,96}
        };
        if (idx < 0 || idx >= kHudPositionPresetCount) return;
        ui->spinMetroidHudHpX->blockSignals(true);
        ui->spinMetroidHudHpY->blockSignals(true);
        ui->spinMetroidHudHpX->setValue(presets[idx].x);
        ui->spinMetroidHudHpY->setValue(presets[idx].y);
        ui->labelMetroidHudHpX->setText(QString::number(presets[idx].x));
        ui->labelMetroidHudHpY->setText(QString::number(presets[idx].y));
        ui->spinMetroidHudHpX->blockSignals(false);
        ui->spinMetroidHudHpY->blockSignals(false);
    });
    // HP X/Y manual change -> switch to Custom
    connect(ui->spinMetroidHudHpX, &QSlider::valueChanged, this, [this]() {
        ui->comboMetroidHudHpPosition->blockSignals(true);
        ui->comboMetroidHudHpPosition->setCurrentIndex(kHudPositionCustomIndex);
        ui->comboMetroidHudHpPosition->blockSignals(false);
    });
    connect(ui->spinMetroidHudHpY, &QSlider::valueChanged, this, [this]() {
        ui->comboMetroidHudHpPosition->blockSignals(true);
        ui->comboMetroidHudHpPosition->setCurrentIndex(kHudPositionCustomIndex);
        ui->comboMetroidHudHpPosition->blockSignals(false);
    });

    // Weapon position preset -> update X/Y
    connect(ui->comboMetroidHudWeaponPosition, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        struct Pos { int x, y; };
        static const Pos presets[] = {
            {4,2}, {120,2}, {226,2}, {226,82}, {226,164}, {120,164}, {4,164}, {4,82}
        };
        if (idx < 0 || idx >= kHudPositionPresetCount) return;
        ui->spinMetroidHudWeaponX->blockSignals(true);
        ui->spinMetroidHudWeaponY->blockSignals(true);
        ui->spinMetroidHudWeaponX->setValue(presets[idx].x);
        ui->spinMetroidHudWeaponY->setValue(presets[idx].y);
        ui->labelMetroidHudWeaponX->setText(QString::number(presets[idx].x));
        ui->labelMetroidHudWeaponY->setText(QString::number(presets[idx].y));
        ui->spinMetroidHudWeaponX->blockSignals(false);
        ui->spinMetroidHudWeaponY->blockSignals(false);
    });
    // Weapon X/Y manual change -> switch to Custom
    connect(ui->spinMetroidHudWeaponX, &QSlider::valueChanged, this, [this]() {
        ui->comboMetroidHudWeaponPosition->blockSignals(true);
        ui->comboMetroidHudWeaponPosition->setCurrentIndex(kHudPositionCustomIndex);
        ui->comboMetroidHudWeaponPosition->blockSignals(false);
    });
    connect(ui->spinMetroidHudWeaponY, &QSlider::valueChanged, this, [this]() {
        ui->comboMetroidHudWeaponPosition->blockSignals(true);
        ui->comboMetroidHudWeaponPosition->setCurrentIndex(kHudPositionCustomIndex);
        ui->comboMetroidHudWeaponPosition->blockSignals(false);
    });

    // HUD Radar

    // HUD Radar

    // Crosshair - Color (continued)
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
    connect(ui->spinMetroidCrosshairR, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { syncCrosshairColorFromRgbEditors(); });
    connect(ui->spinMetroidCrosshairG, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { syncCrosshairColorFromRgbEditors(); });
    connect(ui->spinMetroidCrosshairB, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { syncCrosshairColorFromRgbEditors(); });

    ui->comboMetroidCrosshairColor->setCurrentIndex(findPresetColorIndex(
        kUnifiedHudColorPresets,
        kHudColorPresetCount,
        chR,
        chG,
        chB,
        kHudColorCustomIndex));

    // HUD Radar

    // HUD Radar

    // Crosshair - General
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

    // Crosshair - Inner Lines
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

    // Crosshair - Outer Lines
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
    prvB(ui->cbMetroidHudHpTextAutoColor); prvC(ui->comboMetroidHudHpTextColor); prvE(ui->leMetroidHudHpTextColorCode);
    prvI(ui->spinMetroidHudHpTextColorR); prvI(ui->spinMetroidHudHpTextColorG); prvI(ui->spinMetroidHudHpTextColorB);
    prvSl(ui->spinMetroidHudWeaponX); prvSl(ui->spinMetroidHudWeaponY); prvE(ui->leMetroidHudAmmoPrefix); prvC(ui->comboMetroidHudAmmoAlign);
    prvC(ui->comboMetroidHudAmmoTextColor); prvE(ui->leMetroidHudAmmoTextColorCode);
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
    prvC(ui->comboMetroidHudHpGaugeColor); prvE(ui->leMetroidHudHpGaugeColorCode);
    prvI(ui->spinMetroidHudHpGaugeColorR); prvI(ui->spinMetroidHudHpGaugeColorG); prvI(ui->spinMetroidHudHpGaugeColorB);
    // (HP gauge color button clicks already call applyVisualPreview via setupColorButton)
    // Ammo Gauge
    prvB(ui->cbMetroidHudAmmoGauge);
    prvC(ui->comboMetroidHudAmmoGaugeOrientation);
    prvSl(ui->spinMetroidHudAmmoGaugeLength); prvSl(ui->spinMetroidHudAmmoGaugeWidth);
    prvSl(ui->spinMetroidHudAmmoGaugeOffsetX); prvSl(ui->spinMetroidHudAmmoGaugeOffsetY);
    prvC(ui->comboMetroidHudAmmoGaugeAnchor); prvC(ui->comboMetroidHudAmmoGaugePosMode);
    prvSl(ui->spinMetroidHudAmmoGaugePosX);   prvSl(ui->spinMetroidHudAmmoGaugePosY);
    prvC(ui->comboMetroidHudAmmoGaugeColor); prvE(ui->leMetroidHudAmmoGaugeColorCode);
    prvI(ui->spinMetroidHudAmmoGaugeColorR); prvI(ui->spinMetroidHudAmmoGaugeColorG); prvI(ui->spinMetroidHudAmmoGaugeColorB);
    // (Ammo gauge color button clicks already call applyVisualPreview via setupColorButton)
    // HUD Radar
    prvB(ui->cbMetroidBtmOverlayEnable);
    prvSl(ui->spinMetroidBtmOverlayDstX); prvSl(ui->spinMetroidBtmOverlayDstY); prvSl(ui->spinMetroidBtmOverlayDstSize);
    prvD(ui->spinMetroidBtmOverlayOpacity);
    prvSl(ui->spinMetroidBtmOverlaySrcRadius);
    // Rank & Time HUD
    prvB(ui->cbMetroidHudRankShow);
    prvSl(ui->spinMetroidHudRankX); prvSl(ui->spinMetroidHudRankY);
    prvC(ui->comboMetroidHudRankAlign);
    prvC(ui->comboMetroidHudRankColor);
    prvE(ui->leMetroidHudRankColorCode);
    prvI(ui->spinMetroidHudRankColorR); prvI(ui->spinMetroidHudRankColorG); prvI(ui->spinMetroidHudRankColorB);
    prvB(ui->cbMetroidHudRankShowOrdinal);
    prvB(ui->cbMetroidHudTimeLeftShow);
    prvSl(ui->spinMetroidHudTimeLeftX); prvSl(ui->spinMetroidHudTimeLeftY);
    prvC(ui->comboMetroidHudTimeLeftAlign);
    prvC(ui->comboMetroidHudTimeLeftColor);
    prvE(ui->leMetroidHudTimeLeftColorCode);
    prvI(ui->spinMetroidHudTimeLeftColorR); prvI(ui->spinMetroidHudTimeLeftColorG); prvI(ui->spinMetroidHudTimeLeftColorB);
    prvB(ui->cbMetroidHudTimeLimitShow);
    prvSl(ui->spinMetroidHudTimeLimitX); prvSl(ui->spinMetroidHudTimeLimitY);
    prvC(ui->comboMetroidHudTimeLimitAlign);
    prvC(ui->comboMetroidHudTimeLimitColor);
    prvE(ui->leMetroidHudTimeLimitColorCode);
    prvI(ui->spinMetroidHudTimeLimitColorR); prvI(ui->spinMetroidHudTimeLimitColorG); prvI(ui->spinMetroidHudTimeLimitColorB);
    // Bomb Left HUD
    prvB(ui->cbMetroidHudBombLeftShow);
    prvSl(ui->spinMetroidHudBombLeftX); prvSl(ui->spinMetroidHudBombLeftY);
    prvC(ui->comboMetroidHudBombLeftAlign);
    prvC(ui->comboMetroidHudBombLeftColor);
    prvE(ui->leMetroidHudBombLeftColorCode);
    prvI(ui->spinMetroidHudBombLeftColorR); prvI(ui->spinMetroidHudBombLeftColorG); prvI(ui->spinMetroidHudBombLeftColorB);
    prvE(ui->leMetroidHudBombLeftPrefix);
    prvE(ui->leMetroidHudBombLeftSuffix);
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
    prvC(ui->comboMetroidCrosshairColor); prvE(ui->leMetroidCrosshairColorCode);
    prvI(ui->spinMetroidCrosshairR); prvI(ui->spinMetroidCrosshairG); prvI(ui->spinMetroidCrosshairB);
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
        QColor chosen = QColorDialog::getColor(
            initial,
            this,
            QString(),
            QColorDialog::DontUseNativeDialog);
        m_colorDialogOpen = false;
        if (!chosen.isValid()) return;

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

void MelonPrimeInputConfig::syncCrosshairColorFromRgbEditors()
{
    syncColorFromRgbEditors(
        ui->comboMetroidCrosshairColor,
        ui->leMetroidCrosshairColorCode,
        ui->spinMetroidCrosshairR,
        ui->spinMetroidCrosshairG,
        ui->spinMetroidCrosshairB,
        kHudColorCustomIndex);
}

void MelonPrimeInputConfig::on_leMetroidCrosshairColorCode_editingFinished()
{
    syncColorFromHexEditor(
        ui->comboMetroidCrosshairColor,
        ui->leMetroidCrosshairColorCode,
        ui->spinMetroidCrosshairR,
        ui->spinMetroidCrosshairG,
        ui->spinMetroidCrosshairB,
        kHudColorCustomIndex);
}

void MelonPrimeInputConfig::on_comboMetroidCrosshairColor_currentIndexChanged(int index)
{
    syncColorFromPresetSelection(
        ui->comboMetroidCrosshairColor,
        ui->leMetroidCrosshairColorCode,
        ui->spinMetroidCrosshairR,
        ui->spinMetroidCrosshairG,
        ui->spinMetroidCrosshairB,
        kUnifiedHudColorPresets,
        kHudColorPresetCount,
        index);
}

