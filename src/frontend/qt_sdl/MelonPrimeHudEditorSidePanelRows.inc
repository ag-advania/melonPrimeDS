// This file is a unity-build fragment included only by
// MelonPrimeHudConfigOnScreenEdit.cpp. Do not add it to CMakeLists.txt.
//
// V7 Phase 2: data-table representation of the HUD editor side panel's
// per-element rows. Replaces the hand-written per-element populate*()
// factory-call sequences with row tables consumed by one generic dispatcher
// (MelonPrimeHudConfigOnScreenEdit::populateFromRowTable). This is a data
// representation change only -- row order, labels, keys, ranges, and combo
// item text are unchanged from the pre-table implementation.
//
// Row order intentionally differs from the settings-dialog / in-game overlay
// schema tables (kSec*/kProps*, MelonPrimeHudPropSchema.inc) for bespoke
// side-panel UX; see V2 Phase 2d and V7 Phase 2 for that decision. Do not
// "fix" this table's ordering to match kProps*.
//
// Bespoke rows that do not fit the flat row shape stay outside the table,
// per the same V2/V7 precedent:
//   - the per-weapon icon tint loop in populateWpnIcon() (a 9-entry array
//     already compact as code; see MelonPrimeHudConfigOnScreenEdit.cpp)
//   - populateForCrosshair()'s surrounding lifecycle (m_currentElem = -2,
//     its own title/clear calls) stays in the .cpp; only its flat row body
//     is tabulated here.

#include <cstdint>
#include <utility>
#include <QStringList>

namespace MelonPrime {
namespace HudEditorSidePanel {

enum class RowKind : uint8_t {
    Bool, Combo, Spin, DoubleSpin, Opacity, LineEdit, Color, SubColor,
    ColorOverlay, Separator, SectionHeader,
    Builtins, OffsetRows, Align3, OutlineGroup, GaugePosition,
};

struct Row {
    RowKind kind = RowKind::Separator;
    const char* label = nullptr;
    const char* label2 = nullptr;
    const char* key1 = nullptr;
    const char* key2 = nullptr;
    const char* key3 = nullptr;
    const char* key4 = nullptr;
    const char* key5 = nullptr;
    const char* key6 = nullptr;
    const char* key7 = nullptr;
    const char* key8 = nullptr;
    const char* key9 = nullptr;
    int iMin = 0;
    int iMax = 0;
    double dMin = 0.0;
    double dMax = 0.0;
    double dStep = 0.0;
    QStringList items;
};

// ── Row factory helpers ──────────────────────────────────────────────────
// C++17 has no designated initializers, so named factory functions keep row
// tables readable instead of positional aggregate-init of the wide Row
// struct above.

inline Row RowBool(const char* label, const char* key)
{
    Row r; r.kind = RowKind::Bool; r.label = label; r.key1 = key; return r;
}

inline Row RowCombo(const char* label, const char* key, QStringList items)
{
    Row r; r.kind = RowKind::Combo; r.label = label; r.key1 = key; r.items = std::move(items); return r;
}

inline Row RowSpin(const char* label, const char* key, int min, int max)
{
    Row r; r.kind = RowKind::Spin; r.label = label; r.key1 = key; r.iMin = min; r.iMax = max; return r;
}

inline Row RowDoubleSpin(const char* label, const char* key, double min, double max, double step)
{
    Row r; r.kind = RowKind::DoubleSpin; r.label = label; r.key1 = key;
    r.dMin = min; r.dMax = max; r.dStep = step; return r;
}

inline Row RowOpacity(const char* label, const char* key)
{
    Row r; r.kind = RowKind::Opacity; r.label = label; r.key1 = key; return r;
}

inline Row RowLineEdit(const char* label, const char* key)
{
    Row r; r.kind = RowKind::LineEdit; r.label = label; r.key1 = key; return r;
}

inline Row RowColor(const char* label, const char* keyR, const char* keyG, const char* keyB)
{
    Row r; r.kind = RowKind::Color; r.label = label;
    r.key1 = keyR; r.key2 = keyG; r.key3 = keyB; return r;
}

inline Row RowSubColor(const char* label, const char* overallKey,
    const char* keyR, const char* keyG, const char* keyB)
{
    Row r; r.kind = RowKind::SubColor; r.label = label;
    r.key1 = overallKey; r.key2 = keyR; r.key3 = keyG; r.key4 = keyB; return r;
}

inline Row RowColorOverlay(const char* label, const char* enableKey,
    const char* keyR, const char* keyG, const char* keyB)
{
    Row r; r.kind = RowKind::ColorOverlay; r.label = label;
    r.key1 = enableKey; r.key2 = keyR; r.key3 = keyG; r.key4 = keyB; return r;
}

inline Row RowSeparator()
{
    Row r; r.kind = RowKind::Separator; return r;
}

inline Row RowSectionHeader(const char* label)
{
    Row r; r.kind = RowKind::SectionHeader; r.label = label; return r;
}

// showKey/colorR/colorG/colorB may be nullptr, matching addBuiltins().
inline Row RowBuiltins(const char* showKey,
    const char* colorR, const char* colorG, const char* colorB, const char* anchorKey)
{
    Row r; r.kind = RowKind::Builtins;
    r.key1 = showKey; r.key2 = colorR; r.key3 = colorG; r.key4 = colorB; r.key5 = anchorKey;
    return r;
}

inline Row RowOffset(const char* keyX, const char* keyY, int min, int max,
    const char* labelX, const char* labelY)
{
    Row r; r.kind = RowKind::OffsetRows;
    r.key1 = keyX; r.key2 = keyY; r.iMin = min; r.iMax = max;
    r.label = labelX; r.label2 = labelY; return r;
}

inline Row RowAlign3(const char* label, const char* key)
{
    Row r; r.kind = RowKind::Align3; r.label = label; r.key1 = key; return r;
}

// Call with (sectionLabel, MP_OUTLINE_KEYS(Prefix)) so the six keys are
// checked at compile time, same as the pre-table addOutlineGroupSection call.
inline Row RowOutline(const char* sectionLabel,
    const char* enableKey, const char* colorR, const char* colorG, const char* colorB,
    const char* opacityKey, const char* thicknessKey)
{
    Row r; r.kind = RowKind::OutlineGroup; r.label = sectionLabel;
    r.key1 = enableKey; r.key2 = colorR; r.key3 = colorG; r.key4 = colorB;
    r.key5 = opacityKey; r.key6 = thicknessKey; return r;
}

inline Row RowGaugePosition(const char* posModeKey,
    const char* gaugeAnchorKey, const char* gaugeOffsetXKey, const char* gaugeOffsetYKey,
    const char* gaugePosXKey, const char* gaugePosYKey,
    const char* textAnchorKey, const char* textOffsetXKey, const char* textOffsetYKey)
{
    Row r; r.kind = RowKind::GaugePosition;
    r.key1 = posModeKey; r.key2 = gaugeAnchorKey; r.key3 = gaugeOffsetXKey; r.key4 = gaugeOffsetYKey;
    r.key5 = gaugePosXKey; r.key6 = gaugePosYKey;
    r.key7 = textAnchorKey; r.key8 = textOffsetXKey; r.key9 = textOffsetYKey;
    return r;
}

} // namespace HudEditorSidePanel
} // namespace MelonPrime

#define MP_SIDE_ROW_COUNT(arr) static_cast<int>(sizeof(arr) / sizeof((arr)[0]))

using namespace MelonPrime::HudEditorSidePanel;

// ── Per-element row tables ───────────────────────────────────────────────
// Keys reference MelonPrimeHudPropSchema.inc's MP_HUD_PROP_KEY_* macros
// (already included by MelonPrimeHudConfigOnScreenEdit.cpp), so a typo in a
// key name fails to compile, same guarantee as the pre-table code.

static const Row kRowsHP[] = {
    RowBuiltins(nullptr,
        MP_HUD_PROP_KEY_HudHpTextColorR, MP_HUD_PROP_KEY_HudHpTextColorG, MP_HUD_PROP_KEY_HudHpTextColorB,
        MP_HUD_PROP_KEY_HudHpAnchor),
    RowOffset(MP_HUD_PROP_KEY_HudHpX, MP_HUD_PROP_KEY_HudHpY, -256, 256, "Offset X", "Offset Y"),
    RowLineEdit("Prefix", MP_HUD_PROP_KEY_HudHpPrefix),
    RowAlign3("Align", MP_HUD_PROP_KEY_HudHpAlign),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_HudHpOpacity),
    RowOutline("HP Outline", MP_OUTLINE_KEYS(HudHp)),
};

static const Row kRowsHPGauge[] = {
    RowBuiltins(MP_HUD_PROP_KEY_HudHpGauge,
        MP_HUD_PROP_KEY_HudHpGaugeColorR, MP_HUD_PROP_KEY_HudHpGaugeColorG, MP_HUD_PROP_KEY_HudHpGaugeColorB,
        MP_HUD_PROP_KEY_HudHpGaugePosAnchor),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_HudHpGaugeOpacity),
    RowCombo("Orientation", MP_HUD_PROP_KEY_HudHpGaugeOrientation, {"Horizontal", "Vertical"}),
    RowCombo("Align", MP_HUD_PROP_KEY_HudHpGaugeAlign, {"Start", "Center", "End"}),
    RowSpin("Length", MP_HUD_PROP_KEY_HudHpGaugeLength, 1, 192),
    RowSpin("Width", MP_HUD_PROP_KEY_HudHpGaugeWidth, 1, 20),
    RowGaugePosition(MP_HUD_PROP_KEY_HudHpGaugePosMode,
        MP_HUD_PROP_KEY_HudHpGaugeAnchor, MP_HUD_PROP_KEY_HudHpGaugeOffsetX, MP_HUD_PROP_KEY_HudHpGaugeOffsetY,
        MP_HUD_PROP_KEY_HudHpGaugePosX, MP_HUD_PROP_KEY_HudHpGaugePosY,
        MP_HUD_PROP_KEY_HudHpTextAnchor, MP_HUD_PROP_KEY_HudHpTextOffsetX, MP_HUD_PROP_KEY_HudHpTextOffsetY),
    RowOutline("HP Gauge Outline", MP_OUTLINE_KEYS(HudHpGauge)),
};

static const Row kRowsWeaponAmmo[] = {
    RowBuiltins(nullptr,
        MP_HUD_PROP_KEY_HudAmmoTextColorR, MP_HUD_PROP_KEY_HudAmmoTextColorG, MP_HUD_PROP_KEY_HudAmmoTextColorB,
        MP_HUD_PROP_KEY_HudWeaponAnchor),
    RowOffset(MP_HUD_PROP_KEY_HudWeaponX, MP_HUD_PROP_KEY_HudWeaponY, -256, 256, "Offset X", "Offset Y"),
    RowLineEdit("Prefix", MP_HUD_PROP_KEY_HudAmmoPrefix),
    RowAlign3("Align", MP_HUD_PROP_KEY_HudAmmoAlign),
    RowCombo("Layout", MP_HUD_PROP_KEY_HudWeaponLayout, {"Standard", "Alternative"}),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_HudWeaponOpacity),
    RowOutline("Ammo Outline", MP_OUTLINE_KEYS(HudWeapon)),
};

// Weapon icon tint (per-weapon color overlay) loop stays in
// populateWpnIcon() after this table; the final two rows here (Separator,
// SectionHeader) are its lead-in, matching the pre-table call order.
static const Row kRowsWpnIcon[] = {
    RowBuiltins(MP_HUD_PROP_KEY_HudWeaponIconShow,
        nullptr, nullptr, nullptr,
        MP_HUD_PROP_KEY_HudWeaponIconPosAnchor),
    RowOffset(MP_HUD_PROP_KEY_HudWeaponIconPosX, MP_HUD_PROP_KEY_HudWeaponIconPosY, -256, 256, "Pos X", "Pos Y"),
    RowCombo("Mode", MP_HUD_PROP_KEY_HudWeaponIconMode, {"Relative", "Independent"}),
    RowSpin("Height", MP_HUD_PROP_KEY_HudWeaponIconHeight, 4, 64),
    RowOffset(MP_HUD_PROP_KEY_HudWeaponIconOffsetX, MP_HUD_PROP_KEY_HudWeaponIconOffsetY, -128, 128, "Offset X", "Offset Y"),
    RowAlign3("Align X", MP_HUD_PROP_KEY_HudWeaponIconAnchorX),
    RowCombo("Align Y", MP_HUD_PROP_KEY_HudWeaponIconAnchorY, {"Top", "Center", "Bottom"}),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_HudWpnIconOpacity),
    RowOutline("Weapon Icon Outline", MP_OUTLINE_KEYS(HudWeaponIcon)),
    RowSeparator(),
    RowSectionHeader("Weapon Icon Color Overlay"),
};

static const Row kRowsAmmoGauge[] = {
    RowBuiltins(MP_HUD_PROP_KEY_HudAmmoGauge,
        MP_HUD_PROP_KEY_HudAmmoGaugeColorR, MP_HUD_PROP_KEY_HudAmmoGaugeColorG, MP_HUD_PROP_KEY_HudAmmoGaugeColorB,
        MP_HUD_PROP_KEY_HudAmmoGaugePosAnchor),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_HudAmmoGaugeOpacity),
    RowCombo("Orientation", MP_HUD_PROP_KEY_HudAmmoGaugeOrientation, {"Horizontal", "Vertical"}),
    RowCombo("Align", MP_HUD_PROP_KEY_HudAmmoGaugeAlign, {"Start", "Center", "End"}),
    RowSpin("Length", MP_HUD_PROP_KEY_HudAmmoGaugeLength, 1, 192),
    RowSpin("Width", MP_HUD_PROP_KEY_HudAmmoGaugeWidth, 1, 20),
    RowGaugePosition(MP_HUD_PROP_KEY_HudAmmoGaugePosMode,
        MP_HUD_PROP_KEY_HudAmmoGaugeAnchor, MP_HUD_PROP_KEY_HudAmmoGaugeOffsetX, MP_HUD_PROP_KEY_HudAmmoGaugeOffsetY,
        MP_HUD_PROP_KEY_HudAmmoGaugePosX, MP_HUD_PROP_KEY_HudAmmoGaugePosY,
        MP_HUD_PROP_KEY_HudAmmoTextAnchor, MP_HUD_PROP_KEY_HudAmmoTextOffsetX, MP_HUD_PROP_KEY_HudAmmoTextOffsetY),
    RowOutline("Ammo Gauge Outline", MP_OUTLINE_KEYS(HudAmmoGauge)),
};

static const Row kRowsMatchStatus[] = {
    RowBuiltins(MP_HUD_PROP_KEY_HudMatchStatusShow,
        MP_HUD_PROP_KEY_HudMatchStatusColorR, MP_HUD_PROP_KEY_HudMatchStatusColorG, MP_HUD_PROP_KEY_HudMatchStatusColorB,
        MP_HUD_PROP_KEY_HudMatchStatusAnchor),
    RowOffset(MP_HUD_PROP_KEY_HudMatchStatusX, MP_HUD_PROP_KEY_HudMatchStatusY, -256, 256, "Offset X", "Offset Y"),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_HudMatchStatusOpacity),
    RowCombo("Label Pos", MP_HUD_PROP_KEY_HudMatchStatusLabelPos,
        {"Above", "Below", "Left", "Right", "Center"}),
    RowOffset(MP_HUD_PROP_KEY_HudMatchStatusLabelOfsX, MP_HUD_PROP_KEY_HudMatchStatusLabelOfsY, -128, 128,
        "Label Ofs X", "Label Ofs Y"),
    RowSeparator(),
    RowSectionHeader("Score Labels"),
    RowLineEdit("Battle", MP_HUD_PROP_KEY_HudMatchStatusLabelPoints),
    RowLineEdit("Bounty", MP_HUD_PROP_KEY_HudMatchStatusLabelOctoliths),
    RowLineEdit("Survival", MP_HUD_PROP_KEY_HudMatchStatusLabelLives),
    RowLineEdit("Defender", MP_HUD_PROP_KEY_HudMatchStatusLabelRingTime),
    RowLineEdit("Prime", MP_HUD_PROP_KEY_HudMatchStatusLabelPrimeTime),
    RowSeparator(),
    RowSectionHeader("Score Colors"),
    RowSubColor("Label",
        MP_HUD_PROP_KEY_HudMatchStatusLabelColorOverall,
        MP_HUD_PROP_KEY_HudMatchStatusLabelColorR,
        MP_HUD_PROP_KEY_HudMatchStatusLabelColorG,
        MP_HUD_PROP_KEY_HudMatchStatusLabelColorB),
    RowSubColor("Value",
        MP_HUD_PROP_KEY_HudMatchStatusValueColorOverall,
        MP_HUD_PROP_KEY_HudMatchStatusValueColorR,
        MP_HUD_PROP_KEY_HudMatchStatusValueColorG,
        MP_HUD_PROP_KEY_HudMatchStatusValueColorB),
    RowSubColor("Slash",
        MP_HUD_PROP_KEY_HudMatchStatusSepColorOverall,
        MP_HUD_PROP_KEY_HudMatchStatusSepColorR,
        MP_HUD_PROP_KEY_HudMatchStatusSepColorG,
        MP_HUD_PROP_KEY_HudMatchStatusSepColorB),
    RowSubColor("Goal",
        MP_HUD_PROP_KEY_HudMatchStatusGoalColorOverall,
        MP_HUD_PROP_KEY_HudMatchStatusGoalColorR,
        MP_HUD_PROP_KEY_HudMatchStatusGoalColorG,
        MP_HUD_PROP_KEY_HudMatchStatusGoalColorB),
    RowOutline("Score Outline", MP_OUTLINE_KEYS(HudMatchStatus)),
};

static const Row kRowsRank[] = {
    RowBuiltins(MP_HUD_PROP_KEY_HudRankShow,
        MP_HUD_PROP_KEY_HudRankColorR, MP_HUD_PROP_KEY_HudRankColorG, MP_HUD_PROP_KEY_HudRankColorB,
        MP_HUD_PROP_KEY_HudRankAnchor),
    RowOffset(MP_HUD_PROP_KEY_HudRankX, MP_HUD_PROP_KEY_HudRankY, -256, 256, "Offset X", "Offset Y"),
    RowLineEdit("Prefix", MP_HUD_PROP_KEY_HudRankPrefix),
    RowBool("Ordinal", MP_HUD_PROP_KEY_HudRankShowOrdinal),
    RowLineEdit("Suffix", MP_HUD_PROP_KEY_HudRankSuffix),
    RowAlign3("Align", MP_HUD_PROP_KEY_HudRankAlign),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_HudRankOpacity),
    RowOutline("Rank Outline", MP_OUTLINE_KEYS(HudRank)),
};

static const Row kRowsTimeLeft[] = {
    RowBuiltins(MP_HUD_PROP_KEY_HudTimeLeftShow,
        MP_HUD_PROP_KEY_HudTimeLeftColorR, MP_HUD_PROP_KEY_HudTimeLeftColorG, MP_HUD_PROP_KEY_HudTimeLeftColorB,
        MP_HUD_PROP_KEY_HudTimeLeftAnchor),
    RowOffset(MP_HUD_PROP_KEY_HudTimeLeftX, MP_HUD_PROP_KEY_HudTimeLeftY, -256, 256, "Offset X", "Offset Y"),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_HudTimeLeftOpacity),
    RowAlign3("Align", MP_HUD_PROP_KEY_HudTimeLeftAlign),
    RowOutline("Time Left Outline", MP_OUTLINE_KEYS(HudTimeLeft)),
};

static const Row kRowsTimeLimit[] = {
    RowBuiltins(MP_HUD_PROP_KEY_HudTimeLimitShow,
        MP_HUD_PROP_KEY_HudTimeLimitColorR, MP_HUD_PROP_KEY_HudTimeLimitColorG, MP_HUD_PROP_KEY_HudTimeLimitColorB,
        MP_HUD_PROP_KEY_HudTimeLimitAnchor),
    RowOffset(MP_HUD_PROP_KEY_HudTimeLimitX, MP_HUD_PROP_KEY_HudTimeLimitY, -256, 256, "Offset X", "Offset Y"),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_HudTimeLimitOpacity),
    RowAlign3("Align", MP_HUD_PROP_KEY_HudTimeLimitAlign),
    RowOutline("Time Limit Outline", MP_OUTLINE_KEYS(HudTimeLimit)),
};

static const Row kRowsBombLeft[] = {
    RowBuiltins(MP_HUD_PROP_KEY_HudBombLeftShow,
        MP_HUD_PROP_KEY_HudBombLeftColorR, MP_HUD_PROP_KEY_HudBombLeftColorG, MP_HUD_PROP_KEY_HudBombLeftColorB,
        MP_HUD_PROP_KEY_HudBombLeftAnchor),
    RowOffset(MP_HUD_PROP_KEY_HudBombLeftX, MP_HUD_PROP_KEY_HudBombLeftY, -256, 256, "Offset X", "Offset Y"),
    RowBool("Show Number", MP_HUD_PROP_KEY_HudBombLeftTextShow),
    RowAlign3("Align", MP_HUD_PROP_KEY_HudBombLeftAlign),
    RowLineEdit("Prefix", MP_HUD_PROP_KEY_HudBombLeftPrefix),
    RowLineEdit("Suffix", MP_HUD_PROP_KEY_HudBombLeftSuffix),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_HudBombLeftOpacity),
    RowOutline("Bomb Left Outline", MP_OUTLINE_KEYS(HudBombLeft)),
};

static const Row kRowsBombIcon[] = {
    RowBuiltins(MP_HUD_PROP_KEY_HudBombLeftIconShow,
        MP_HUD_PROP_KEY_HudBombLeftIconColorR, MP_HUD_PROP_KEY_HudBombLeftIconColorG, MP_HUD_PROP_KEY_HudBombLeftIconColorB,
        MP_HUD_PROP_KEY_HudBombLeftIconPosAnchor),
    RowOffset(MP_HUD_PROP_KEY_HudBombLeftIconPosX, MP_HUD_PROP_KEY_HudBombLeftIconPosY, -256, 256, "Pos X", "Pos Y"),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_HudBombIconOpacity),
    RowBool("Color Overlay", MP_HUD_PROP_KEY_HudBombLeftIconColorOverlay),
    RowCombo("Mode", MP_HUD_PROP_KEY_HudBombLeftIconMode, {"Relative", "Independent"}),
    RowSpin("Height", MP_HUD_PROP_KEY_HudBombIconHeight, 4, 64),
    RowOffset(MP_HUD_PROP_KEY_HudBombLeftIconOfsX, MP_HUD_PROP_KEY_HudBombLeftIconOfsY, -128, 128, "Offset X", "Offset Y"),
    RowAlign3("Align X", MP_HUD_PROP_KEY_HudBombLeftIconAnchorX),
    RowCombo("Align Y", MP_HUD_PROP_KEY_HudBombLeftIconAnchorY, {"Top", "Center", "Bottom"}),
    RowOutline("Bomb Icon Outline", MP_OUTLINE_KEYS(HudBombIcon)),
};

static const Row kRowsWeaponInventory[] = {
    RowBuiltins(MP_HUD_PROP_KEY_HudWeaponInventoryShow,
        MP_HUD_PROP_KEY_HudWeaponInventoryColorR,
        MP_HUD_PROP_KEY_HudWeaponInventoryColorG,
        MP_HUD_PROP_KEY_HudWeaponInventoryColorB,
        MP_HUD_PROP_KEY_HudWeaponInventoryAnchor),
    RowOffset(MP_HUD_PROP_KEY_HudWeaponInventoryX, MP_HUD_PROP_KEY_HudWeaponInventoryY, -256, 256, "Offset X", "Offset Y"),
    RowCombo("Orientation", MP_HUD_PROP_KEY_HudWeaponInventoryOrientation, {"Horizontal", "Vertical"}),
    RowAlign3("Align", MP_HUD_PROP_KEY_HudWeaponInventoryAlign),
    RowSpin("Icon Height", MP_HUD_PROP_KEY_HudWeaponInventoryIconHeight, 4, 48),
    RowSpin("Spacing", MP_HUD_PROP_KEY_HudWeaponInventorySpacing, 0, 32),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_HudWeaponInventoryOpacity),
    RowOpacity("Not Owned Opacity", MP_HUD_PROP_KEY_HudWeaponInventoryNotOwnedOpacity),
    RowOutline("Weapon Inventory Outline", MP_OUTLINE_KEYS(HudWeaponInventory)),
    RowOutline("Weapon Inventory Icon Outline", MP_OUTLINE_KEYS(HudWeaponInventoryIcon)),
    RowSeparator(),
    RowSectionHeader("Weapon Inventory Highlight"),
    RowBool("Highlight Current Weapon", MP_HUD_PROP_KEY_HudWeaponInventoryHighlightEnable),
    RowColor("Highlight Color",
        MP_HUD_PROP_KEY_HudWeaponInventoryHighlightColorR,
        MP_HUD_PROP_KEY_HudWeaponInventoryHighlightColorG,
        MP_HUD_PROP_KEY_HudWeaponInventoryHighlightColorB),
    RowOpacity("Highlight Opacity", MP_HUD_PROP_KEY_HudWeaponInventoryHighlightOpacity),
    RowDoubleSpin("Highlight Thickness", MP_HUD_PROP_KEY_HudWeaponInventoryHighlightThickness, 0.1, 8.0, 0.25),
    RowSpin("Highlight Padding", MP_HUD_PROP_KEY_HudWeaponInventoryHighlightPadding, 0, 16),
    RowSpin("Highlight Corner Radius", MP_HUD_PROP_KEY_HudWeaponInventoryHighlightCornerRadius, 0, 16),
    RowSpin("Highlight Offset Left",   MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetLeft,   -16, 32),
    RowSpin("Highlight Offset Right",  MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetRight,  -16, 32),
    RowSpin("Highlight Offset Top",    MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetTop,    -16, 32),
    RowSpin("Highlight Offset Bottom", MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetBottom, -16, 32),
};

static const Row kRowsRadar[] = {
    RowBuiltins(MP_HUD_PROP_KEY_BtmOverlayEnable,
        nullptr, nullptr, nullptr,
        MP_HUD_PROP_KEY_BtmOverlayAnchor),
    RowOffset(MP_HUD_PROP_KEY_BtmOverlayDstX, MP_HUD_PROP_KEY_BtmOverlayDstY, -256, 256, "Dst X", "Dst Y"),
    RowSpin("Dst Size", MP_HUD_PROP_KEY_BtmOverlayDstSize, 16, 128),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_BtmOverlayOpacity),
    RowSpin("Src Radius", MP_HUD_PROP_KEY_BtmOverlaySrcRadius, 10, 120),
    RowColor("Radar Color",
        MP_HUD_PROP_KEY_BtmOverlayRadarColorR,
        MP_HUD_PROP_KEY_BtmOverlayRadarColorG,
        MP_HUD_PROP_KEY_BtmOverlayRadarColorB),
    RowBool("Use Hunter Color", MP_HUD_PROP_KEY_BtmOverlayRadarColorUseHunter),
    RowOutline("Radar Outline", MP_OUTLINE_KEYS(BtmOverlay)),
    RowOutline("Frame Outline", MP_OUTLINE_KEYS(BtmOverlayFrame)),
};

// Crosshair's flat row body (no composites). populateForCrosshair() keeps
// its own surrounding lifecycle (m_currentElem = -2, title, m_populating)
// and calls populateFromRowTable(kRowsCrosshair, ...) for this body.
static const Row kRowsCrosshair[] = {
    RowColor("Color", MP_HUD_PROP_KEY_CrosshairColorR, MP_HUD_PROP_KEY_CrosshairColorG, MP_HUD_PROP_KEY_CrosshairColorB),
    RowSpin("Scale %", MP_HUD_PROP_KEY_CrosshairScale, 100, 800),
    RowBool("Outline", MP_HUD_PROP_KEY_CrosshairOutline),
    RowOpacity("Outline Opacity", MP_HUD_PROP_KEY_CrosshairOutlineOpacity),
    RowSpin("Outline Thick.", MP_HUD_PROP_KEY_CrosshairOutlineThickness, 1, 10),
    RowBool("Center Dot", MP_HUD_PROP_KEY_CrosshairCenterDot),
    RowOpacity("Dot Opacity", MP_HUD_PROP_KEY_CrosshairDotOpacity),
    RowSpin("Dot Thick.", MP_HUD_PROP_KEY_CrosshairDotThickness, 1, 10),
    RowCombo("Dot Shape", MP_HUD_PROP_KEY_CrosshairDotShape, {"Square", "Circle"}),
    RowBool("Custom Dot Color", MP_HUD_PROP_KEY_CrosshairDotCustomColor),
    RowColor("Dot Color",
        MP_HUD_PROP_KEY_CrosshairDotColorR, MP_HUD_PROP_KEY_CrosshairDotColorG, MP_HUD_PROP_KEY_CrosshairDotColorB),
    RowBool("T-Style", MP_HUD_PROP_KEY_CrosshairTStyle),

    RowSeparator(),
    RowSectionHeader("Zoom Crosshair"),
    RowBool("Zoom Stage", MP_HUD_PROP_KEY_CrosshairZoomStageEnable),
    RowSpin("Zoom Base Scale %", MP_HUD_PROP_KEY_CrosshairZoomScale, 10, 200),
    RowSpin("Zoom Base Opacity %", MP_HUD_PROP_KEY_CrosshairZoomOpacity, 0, 100),
    RowBool("Zoom Scope", MP_HUD_PROP_KEY_CrosshairZoomScopeEnable),
    RowSpin("Scope Radius", MP_HUD_PROP_KEY_CrosshairZoomScopeRadius, 4, 1024),
    RowSpin("Scope Gap", MP_HUD_PROP_KEY_CrosshairZoomScopeGap, 0, 64),
    RowSpin("Scope Thick.", MP_HUD_PROP_KEY_CrosshairZoomScopeThickness, 1, 12),
    RowBool("Scope Center Dot", MP_HUD_PROP_KEY_CrosshairZoomScopeCenterDot),
    RowCombo("Scope Dot Shape", MP_HUD_PROP_KEY_CrosshairZoomScopeDotShape, {"Square", "Circle"}),
    RowSpin("Scope Dot Size", MP_HUD_PROP_KEY_CrosshairZoomScopeDotSize, 1, 32),
    RowSpin("Scope Dot Opacity %", MP_HUD_PROP_KEY_CrosshairZoomScopeDotOpacity, 0, 100),
    RowBool("Custom Scope Dot Color", MP_HUD_PROP_KEY_CrosshairZoomScopeDotCustomColor),
    RowColor("Scope Dot Color",
        MP_HUD_PROP_KEY_CrosshairZoomScopeDotColorR,
        MP_HUD_PROP_KEY_CrosshairZoomScopeDotColorG,
        MP_HUD_PROP_KEY_CrosshairZoomScopeDotColorB),
    RowSpin("Scope Opacity %", MP_HUD_PROP_KEY_CrosshairZoomScopeOpacity, 0, 100),
    RowBool("Zoom Transition", MP_HUD_PROP_KEY_CrosshairZoomTransitionEnable),
    RowCombo("Transition Style", MP_HUD_PROP_KEY_CrosshairZoomTransitionStyle,
        {"Fade", "Staged", "Glitch", "Glitch2", "Snap", "Digital",
         "Pulse Wave", "Magic Circle", "SF Movie",
         "Tactical Lock", "Sniper Optics", "Drone LIDAR", "Beam Charge"}),
    RowSpin("Transition Speed %", MP_HUD_PROP_KEY_CrosshairZoomTransitionSpeed, 25, 400),
    RowBool("Pulse Ring", MP_HUD_PROP_KEY_CrosshairZoomTransitionPulseEnable),
    RowSpin("Pulse Strength %", MP_HUD_PROP_KEY_CrosshairZoomTransitionPulseStrength, 0, 100),

    RowSeparator(),
    RowSectionHeader("Inner Lines"),
    RowBool("Show", MP_HUD_PROP_KEY_CrosshairInnerShow),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_CrosshairInnerOpacity),
    RowSpin("Length X", MP_HUD_PROP_KEY_CrosshairInnerLengthX, 0, 64),
    RowSpin("Length Y", MP_HUD_PROP_KEY_CrosshairInnerLengthY, 0, 64),
    RowBool("Link XY", MP_HUD_PROP_KEY_CrosshairInnerLinkXY),
    RowSpin("Thickness", MP_HUD_PROP_KEY_CrosshairInnerThickness, 1, 10),
    RowSpin("Offset", MP_HUD_PROP_KEY_CrosshairInnerOffset, 0, 64),

    RowSeparator(),
    RowSectionHeader("Outer Lines"),
    RowBool("Show", MP_HUD_PROP_KEY_CrosshairOuterShow),
    RowOpacity("Opacity", MP_HUD_PROP_KEY_CrosshairOuterOpacity),
    RowSpin("Length X", MP_HUD_PROP_KEY_CrosshairOuterLengthX, 0, 64),
    RowSpin("Length Y", MP_HUD_PROP_KEY_CrosshairOuterLengthY, 0, 64),
    RowBool("Link XY", MP_HUD_PROP_KEY_CrosshairOuterLinkXY),
    RowSpin("Thickness", MP_HUD_PROP_KEY_CrosshairOuterThickness, 1, 10),
    RowSpin("Offset", MP_HUD_PROP_KEY_CrosshairOuterOffset, 0, 64),
};
