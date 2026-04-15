// This file is a unity-build fragment included by MelonPrimeHudConfigOnScreen.cpp.
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
static const char* kEnumGaugeAlign  = "Start|Center|End";

static QString EditEnumLabel(const char* items, int val) {
    if (!items || val < 0) return QString::number(val);
    const char* p = items;
    for (int idx = 0; idx < val; ++idx) {
        while (*p && *p != '|') ++p;
        if (*p == '|') ++p; else return QString::number(val);
    }
    const char* end = p;
    while (*end && *end != '|') ++end;
    return QString::fromUtf8(p, static_cast<int>(end - p));
}

static const HudEditPropDesc kPropsHp[] = {
    {"Prefix",    EditPropType::String, "Metroid.Visual.HudHpPrefix", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Align",     EditPropType::Enum,   "Metroid.Visual.HudHpAlign", 0, 2, 1, kEnumAlign3, nullptr, nullptr},
    {"Auto Color",EditPropType::Bool,   "Metroid.Visual.HudHpTextAutoColor", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Opacity",   EditPropType::Float,  "Metroid.Visual.HudHpOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsHpGauge[] = {
    // Appearance
    {"Auto Color",   EditPropType::Bool, "Metroid.Visual.HudHpGaugeAutoColor", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Orient",       EditPropType::Enum, "Metroid.Visual.HudHpGaugeOrientation", 0, 1, 1, kEnumOrientation, nullptr, nullptr},
    {"Length",       EditPropType::Int,  "Metroid.Visual.HudHpGaugeLength", 1, 192, 1, nullptr, nullptr, nullptr},
    {"Width",        EditPropType::Int,  "Metroid.Visual.HudHpGaugeWidth", 1, 20, 1, nullptr, nullptr, nullptr},
    {"Align",        EditPropType::Enum, "Metroid.Visual.HudHpGaugeAlign", 0, 2, 1, kEnumGaugeAlign, nullptr, nullptr},
    {"Opacity",      EditPropType::Float,"Metroid.Visual.HudHpGaugeOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    // Position mode selector
    {"Pos Mode",     EditPropType::Enum, "Metroid.Visual.HudHpGaugePosMode", 0, 2, 1, kEnumPosMode3, nullptr, nullptr},
    // Mode 0: gauge relative to text
    {"Gauge Side",   EditPropType::Enum, "Metroid.Visual.HudHpGaugeAnchor", 0, 4, 1, kEnumGaugeAnchor, nullptr, nullptr},
    {"Offset X",     EditPropType::Int,  "Metroid.Visual.HudHpGaugeOffsetX", -128, 128, 1, nullptr, nullptr, nullptr},
    {"Offset Y",     EditPropType::Int,  "Metroid.Visual.HudHpGaugeOffsetY", -128, 128, 1, nullptr, nullptr, nullptr},
    // Modes 1 & 2: gauge at independent absolute position
    {"Gauge X",      EditPropType::Int,  "Metroid.Visual.HudHpGaugePosX", -256, 256, 1, nullptr, nullptr, nullptr},
    {"Gauge Y",      EditPropType::Int,  "Metroid.Visual.HudHpGaugePosY", -256, 256, 1, nullptr, nullptr, nullptr},
    // Mode 2: text relative to gauge
    {"Text Side",    EditPropType::Enum, "Metroid.Visual.HudHpTextAnchor", 0, 4, 1, kEnumGaugeAnchor, nullptr, nullptr},
    {"Text Ofs X",   EditPropType::Int,  "Metroid.Visual.HudHpTextOffsetX", -128, 128, 1, nullptr, nullptr, nullptr},
    {"Text Ofs Y",   EditPropType::Int,  "Metroid.Visual.HudHpTextOffsetY", -128, 128, 1, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsWeaponAmmo[] = {
    {"Prefix", EditPropType::String, "Metroid.Visual.HudAmmoPrefix", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Align",  EditPropType::Enum,   "Metroid.Visual.HudAmmoAlign", 0, 2, 1, kEnumAlign3, nullptr, nullptr},
    {"Layout", EditPropType::Enum,   "Metroid.Visual.HudWeaponLayout", 0, 1, 1, kEnumLayout, nullptr, nullptr},
    {"Opacity",EditPropType::Float,  "Metroid.Visual.HudWeaponOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsWpnIcon[] = {
    {"Mode",     EditPropType::Enum, "Metroid.Visual.HudWeaponIconMode", 0, 1, 1, kEnumRelIndep, nullptr, nullptr},
    {"Height",   EditPropType::Int,  "Metroid.Visual.HudWeaponIconHeight", 4, 64, 1, nullptr, nullptr, nullptr},
    {"Ofs X",    EditPropType::Int,  "Metroid.Visual.HudWeaponIconOffsetX", -128, 128, 1, nullptr, nullptr, nullptr},
    {"Ofs Y",    EditPropType::Int,  "Metroid.Visual.HudWeaponIconOffsetY", -128, 128, 1, nullptr, nullptr, nullptr},
    {"Anchor X", EditPropType::Enum, "Metroid.Visual.HudWeaponIconAnchorX", 0, 2, 1, kEnumAlign3, nullptr, nullptr},
    {"Anchor Y", EditPropType::Enum, "Metroid.Visual.HudWeaponIconAnchorY", 0, 2, 1, kEnumAnchorY, nullptr, nullptr},
    {"Opacity",  EditPropType::Float,"Metroid.Visual.HudWpnIconOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsAmmoGauge[] = {
    // Appearance
    {"Orient",       EditPropType::Enum, "Metroid.Visual.HudAmmoGaugeOrientation", 0, 1, 1, kEnumOrientation, nullptr, nullptr},
    {"Length",       EditPropType::Int,  "Metroid.Visual.HudAmmoGaugeLength", 1, 192, 1, nullptr, nullptr, nullptr},
    {"Width",        EditPropType::Int,  "Metroid.Visual.HudAmmoGaugeWidth", 1, 20, 1, nullptr, nullptr, nullptr},
    {"Align",        EditPropType::Enum, "Metroid.Visual.HudAmmoGaugeAlign", 0, 2, 1, kEnumGaugeAlign, nullptr, nullptr},
    {"Opacity",      EditPropType::Float,"Metroid.Visual.HudAmmoGaugeOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    // Position mode selector
    {"Pos Mode",     EditPropType::Enum, "Metroid.Visual.HudAmmoGaugePosMode", 0, 2, 1, kEnumPosMode3, nullptr, nullptr},
    // Mode 0: gauge relative to text
    {"Gauge Side",   EditPropType::Enum, "Metroid.Visual.HudAmmoGaugeAnchor", 0, 4, 1, kEnumGaugeAnchor, nullptr, nullptr},
    {"Offset X",     EditPropType::Int,  "Metroid.Visual.HudAmmoGaugeOffsetX", -128, 128, 1, nullptr, nullptr, nullptr},
    {"Offset Y",     EditPropType::Int,  "Metroid.Visual.HudAmmoGaugeOffsetY", -128, 128, 1, nullptr, nullptr, nullptr},
    // Modes 1 & 2: gauge at independent absolute position
    {"Gauge X",      EditPropType::Int,  "Metroid.Visual.HudAmmoGaugePosX", -256, 256, 1, nullptr, nullptr, nullptr},
    {"Gauge Y",      EditPropType::Int,  "Metroid.Visual.HudAmmoGaugePosY", -256, 256, 1, nullptr, nullptr, nullptr},
    // Mode 2: text relative to gauge
    {"Text Side",    EditPropType::Enum, "Metroid.Visual.HudAmmoTextAnchor", 0, 4, 1, kEnumGaugeAnchor, nullptr, nullptr},
    {"Text Ofs X",   EditPropType::Int,  "Metroid.Visual.HudAmmoTextOffsetX", -128, 128, 1, nullptr, nullptr, nullptr},
    {"Text Ofs Y",   EditPropType::Int,  "Metroid.Visual.HudAmmoTextOffsetY", -128, 128, 1, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsMatchStatus[] = {
    {"Label Pos",  EditPropType::Enum, "Metroid.Visual.HudMatchStatusLabelPos", 0, 4, 1, kEnumLabelPos, nullptr, nullptr},
    {"Label Ofs X",EditPropType::Int,  "Metroid.Visual.HudMatchStatusLabelOfsX", -64, 64, 1, nullptr, nullptr, nullptr},
    {"Label Ofs Y",EditPropType::Int,  "Metroid.Visual.HudMatchStatusLabelOfsY", -64, 64, 1, nullptr, nullptr, nullptr},
    {"Points",   EditPropType::String, "Metroid.Visual.HudMatchStatusLabelPoints", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Octolith", EditPropType::String, "Metroid.Visual.HudMatchStatusLabelOctoliths", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Lives",    EditPropType::String, "Metroid.Visual.HudMatchStatusLabelLives", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Rings",    EditPropType::String, "Metroid.Visual.HudMatchStatusLabelRingTime", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Primes",   EditPropType::String, "Metroid.Visual.HudMatchStatusLabelPrimeTime", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Label",    EditPropType::SubColor,"Metroid.Visual.HudMatchStatusLabelColorOverall", 0, 0, 0,
        "Metroid.Visual.HudMatchStatusLabelColorR","Metroid.Visual.HudMatchStatusLabelColorG","Metroid.Visual.HudMatchStatusLabelColorB"},
    {"Value",    EditPropType::SubColor,"Metroid.Visual.HudMatchStatusValueColorOverall", 0, 0, 0,
        "Metroid.Visual.HudMatchStatusValueColorR","Metroid.Visual.HudMatchStatusValueColorG","Metroid.Visual.HudMatchStatusValueColorB"},
    {"Separator",EditPropType::SubColor,"Metroid.Visual.HudMatchStatusSepColorOverall", 0, 0, 0,
        "Metroid.Visual.HudMatchStatusSepColorR","Metroid.Visual.HudMatchStatusSepColorG","Metroid.Visual.HudMatchStatusSepColorB"},
    {"Goal", EditPropType::SubColor,"Metroid.Visual.HudMatchStatusGoalColorOverall", 0, 0, 0,
        "Metroid.Visual.HudMatchStatusGoalColorR","Metroid.Visual.HudMatchStatusGoalColorG","Metroid.Visual.HudMatchStatusGoalColorB"},
    {"Opacity",  EditPropType::Float,"Metroid.Visual.HudMatchStatusOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsRank[] = {
    {"Prefix", EditPropType::String, "Metroid.Visual.HudRankPrefix", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Suffix", EditPropType::String, "Metroid.Visual.HudRankSuffix", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Align",  EditPropType::Enum,   "Metroid.Visual.HudRankAlign", 0, 2, 1, kEnumAlign3, nullptr, nullptr},
    {"Ordinal",EditPropType::Bool,   "Metroid.Visual.HudRankShowOrdinal", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Opacity",EditPropType::Float,  "Metroid.Visual.HudRankOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsTimeLeft[] = {
    {"Align",  EditPropType::Enum,  "Metroid.Visual.HudTimeLeftAlign", 0, 2, 1, kEnumAlign3, nullptr, nullptr},
    {"Opacity",EditPropType::Float, "Metroid.Visual.HudTimeLeftOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsTimeLimit[] = {
    {"Align",  EditPropType::Enum,  "Metroid.Visual.HudTimeLimitAlign", 0, 2, 1, kEnumAlign3, nullptr, nullptr},
    {"Opacity",EditPropType::Float, "Metroid.Visual.HudTimeLimitOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsBombLeft[] = {
    {"Text",   EditPropType::Bool,   "Metroid.Visual.HudBombLeftTextShow", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Prefix", EditPropType::String, "Metroid.Visual.HudBombLeftPrefix", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Suffix", EditPropType::String, "Metroid.Visual.HudBombLeftSuffix", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Align",  EditPropType::Enum,   "Metroid.Visual.HudBombLeftAlign", 0, 2, 1, kEnumAlign3, nullptr, nullptr},
    {"Opacity",EditPropType::Float,  "Metroid.Visual.HudBombLeftOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsBombIcon[] = {
    {"Mode",     EditPropType::Enum, "Metroid.Visual.HudBombLeftIconMode", 0, 1, 1, kEnumRelIndep, nullptr, nullptr},
    {"Tint",     EditPropType::Bool, "Metroid.Visual.HudBombLeftIconColorOverlay", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Height",   EditPropType::Int,  "Metroid.Visual.HudBombIconHeight", 4, 64, 1, nullptr, nullptr, nullptr},
    {"Ofs X",    EditPropType::Int,  "Metroid.Visual.HudBombLeftIconOfsX", -128, 128, 1, nullptr, nullptr, nullptr},
    {"Ofs Y",    EditPropType::Int,  "Metroid.Visual.HudBombLeftIconOfsY", -128, 128, 1, nullptr, nullptr, nullptr},
    {"Anchor X", EditPropType::Enum, "Metroid.Visual.HudBombLeftIconAnchorX", 0, 2, 1, kEnumAlign3, nullptr, nullptr},
    {"Anchor Y", EditPropType::Enum, "Metroid.Visual.HudBombLeftIconAnchorY", 0, 2, 1, kEnumAnchorY, nullptr, nullptr},
    {"Opacity",  EditPropType::Float,"Metroid.Visual.HudBombIconOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsWeaponInventory[] = {
    {"Orientation", EditPropType::Enum,  "Metroid.Visual.HudWeaponInventoryOrientation", 0, 1, 1, kEnumOrientation, nullptr, nullptr},
    {"Align",       EditPropType::Enum,  "Metroid.Visual.HudWeaponInventoryAlign",       0, 2, 1, kEnumAlign3,      nullptr, nullptr},
    {"Icon Height", EditPropType::Int,   "Metroid.Visual.HudWeaponInventoryIconHeight",  4, 48, 1, nullptr, nullptr, nullptr},
    {"Spacing",     EditPropType::Int,   "Metroid.Visual.HudWeaponInventorySpacing",     0, 32, 1, nullptr, nullptr, nullptr},
    {"Opacity",     EditPropType::Float, "Metroid.Visual.HudWeaponInventoryOpacity",     0, 100, 5, nullptr, nullptr, nullptr},
    {"Not Owned",   EditPropType::Float, "Metroid.Visual.HudWeaponInventoryNotOwnedOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    {"Highlight",        EditPropType::Bool,  "Metroid.Visual.HudWeaponInventoryHighlightEnable",       0, 1,   1, nullptr, nullptr, nullptr},
    {"Hl Opacity",       EditPropType::Float, "Metroid.Visual.HudWeaponInventoryHighlightOpacity",       0, 100, 5, nullptr, nullptr, nullptr},
    {"Hl Thickness",     EditPropType::Float, "Metroid.Visual.HudWeaponInventoryHighlightThickness",     1,  80, 5, nullptr, nullptr, nullptr},
    {"Hl Padding",       EditPropType::Int,   "Metroid.Visual.HudWeaponInventoryHighlightPadding",       0,  16, 1, nullptr, nullptr, nullptr},
    {"Hl Corner Radius", EditPropType::Int,   "Metroid.Visual.HudWeaponInventoryHighlightCornerRadius",  0,  16, 1, nullptr, nullptr, nullptr},
    {"Hl Ofs Left",      EditPropType::Int,   "Metroid.Visual.HudWeaponInventoryHighlightSizeOffsetLeft",   -16, 32, 1, nullptr, nullptr, nullptr},
    {"Hl Ofs Right",     EditPropType::Int,   "Metroid.Visual.HudWeaponInventoryHighlightSizeOffsetRight",  -16, 32, 1, nullptr, nullptr, nullptr},
    {"Hl Ofs Top",       EditPropType::Int,   "Metroid.Visual.HudWeaponInventoryHighlightSizeOffsetTop",    -16, 32, 1, nullptr, nullptr, nullptr},
    {"Hl Ofs Bottom",    EditPropType::Int,   "Metroid.Visual.HudWeaponInventoryHighlightSizeOffsetBottom", -16, 32, 1, nullptr, nullptr, nullptr},
};

static const HudEditPropDesc kPropsRadar[] = {
    {"Opacity",   EditPropType::Float,"Metroid.Visual.BtmOverlayOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    {"Src Radius",EditPropType::Int,  "Metroid.Visual.BtmOverlaySrcRadius", 10, 96, 1, nullptr, nullptr, nullptr},
};

// ── Crosshair edit-mode props ────────────────────────────────────────────
static const HudEditPropDesc kPropsCrosshairMain[] = {
    {"Scale %",          EditPropType::Int,   "Metroid.Visual.CrosshairScale", 100, 800, 1, nullptr, nullptr, nullptr},
    {"Outline",          EditPropType::Bool,  "Metroid.Visual.CrosshairOutline", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Outline Opacity",  EditPropType::Float, "Metroid.Visual.CrosshairOutlineOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    {"Outline Thick.",   EditPropType::Int,   "Metroid.Visual.CrosshairOutlineThickness", 1, 10, 1, nullptr, nullptr, nullptr},
    {"Center Dot",       EditPropType::Bool,  "Metroid.Visual.CrosshairCenterDot", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Dot Opacity",      EditPropType::Float, "Metroid.Visual.CrosshairDotOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    {"Dot Thick.",       EditPropType::Int,   "Metroid.Visual.CrosshairDotThickness", 1, 10, 1, nullptr, nullptr, nullptr},
    {"T-Style",          EditPropType::Bool,  "Metroid.Visual.CrosshairTStyle", 0, 0, 0, nullptr, nullptr, nullptr},
};
static constexpr int kCrosshairMainCount = HUD_EDIT_PROP_COUNT(kPropsCrosshairMain);

static const HudEditPropDesc kPropsCrosshairInner[] = {
    {"Show",      EditPropType::Bool,  "Metroid.Visual.CrosshairInnerShow", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Opacity",   EditPropType::Float, "Metroid.Visual.CrosshairInnerOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    {"Length X",  EditPropType::Int,   "Metroid.Visual.CrosshairInnerLengthX", 0, 64, 1, nullptr, nullptr, nullptr},
    {"Length Y",  EditPropType::Int,   "Metroid.Visual.CrosshairInnerLengthY", 0, 64, 1, nullptr, nullptr, nullptr},
    {"Link XY",  EditPropType::Bool,  "Metroid.Visual.CrosshairInnerLinkXY", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Thickness", EditPropType::Int,   "Metroid.Visual.CrosshairInnerThickness", 1, 10, 1, nullptr, nullptr, nullptr},
    {"Offset",    EditPropType::Int,   "Metroid.Visual.CrosshairInnerOffset", 0, 64, 1, nullptr, nullptr, nullptr},
};
static constexpr int kCrosshairInnerCount = HUD_EDIT_PROP_COUNT(kPropsCrosshairInner);

static const HudEditPropDesc kPropsCrosshairOuter[] = {
    {"Show",      EditPropType::Bool,  "Metroid.Visual.CrosshairOuterShow", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Opacity",   EditPropType::Float, "Metroid.Visual.CrosshairOuterOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    {"Length X",  EditPropType::Int,   "Metroid.Visual.CrosshairOuterLengthX", 0, 64, 1, nullptr, nullptr, nullptr},
    {"Length Y",  EditPropType::Int,   "Metroid.Visual.CrosshairOuterLengthY", 0, 64, 1, nullptr, nullptr, nullptr},
    {"Link XY",  EditPropType::Bool,  "Metroid.Visual.CrosshairOuterLinkXY", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Thickness", EditPropType::Int,   "Metroid.Visual.CrosshairOuterThickness", 1, 10, 1, nullptr, nullptr, nullptr},
    {"Offset",    EditPropType::Int,   "Metroid.Visual.CrosshairOuterOffset", 0, 64, 1, nullptr, nullptr, nullptr},
};
static constexpr int kCrosshairOuterCount = HUD_EDIT_PROP_COUNT(kPropsCrosshairOuter);

// Crosshair panel state
static bool s_crosshairPanelOpen  = false;
static bool s_innerSectionOpen    = false;
static bool s_outerSectionOpen    = false;
static int  s_crosshairPanelScroll = 0;

// Text-scale slider drag state
static bool s_textScaleDragging = false;

// Auto-scale global cap slider drag state
static bool s_autoScaleCapDragging = false;

static int CountCrosshairRows() {
    return 1 + kCrosshairMainCount + 2; // color + main props + Inner header + Outer header
}

static const HudEditPropDesc* GetCrosshairPropForRow(int rowIdx, bool& isColorRow,
                                                     bool& isInnerHeader, bool& isOuterHeader)
{
    isColorRow = isInnerHeader = isOuterHeader = false;
    if (rowIdx == 0) { isColorRow = true; return nullptr; }
    int idx = rowIdx - 1;
    if (idx < kCrosshairMainCount) return &kPropsCrosshairMain[idx];
    idx -= kCrosshairMainCount;
    if (idx == 0) { isInnerHeader = true; return nullptr; }
    idx -= 1;
    if (idx == 0) { isOuterHeader = true; return nullptr; }
    return nullptr;
}

static const HudEditElemDesc kEditElems[kEditElemCount] = {
    {   // 0: HP text
        "HP",
        "Metroid.Visual.HudHpAnchor",
        "Metroid.Visual.HudHpX",
        "Metroid.Visual.HudHpY",
        nullptr, nullptr, nullptr, nullptr,
        nullptr, // showKey: always visible
        "Metroid.Visual.HudHpTextColorR",
        "Metroid.Visual.HudHpTextColorG",
        "Metroid.Visual.HudHpTextColorB",
        kPropsHp, HUD_EDIT_PROP_COUNT(kPropsHp)
    },
    {   // 1: HP Gauge (independent position)
        "HP Gauge",
        "Metroid.Visual.HudHpGaugePosAnchor",
        "Metroid.Visual.HudHpGaugePosX",
        "Metroid.Visual.HudHpGaugePosY",
        "Metroid.Visual.HudHpGaugeOrientation",
        "Metroid.Visual.HudHpGaugeLength",
        "Metroid.Visual.HudHpGaugeWidth",
        "Metroid.Visual.HudHpGaugePosMode",
        "Metroid.Visual.HudHpGauge", // showKey
        "Metroid.Visual.HudHpGaugeColorR",
        "Metroid.Visual.HudHpGaugeColorG",
        "Metroid.Visual.HudHpGaugeColorB",
        kPropsHpGauge, HUD_EDIT_PROP_COUNT(kPropsHpGauge)
    },
    {   // 2: Weapon / Ammo text
        "Weapon/Ammo",
        "Metroid.Visual.HudWeaponAnchor",
        "Metroid.Visual.HudWeaponX",
        "Metroid.Visual.HudWeaponY",
        nullptr, nullptr, nullptr, nullptr,
        nullptr, // showKey: always visible
        "Metroid.Visual.HudAmmoTextColorR",
        "Metroid.Visual.HudAmmoTextColorG",
        "Metroid.Visual.HudAmmoTextColorB",
        kPropsWeaponAmmo, HUD_EDIT_PROP_COUNT(kPropsWeaponAmmo)
    },
    {   // 3: Weapon icon
        "Wpn\nIcon",
        "Metroid.Visual.HudWeaponIconPosAnchor",
        "Metroid.Visual.HudWeaponIconPosX",
        "Metroid.Visual.HudWeaponIconPosY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudWeaponIconShow", // showKey
        nullptr, nullptr, nullptr, // no color picker
        kPropsWpnIcon, HUD_EDIT_PROP_COUNT(kPropsWpnIcon)
    },
    {   // 4: Ammo gauge (independent position)
        "Ammo Gauge",
        "Metroid.Visual.HudAmmoGaugePosAnchor",
        "Metroid.Visual.HudAmmoGaugePosX",
        "Metroid.Visual.HudAmmoGaugePosY",
        "Metroid.Visual.HudAmmoGaugeOrientation",
        "Metroid.Visual.HudAmmoGaugeLength",
        "Metroid.Visual.HudAmmoGaugeWidth",
        "Metroid.Visual.HudAmmoGaugePosMode",
        "Metroid.Visual.HudAmmoGauge", // showKey
        "Metroid.Visual.HudAmmoGaugeColorR",
        "Metroid.Visual.HudAmmoGaugeColorG",
        "Metroid.Visual.HudAmmoGaugeColorB",
        kPropsAmmoGauge, HUD_EDIT_PROP_COUNT(kPropsAmmoGauge)
    },
    {   // 5: Match Status
        "Match Status",
        "Metroid.Visual.HudMatchStatusAnchor",
        "Metroid.Visual.HudMatchStatusX",
        "Metroid.Visual.HudMatchStatusY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudMatchStatusShow", // showKey
        "Metroid.Visual.HudMatchStatusColorR",
        "Metroid.Visual.HudMatchStatusColorG",
        "Metroid.Visual.HudMatchStatusColorB",
        kPropsMatchStatus, HUD_EDIT_PROP_COUNT(kPropsMatchStatus)
    },
    {   // 6: Rank
        "Rank",
        "Metroid.Visual.HudRankAnchor",
        "Metroid.Visual.HudRankX",
        "Metroid.Visual.HudRankY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudRankShow", // showKey
        "Metroid.Visual.HudRankColorR",
        "Metroid.Visual.HudRankColorG",
        "Metroid.Visual.HudRankColorB",
        kPropsRank, HUD_EDIT_PROP_COUNT(kPropsRank)
    },
    {   // 7: Time Left
        "Time Left",
        "Metroid.Visual.HudTimeLeftAnchor",
        "Metroid.Visual.HudTimeLeftX",
        "Metroid.Visual.HudTimeLeftY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudTimeLeftShow", // showKey
        "Metroid.Visual.HudTimeLeftColorR",
        "Metroid.Visual.HudTimeLeftColorG",
        "Metroid.Visual.HudTimeLeftColorB",
        kPropsTimeLeft, HUD_EDIT_PROP_COUNT(kPropsTimeLeft)
    },
    {   // 8: Time Limit
        "Time Limit",
        "Metroid.Visual.HudTimeLimitAnchor",
        "Metroid.Visual.HudTimeLimitX",
        "Metroid.Visual.HudTimeLimitY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudTimeLimitShow", // showKey
        "Metroid.Visual.HudTimeLimitColorR",
        "Metroid.Visual.HudTimeLimitColorG",
        "Metroid.Visual.HudTimeLimitColorB",
        kPropsTimeLimit, HUD_EDIT_PROP_COUNT(kPropsTimeLimit)
    },
    {   // 9: Bomb Left text
        "Bomb Left",
        "Metroid.Visual.HudBombLeftAnchor",
        "Metroid.Visual.HudBombLeftX",
        "Metroid.Visual.HudBombLeftY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudBombLeftShow", // showKey
        "Metroid.Visual.HudBombLeftColorR",
        "Metroid.Visual.HudBombLeftColorG",
        "Metroid.Visual.HudBombLeftColorB",
        kPropsBombLeft, HUD_EDIT_PROP_COUNT(kPropsBombLeft)
    },
    {   // 10: Bomb icon
        "Bmb\nIcon",
        "Metroid.Visual.HudBombLeftIconPosAnchor",
        "Metroid.Visual.HudBombLeftIconPosX",
        "Metroid.Visual.HudBombLeftIconPosY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudBombLeftIconShow", // showKey
        "Metroid.Visual.HudBombLeftIconColorR",
        "Metroid.Visual.HudBombLeftIconColorG",
        "Metroid.Visual.HudBombLeftIconColorB",
        kPropsBombIcon, HUD_EDIT_PROP_COUNT(kPropsBombIcon)
    },
    {   // 11: Radar overlay
        "Radar",
        "Metroid.Visual.BtmOverlayAnchor",
        "Metroid.Visual.BtmOverlayDstX",
        "Metroid.Visual.BtmOverlayDstY",
        nullptr, // orientKey: not applicable
        "Metroid.Visual.BtmOverlayDstSize", // lengthKey (square resize)
        "Metroid.Visual.BtmOverlayDstSize", // widthKey (same key = coupled)
        nullptr, // posModeKey
        "Metroid.Visual.BtmOverlayEnable", // showKey
        nullptr, nullptr, nullptr, // no color picker
        kPropsRadar, HUD_EDIT_PROP_COUNT(kPropsRadar)
    },
    {   // 12: Weapon Inventory
        "Wpn\nInventory",
        "Metroid.Visual.HudWeaponInventoryAnchor",
        "Metroid.Visual.HudWeaponInventoryX",
        "Metroid.Visual.HudWeaponInventoryY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudWeaponInventoryShow", // showKey
        "Metroid.Visual.HudWeaponInventoryColorR",
        "Metroid.Visual.HudWeaponInventoryColorG",
        "Metroid.Visual.HudWeaponInventoryColorB",
        kPropsWeaponInventory, HUD_EDIT_PROP_COUNT(kPropsWeaponInventory)
    },
    {   // 13: Crosshair (fixed center position — Qt side panel only, no DS-space props)
        "Crosshair",
        nullptr, nullptr, nullptr,            // anchorKey / ofsXKey / ofsYKey: fixed
        nullptr, nullptr, nullptr, nullptr,   // orient / length / width / posMode
        nullptr,                              // showKey: always visible in edit mode
        nullptr, nullptr, nullptr,            // no DS-space color picker
        nullptr, 0                            // no DS-space extra props
    },
};

#undef HUD_EDIT_PROP_COUNT

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

