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
};

