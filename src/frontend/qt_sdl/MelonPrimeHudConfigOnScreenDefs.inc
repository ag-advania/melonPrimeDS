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
    {"Column Spacing", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardColumnSpacing, 0, 32, 1, nullptr, nullptr, nullptr},
    {"Right Padding", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardRightPadding, 0, 32, 1, nullptr, nullptr, nullptr},
    {"Scoreboard Scale %", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardScale, 10, 800, 1, nullptr, nullptr, nullptr},
    {"Row Spacing", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardRowSpacing, 0, 16, 1, nullptr, nullptr, nullptr},
    {"Hunter Icon Height", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardHunterIconHeight, 0, 48, 1, nullptr, nullptr, nullptr},
    {"Stars Height", EditPropType::Int, MP_HUD_PROP_KEY_HudScoreboardStarsHeight, 0, 24, 1, nullptr, nullptr, nullptr},
    {"Opacity", EditPropType::Float, MP_HUD_PROP_KEY_HudScoreboardOpacity, 0, 100, 5, nullptr, nullptr, nullptr},
    {"Background Opacity", EditPropType::Float, MP_HUD_PROP_KEY_HudScoreboardBackgroundOpacity, 0, 100, 5, nullptr, nullptr, nullptr},
    {"Header Color", EditPropType::Color, MP_HUD_PROP_KEY_HudScoreboardHeaderColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardHeaderColorG, MP_HUD_PROP_KEY_HudScoreboardHeaderColorB, nullptr},
    {"Local Player Color", EditPropType::Color, MP_HUD_PROP_KEY_HudScoreboardLocalPlayerColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardLocalPlayerColorG, MP_HUD_PROP_KEY_HudScoreboardLocalPlayerColorB, nullptr},
    {"Team Row Color", EditPropType::Color, MP_HUD_PROP_KEY_HudScoreboardTeamRowColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardTeamRowColorG, MP_HUD_PROP_KEY_HudScoreboardTeamRowColorB, nullptr},
    {"Background Color", EditPropType::Color, MP_HUD_PROP_KEY_HudScoreboardBackgroundColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardBackgroundColorG, MP_HUD_PROP_KEY_HudScoreboardBackgroundColorB, nullptr},
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
static const char* const kEditElemSampleText[kEditElemCount] = {
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
};

