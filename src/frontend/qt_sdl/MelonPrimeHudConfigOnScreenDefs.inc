// This file is a unity-build fragment included by MelonPrimeHudConfigOnScreenUnity.inc.
// Do not add it to CMakeLists.txt.

// =========================================================================
//  Element property descriptors and element table
// =========================================================================

enum class EditPropType : uint8_t { Bool, Int, Float, String, SubColor, Color, Enum };

struct HudEditPropDesc {
    const char* label;       // tiny label e.g. "Align", "Pfx"
    EditPropType type;
    const char* cfgKey;      // config key. SubColor: overall-bool key
    int minVal, maxVal;      // Int range. Float: value*100 range (e.g. 0-100 for 0.0-1.0)
    int step;                // 0 = use default (1 for Int, 5 for Float)
    const char* extra1;      // SubColor: R key
    const char* extra2;      // SubColor: G key
    const char* extra3;      // SubColor: B key
};

struct HudEditElemDesc {
    const char* name;
    const char* anchorKey;
    const char* ofsXKey;
    const char* ofsYKey;
    const char* orientKey;   // nullptr if no orientation toggle
    const char* lengthKey;   // nullptr if no resize handles
    const char* widthKey;    // nullptr if no resize handles
    const char* posModeKey;  // nullptr if no PosMode switch
    const char* showKey;     // nullptr = always visible (no toggle)
    const char* colorRKey;   // nullptr = no color picker
    const char* colorGKey;
    const char* colorBKey;
    const HudEditPropDesc* props;   // additional properties (nullable)
    int propCount;                  // number of props
};

#define HUD_EDIT_PROP_COUNT(arr) static_cast<int>(sizeof(arr) / sizeof((arr)[0]))

// ── Enum label helpers ─────────────────────────────────────────────────────
static const char* kEnumAlign3      = "Left|Center|Right";
static const char* kEnumGaugeAnchor = "Below|Above|Right|Left|Center";
static const char* kEnumRelIndep    = "Relative|Independent";
static const char* kEnumPosMode3    = "Gauge\u2192Text|Independent|Text\u2192Gauge";
static const char* kEnumAnchorY     = "Top|Center|Bottom";
static const char* kEnumLabelPos    = "Above|Below|Left|Right|Center";
static const char* kEnumLayout      = "Standard|Alternative";
static const char* kEnumOrientation = "Horiz|Vert";
static const char* kEnumDotShape    = "Square|Circle";
static const char* kEnumGaugeAlign  = "Start|Center|End";
static const char* kEnumEnemyTargetHpMode = "Segmented|Single Bar|Number Only|Off";
static const char* kEnumZoomTransitionStyle =
    "Fade|Staged|Glitch|Glitch2|Snap|Digital|Pulse|Magic Circle|SF Movie|"
    "Tactical Lock|Sniper Optics|Drone LIDAR|Beam Charge";

static QString EditEnumLabel(const char* items, int val) {
    if (!items || val < 0) return QString::number(val);
    const char* p = items;
    for (int idx = 0; idx < val; ++idx) {
        while (*p && *p != '|') ++p;
        if (*p == '|') ++p; else return QString::number(val);
    }
    const char* end = p;
    while (*end && *end != '|') ++end;
    return UiText::Tr(p, static_cast<int>(end - p));
}

#include "MelonPrimeHudPropSchema.inc"
#include "MelonPrimeHudConfigOnScreenEditProps.inc"

// Scoreboard edit descriptors are kept on this source surface so the schema
// generator can merge them with the generated legacy descriptor table.
#if 0
static const HudEditPropDesc kPropsScoreboard[] = {
    {"Native Layout", EditPropType::Bool, MP_HUD_PROP_KEY_HudScoreboardNativeLayout, 0, 0, 0, nullptr, nullptr, nullptr},
    {"Scoreboard Scale %", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardScale, 10, 800, 1, nullptr, nullptr, nullptr},
    {"Text Scale %", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardTextScale, 10, 800, 1, nullptr, nullptr, nullptr},
    {"Hunter Icon Height", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardHunterIconHeight, 0, 48, 1, nullptr, nullptr, nullptr},
    {"Stars Height", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardStarsHeight, 0, 24, 1, nullptr, nullptr, nullptr},
    {"Opacity", EditPropType::Float, MP_HUD_PROP_KEY_HudScoreboardOpacity, 0, 100, 5, nullptr, nullptr, nullptr},
    {"Background Opacity", EditPropType::Float, MP_HUD_PROP_KEY_HudScoreboardBackgroundOpacity, 0, 100, 5, nullptr, nullptr, nullptr},
    {"Outline", EditPropType::Bool, MP_HUD_PROP_KEY_HudScoreboardOutline, 0, 0, 0, nullptr, nullptr, nullptr},
    {"Outline Color", EditPropType::Color, MP_HUD_PROP_KEY_HudScoreboardOutlineColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardOutlineColorG, MP_HUD_PROP_KEY_HudScoreboardOutlineColorB, nullptr},
    {"Outline Opacity", EditPropType::Float, MP_HUD_PROP_KEY_HudScoreboardOutlineOpacity, 0, 100, 5, nullptr, nullptr, nullptr},
    {"Outline Thick.", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardOutlineThickness, 1, 10, 1, nullptr, nullptr, nullptr},
    {"Header Color", EditPropType::Color, MP_HUD_PROP_KEY_HudScoreboardHeaderColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardHeaderColorG, MP_HUD_PROP_KEY_HudScoreboardHeaderColorB, nullptr},
    {"Local Player Color", EditPropType::Color, MP_HUD_PROP_KEY_HudScoreboardLocalPlayerColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardLocalPlayerColorG, MP_HUD_PROP_KEY_HudScoreboardLocalPlayerColorB, nullptr},
    {"Team Row Color", EditPropType::Color, MP_HUD_PROP_KEY_HudScoreboardTeamRowColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardTeamRowColorG, MP_HUD_PROP_KEY_HudScoreboardTeamRowColorB, nullptr},
    {"Background Color", EditPropType::Color, MP_HUD_PROP_KEY_HudScoreboardBackgroundColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardBackgroundColorG, MP_HUD_PROP_KEY_HudScoreboardBackgroundColorB, nullptr},
    // Auto layout only: the native layout takes its gaps and its rank
    // indicator from the ROM, so these four do nothing while it is on.
    {"Column Spacing", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardColumnSpacing, 0, 32, 1, nullptr, nullptr, nullptr},
    {"Right Padding", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardRightPadding, 0, 32, 1, nullptr, nullptr, nullptr},
    {"Row Spacing", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardRowSpacing, 0, 16, 1, nullptr, nullptr, nullptr},
    {"Rank", EditPropType::Bool, MP_HUD_PROP_KEY_HudScoreboardRankShow, 0, 0, 0, nullptr, nullptr, nullptr},
};

static const HudEditElemDesc kEditElemScoreboard[] = {
    {
        "Scoreboard",
        MP_HUD_PROP_KEY_HudScoreboardAnchor, MP_HUD_PROP_KEY_HudScoreboardX, MP_HUD_PROP_KEY_HudScoreboardY,
        nullptr, nullptr, nullptr, nullptr,
        MP_HUD_PROP_KEY_HudScoreboardShow,
        MP_HUD_PROP_KEY_HudScoreboardTextColorR, MP_HUD_PROP_KEY_HudScoreboardTextColorG,
        MP_HUD_PROP_KEY_HudScoreboardTextColorB,
        kPropsScoreboard, HUD_EDIT_PROP_COUNT(kPropsScoreboard)
    },
};

// Enemy Target edit descriptors share this generator-source surface with the
// scoreboard for the same reason: the generated legacy table predates them.
static const HudEditPropDesc kPropsEnemyTarget[] = {
    {"Scale %", EditPropType::Int, MP_HUD_PROP_KEY_HudEnemyTargetScale, 10, 800, 1, nullptr, nullptr, nullptr},
    {"Text Scale %", EditPropType::Int, MP_HUD_PROP_KEY_HudEnemyTargetTextScale, 10, 800, 1, nullptr, nullptr, nullptr},
    {"Row Spacing", EditPropType::Int, MP_HUD_PROP_KEY_HudEnemyTargetRowSpacing, 0, 16, 1, nullptr, nullptr, nullptr},
    {"Opacity", EditPropType::Float, MP_HUD_PROP_KEY_HudEnemyTargetOpacity, 0, 100, 5, nullptr, nullptr, nullptr},
    {"Name", EditPropType::Bool, MP_HUD_PROP_KEY_HudEnemyTargetNameShow, 0, 0, 0, nullptr, nullptr, nullptr},
    {"Team Color", EditPropType::Bool, MP_HUD_PROP_KEY_HudEnemyTargetTeamColor, 0, 0, 0, nullptr, nullptr, nullptr},
    {"Hunter Icon", EditPropType::Bool, MP_HUD_PROP_KEY_HudEnemyTargetHunterIconShow, 0, 0, 0, nullptr, nullptr, nullptr},
    {"Hunter Icon Height", EditPropType::Int, MP_HUD_PROP_KEY_HudEnemyTargetHunterIconHeight, 0, 48, 1, nullptr, nullptr, nullptr},
    {"HP Mode", EditPropType::Enum, MP_HUD_PROP_KEY_HudEnemyTargetHpMode, 0, 3, 1, kEnumEnemyTargetHpMode, nullptr, nullptr},
    {"HP Number", EditPropType::Bool, MP_HUD_PROP_KEY_HudEnemyTargetHpNumberShow, 0, 0, 0, nullptr, nullptr, nullptr},
    {"Bar Length", EditPropType::Int, MP_HUD_PROP_KEY_HudEnemyTargetBarLength, 8, 192, 1, nullptr, nullptr, nullptr},
    {"Bar Height", EditPropType::Int, MP_HUD_PROP_KEY_HudEnemyTargetBarHeight, 1, 20, 1, nullptr, nullptr, nullptr},
    {"HP Color", EditPropType::Color, MP_HUD_PROP_KEY_HudEnemyTargetHpColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudEnemyTargetHpColorG, MP_HUD_PROP_KEY_HudEnemyTargetHpColorB, nullptr},
    {"HP Back Color", EditPropType::Color, MP_HUD_PROP_KEY_HudEnemyTargetHpBackColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudEnemyTargetHpBackColorG, MP_HUD_PROP_KEY_HudEnemyTargetHpBackColorB, nullptr},
    {"Score", EditPropType::Bool, MP_HUD_PROP_KEY_HudEnemyTargetScoreShow, 0, 0, 0, nullptr, nullptr, nullptr},
    {"Score Color", EditPropType::Color, MP_HUD_PROP_KEY_HudEnemyTargetScoreColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudEnemyTargetScoreColorG, MP_HUD_PROP_KEY_HudEnemyTargetScoreColorB, nullptr},
    {"Outline", EditPropType::Bool, MP_HUD_PROP_KEY_HudEnemyTargetOutline, 0, 0, 0, nullptr, nullptr, nullptr},
    {"Outline Color", EditPropType::Color, MP_HUD_PROP_KEY_HudEnemyTargetOutlineColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudEnemyTargetOutlineColorG, MP_HUD_PROP_KEY_HudEnemyTargetOutlineColorB, nullptr},
    {"Outline Opacity", EditPropType::Float, MP_HUD_PROP_KEY_HudEnemyTargetOutlineOpacity, 0, 100, 5, nullptr, nullptr, nullptr},
    {"Outline Thick.", EditPropType::Int, MP_HUD_PROP_KEY_HudEnemyTargetOutlineThickness, 1, 10, 1, nullptr, nullptr, nullptr},
};

static const HudEditElemDesc kEditElemEnemyTarget[] = {
    {
        "Enemy\nTarget",
        MP_HUD_PROP_KEY_HudEnemyTargetAnchor, MP_HUD_PROP_KEY_HudEnemyTargetX, MP_HUD_PROP_KEY_HudEnemyTargetY,
        nullptr, nullptr, nullptr, nullptr,
        MP_HUD_PROP_KEY_HudEnemyTargetShow,
        MP_HUD_PROP_KEY_HudEnemyTargetColorR, MP_HUD_PROP_KEY_HudEnemyTargetColorG,
        MP_HUD_PROP_KEY_HudEnemyTargetColorB,
        kPropsEnemyTarget, HUD_EDIT_PROP_COUNT(kPropsEnemyTarget)
    },
};
#endif

// Crosshair panel state

// Text-scale slider drag state

// Auto-scale global cap slider drag state

static int CountCrosshairRows() {
    return 1 + kCrosshairNormalCount + 3; // color + normal props + Zoom/Inner/Outer headers
}

static const HudEditPropDesc* GetCrosshairPropForRow(int rowIdx, bool& isColorRow,
                                                     bool& isZoomHeader,
                                                     bool& isInnerHeader, bool& isOuterHeader)
{
    isColorRow = isZoomHeader = isInnerHeader = isOuterHeader = false;
    if (rowIdx == 0) { isColorRow = true; return nullptr; }
    int idx = rowIdx - 1;
    if (idx < kCrosshairNormalCount) return &kPropsCrosshairNormal[idx];
    idx -= kCrosshairNormalCount;
    if (idx == 0) { isZoomHeader = true; return nullptr; }
    idx -= 1;
    if (idx == 0) { isInnerHeader = true; return nullptr; }
    idx -= 1;
    if (idx == 0) { isOuterHeader = true; return nullptr; }
    return nullptr;
}


// Sample text used for element box previews (must align with kEditElems indices)
// Width sample text for element boxes. Match Status uses a two-line format separately.
static const char* const kEditElemSampleText[] = {
    "100",       // 0: HP
    nullptr,     // 1: HP Gauge
    "50",        // 2: Weapon/Ammo (number only, no weapon name prefix)
    nullptr,     // 3: Weapon Icon
    nullptr,     // 4: Ammo Gauge
    "points",    // 5: Match Status — width based on label row; height handled specially
    "1st",       // 6: Rank
    "2:30",      // 7: Time Left
    "5:00",      // 8: Time Limit
    "Bombs",     // 9: Bomb Left
    nullptr,     // 10: Bomb Icon
    nullptr,     // 11: Radar
    nullptr,     // 12: Weapon Inventory
    nullptr,     // 13: Crosshair
    nullptr,     // 14: Scoreboard (drawn with the deterministic composite preview)
    nullptr,     // 15: Enemy Target (drawn with the deterministic composite preview)
};

static_assert(
    sizeof(kEditElemSampleText) / sizeof(kEditElemSampleText[0]) == kEditElemCount,
    "kEditElemSampleText must have one entry per On-Screen Edit element.");

