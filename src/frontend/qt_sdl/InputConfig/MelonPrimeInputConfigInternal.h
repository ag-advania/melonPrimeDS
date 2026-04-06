/*
    Copyright 2016-2025 melonDS team
    (MelonPrime specific configuration extension)

    Internal header shared by MelonPrimeInputConfig*.cpp files.
    Do NOT include from other headers — implementation detail only.
*/

#pragma once

#include <initializer_list>

#include <QColor>
#include <QComboBox>
#include <QFont>
#include <QFontDatabase>
#include <QLineEdit>
#include <QObject>
#include <QSpinBox>
#include <QString>
#include <QWidget>

#include "MelonPrimeConstants.h"

// ─── Colour palette ────────────────────────────────────────────────────────

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

struct PresetColor { int r, g, b; };
struct PositionPreset { int x, y; };

template <typename T, size_t N>
constexpr int ArrayCount(const T (&)[N]) { return static_cast<int>(N); }

// Must be constexpr so ArrayCount() below is a compile-time constant.
constexpr const char* kHudColorPaletteNames[] = {
    "White",
    "Green",
    "Yellow Green",
    "Green Yellow",
    "Yellow",
    "Pure Cyan",
    "Hud Cyan",
    "Pink",
    "Red",
    "Orange",
    "Samus Hud",
    "Samus Hud Outline",
    "Kanden Hud",
    "Spire Hud",
    "Spire Hud Outline",
    "Trace Hud",
    "Noxus Hud",
    "Noxus Hud Outline",
    "Sylux Hud",
    "Sylux Crosshair",
    "Weavel Hud",
    "Weavel Hud Outline",
    "Avium Purple",
};

constexpr PresetColor kHudColorPalette[] = {
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

constexpr HudColorPresetId kUnifiedHudColorPresets[] = {
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

constexpr int kHudColorPresetCount              = ArrayCount(kUnifiedHudColorPresets);
constexpr int kHudColorCustomIndex              = kHudColorPresetCount;
constexpr int kHudColorOverallIndex             = 0;
constexpr int kHudColorSubColorPresetIndexOffset = 1;
constexpr int kHudColorSubColorCustomIndex      = kHudColorSubColorPresetIndexOffset + kHudColorPresetCount;
constexpr int kHudPositionPresetCount           = 8;
constexpr int kHudPositionCustomIndex           = kHudPositionPresetCount;

static_assert(ArrayCount(kHudColorPalette) == kHudColorPresetCount,
    "kHudColorPalette must match the unified HUD preset list");
static_assert(static_cast<int>(HudColorPresetId::AviumPurple) + 1 == kHudColorPresetCount,
    "HudColorPresetId and the unified HUD preset list must stay in sync");

// ─── Position presets ──────────────────────────────────────────────────────

constexpr PositionPreset kHudHpPositionPresets[] = {
    {4,8}, {120,8}, {220,8}, {220,96}, {220,188}, {120,188}, {4,188}, {4,96}
};

constexpr PositionPreset kHudWeaponPositionPresets[] = {
    {4,2}, {120,2}, {226,2}, {226,82}, {226,164}, {120,164}, {4,164}, {4,82}
};

// Weapon icon uses slightly different Y values from the weapon text position.
constexpr PositionPreset kHudWeaponIconPositionPresets[] = {
    {4,2}, {120,2}, {226,2}, {226,82}, {226,174}, {120,174}, {4,174}, {4,82}
};

static_assert(ArrayCount(kHudHpPositionPresets)         == kHudPositionPresetCount);
static_assert(ArrayCount(kHudWeaponPositionPresets)     == kHudPositionPresetCount);
static_assert(ArrayCount(kHudWeaponIconPositionPresets) == kHudPositionPresetCount);

// ─── Utility functions ─────────────────────────────────────────────────────

inline const PresetColor& getPresetColor(HudColorPresetId id)
{
    return kHudColorPalette[static_cast<int>(id)];
}

inline QString formatColorHex(int r, int g, int b)
{
    return QString("#%1%2%3")
        .arg(r, 2, 16, QChar('0'))
        .arg(g, 2, 16, QChar('0'))
        .arg(b, 2, 16, QChar('0'))
        .toUpper();
}

inline void setBlockedSpinValue(QSpinBox* spin, int value)
{
    const bool old = spin->blockSignals(true);
    spin->setValue(value);
    spin->blockSignals(old);
}

inline void setColorSpinValues(QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB, int r, int g, int b)
{
    setBlockedSpinValue(spinR, r);
    setBlockedSpinValue(spinG, g);
    setBlockedSpinValue(spinB, b);
}

inline void setBlockedComboIndex(QComboBox* combo, int index)
{
    const bool old = combo->blockSignals(true);
    combo->setCurrentIndex(index);
    combo->blockSignals(old);
}

inline void hideWidgets(std::initializer_list<QWidget*> widgets)
{
    for (QWidget* w : widgets)
        if (w) w->hide();
}

inline int findPositionPresetIndex(const PositionPreset* presets, int x, int y, int defaultIndex)
{
    for (int i = 0; i < kHudPositionPresetCount; ++i)
        if (x == presets[i].x && y == presets[i].y) return i;
    return defaultIndex;
}

inline int findPresetColorIndex(const HudColorPresetId* presetOrder, int presetCount,
    int r, int g, int b, int defaultIndex, int presetIndexOffset = 0)
{
    for (int i = 0; i < presetCount; ++i) {
        const PresetColor& preset = getPresetColor(presetOrder[i]);
        if (r == preset.r && g == preset.g && b == preset.b)
            return i + presetIndexOffset;
    }
    return defaultIndex;
}

inline void syncColorFromPresetSelection(QComboBox* combo, QLineEdit* lineEdit,
    QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB,
    const HudColorPresetId* presetOrder, int presetCount,
    int comboIndex, int presetIndexOffset = 0)
{
    const int presetIndex = comboIndex - presetIndexOffset;
    if (presetIndex < 0 || presetIndex >= presetCount) return;
    const PresetColor& preset = getPresetColor(presetOrder[presetIndex]);
    setColorSpinValues(spinR, spinG, spinB, preset.r, preset.g, preset.b);
    lineEdit->setText(formatColorHex(preset.r, preset.g, preset.b));
}

inline void syncColorFromRgbEditors(QComboBox* combo, QLineEdit* lineEdit,
    QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB, int customIndex)
{
    setBlockedComboIndex(combo, customIndex);
    lineEdit->setText(formatColorHex(spinR->value(), spinG->value(), spinB->value()));
}

inline void syncColorFromHexEditor(QComboBox* combo, QLineEdit* lineEdit,
    QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB, int customIndex)
{
    QColor color(lineEdit->text());
    if (!color.isValid()) return;
    setColorSpinValues(spinR, spinG, spinB, color.red(), color.green(), color.blue());
    setBlockedComboIndex(combo, customIndex);
}

inline void bindPresetColorSync(QObject* owner, QComboBox* combo, QLineEdit* lineEdit,
    QSpinBox* spinR, QSpinBox* spinG, QSpinBox* spinB,
    const HudColorPresetId* presetOrder, int presetCount,
    int customIndex, int presetIndexOffset = 0)
{
    combo->setCurrentIndex(findPresetColorIndex(
        presetOrder, presetCount,
        spinR->value(), spinG->value(), spinB->value(),
        customIndex, presetIndexOffset));

    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), owner,
        [=](int comboIndex) {
            syncColorFromPresetSelection(combo, lineEdit, spinR, spinG, spinB,
                presetOrder, presetCount, comboIndex, presetIndexOffset);
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

// ─── HUD font helper ───────────────────────────────────────────────────────
// Returns the mph.ttf font at kCustomHudFontSize scaled by textScalePct/100,
// matching the rendering used by the actual in-game HUD overlay.
// textScalePct: value of Metroid.Visual.HudTextScale (default 100)
inline QFont getMphHudFont(int textScalePct = 100)
{
    static QString s_family;
    if (s_family.isEmpty()) {
        // Font may already be registered by Screen.cpp; addApplicationFont is idempotent.
        int id = QFontDatabase::addApplicationFont(QStringLiteral(":/mph-font"));
        if (id >= 0) {
            const auto families = QFontDatabase::applicationFontFamilies(id);
            if (!families.isEmpty())
                s_family = families.at(0);
        }
    }

    const double tds = qBound(0.1, textScalePct / 100.0, 10.0);
    const int px = qMax(3, static_cast<int>(MelonPrime::kCustomHudFontSize * tds));

    QFont f(s_family.isEmpty() ? QStringLiteral("Consolas") : s_family, -1);
    f.setPixelSize(px);
    f.setStyleStrategy(QFont::NoAntialias);
    f.setHintingPreference(QFont::PreferFullHinting);
    return f;
}
