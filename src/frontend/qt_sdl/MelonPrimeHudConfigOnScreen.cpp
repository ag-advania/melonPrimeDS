// MelonPrimeCustomHudScreenConfig.cpp
// ============================================================
// In-game HUD layout editor — on-screen config mode.
//
// This file is NOT a standalone translation unit.
// It is #include-d near the bottom of MelonPrimeCustomHud.cpp
// (inside namespace MelonPrime, inside #ifdef MELONPRIME_CUSTOM_HUD)
// and therefore shares all statics, types, and draw functions
// defined in that file.  Do NOT add it to CMakeLists.txt.
// ============================================================

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

// ── Enum label helpers ─────────────────────────────────────────────────────
static const char* kEnumAlign3      = "Left|Center|Right";
static const char* kEnumGaugeAnchor = "Below|Above|Right|Left|Center";
static const char* kEnumRelIndep    = "Relative|Independent";
static const char* kEnumAnchorY     = "Top|Center|Bottom";
static const char* kEnumLabelPos    = "Above|Below|Left|Right|Center";
static const char* kEnumLayout      = "Standard|Alternative";

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
    {"Auto Color",   EditPropType::Bool, "Metroid.Visual.HudHpGaugeAutoColor", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Gauge Anchor", EditPropType::Enum, "Metroid.Visual.HudHpGaugeAnchor", 0, 4, 1, kEnumGaugeAnchor, nullptr, nullptr},
    {"Offset X",     EditPropType::Int,  "Metroid.Visual.HudHpGaugeOffsetX", -128, 128, 1, nullptr, nullptr, nullptr},
    {"Offset Y",     EditPropType::Int,  "Metroid.Visual.HudHpGaugeOffsetY", -128, 128, 1, nullptr, nullptr, nullptr},
    {"Opacity",      EditPropType::Float,"Metroid.Visual.HudHpGaugeOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
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
    {"Gauge Anchor", EditPropType::Enum, "Metroid.Visual.HudAmmoGaugeAnchor", 0, 4, 1, kEnumGaugeAnchor, nullptr, nullptr},
    {"Offset X",     EditPropType::Int,  "Metroid.Visual.HudAmmoGaugeOffsetX", -128, 128, 1, nullptr, nullptr, nullptr},
    {"Offset Y",     EditPropType::Int,  "Metroid.Visual.HudAmmoGaugeOffsetY", -128, 128, 1, nullptr, nullptr, nullptr},
    {"Opacity",      EditPropType::Float,"Metroid.Visual.HudAmmoGaugeOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
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
static constexpr int kCrosshairMainCount = 8;

static const HudEditPropDesc kPropsCrosshairInner[] = {
    {"Show",      EditPropType::Bool,  "Metroid.Visual.CrosshairInnerShow", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Opacity",   EditPropType::Float, "Metroid.Visual.CrosshairInnerOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    {"Length X",  EditPropType::Int,   "Metroid.Visual.CrosshairInnerLengthX", 0, 64, 1, nullptr, nullptr, nullptr},
    {"Length Y",  EditPropType::Int,   "Metroid.Visual.CrosshairInnerLengthY", 0, 64, 1, nullptr, nullptr, nullptr},
    {"Link XY",  EditPropType::Bool,  "Metroid.Visual.CrosshairInnerLinkXY", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Thickness", EditPropType::Int,   "Metroid.Visual.CrosshairInnerThickness", 1, 10, 1, nullptr, nullptr, nullptr},
    {"Offset",    EditPropType::Int,   "Metroid.Visual.CrosshairInnerOffset", 0, 64, 1, nullptr, nullptr, nullptr},
};
static constexpr int kCrosshairInnerCount = 7;

static const HudEditPropDesc kPropsCrosshairOuter[] = {
    {"Show",      EditPropType::Bool,  "Metroid.Visual.CrosshairOuterShow", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Opacity",   EditPropType::Float, "Metroid.Visual.CrosshairOuterOpacity", 0, 100, 5, nullptr, nullptr, nullptr},
    {"Length X",  EditPropType::Int,   "Metroid.Visual.CrosshairOuterLengthX", 0, 64, 1, nullptr, nullptr, nullptr},
    {"Length Y",  EditPropType::Int,   "Metroid.Visual.CrosshairOuterLengthY", 0, 64, 1, nullptr, nullptr, nullptr},
    {"Link XY",  EditPropType::Bool,  "Metroid.Visual.CrosshairOuterLinkXY", 0, 0, 0, nullptr, nullptr, nullptr},
    {"Thickness", EditPropType::Int,   "Metroid.Visual.CrosshairOuterThickness", 1, 10, 1, nullptr, nullptr, nullptr},
    {"Offset",    EditPropType::Int,   "Metroid.Visual.CrosshairOuterOffset", 0, 64, 1, nullptr, nullptr, nullptr},
};
static constexpr int kCrosshairOuterCount = 7;

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
        kPropsHp, 4
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
        kPropsHpGauge, 5
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
        kPropsWeaponAmmo, 4
    },
    {   // 3: Weapon icon
        "Wpn\nIcon",
        "Metroid.Visual.HudWeaponIconPosAnchor",
        "Metroid.Visual.HudWeaponIconPosX",
        "Metroid.Visual.HudWeaponIconPosY",
        nullptr, nullptr, nullptr, nullptr,
        "Metroid.Visual.HudWeaponIconShow", // showKey
        nullptr, nullptr, nullptr, // no color picker
        kPropsWpnIcon, 8
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
        kPropsAmmoGauge, 4
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
        kPropsMatchStatus, 13
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
        kPropsRank, 5
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
        kPropsTimeLeft, 2
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
        kPropsTimeLimit, 2
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
        kPropsBombLeft, 5
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
        kPropsBombIcon, 8
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
        kPropsRadar, 2
    },
};

// ── Edit mode static state ──────────────────────────────────────────────────
static EmuInstance*     s_editEmu           = nullptr;
static int              s_editSelected      = -1;
static int              s_editHovered       = -1;
static bool             s_dragging          = false;
static bool             s_anchorPickerOpen  = false;  // expanded 3×3 anchor grid visible
static QPointF      s_dragStartDS;
static int          s_dragStartOfsX   = 0;
static int          s_dragStartOfsY   = 0;
static bool         s_resizingLength  = false;
static bool         s_resizingWidth   = false;
static int          s_resizeStartVal  = 0;
static QPointF      s_resizeStartDS;
static std::map<std::string, int> s_editSnapshot;
static std::set<std::string>      s_editSnapshotBools;
static std::map<std::string, double>      s_editSnapshotDoubles;
static std::map<std::string, std::string> s_editSnapshotStrings;
static std::function<void(int)>           s_editSelectionCb;

static void NotifySelectionChanged(int idx)
{
    if (s_editSelectionCb) s_editSelectionCb(idx);
}

// Context updated by render pass and mouse handlers
static float        s_editOriginX     = 0.0f;
static float        s_editOriginY     = 0.0f;

// Save/Cancel/Reset button rects in DS-space
static const QRectF kEditSaveRect  (10.0f,  1.0f, 74.0f, 12.0f);
static const QRectF kEditCancelRect(88.0f,  1.0f, 74.0f, 12.0f);
static const QRectF kEditResetRect (166.0f, 1.0f, 74.0f, 12.0f);

// Text Scale control, Auto-Scale Cap, Crosshair button, and Preview toggle (below button bar)
static const QRectF kEditTextScaleRect  (10.0f,  15.0f, 58.0f, 10.0f);
static const QRectF kEditAutoScaleCapRect(70.0f, 15.0f, 58.0f, 10.0f);
static const QRectF kEditCrosshairBtnRect(130.0f, 15.0f, 54.0f, 10.0f);

// Text Scale slider helpers (depend on kEditTextScaleRect)
static constexpr int kTsMin = 100, kTsMax = 300;
static constexpr float kTsLabelW = 14.0f;

static inline QRectF TsTrackRect()
{
    const float tsX = static_cast<float>(kEditTextScaleRect.left()) + 2.0f;
    const float tsY = static_cast<float>(kEditTextScaleRect.top());
    const float tsW = static_cast<float>(kEditTextScaleRect.width()) - 4.0f;
    const float tsH = static_cast<float>(kEditTextScaleRect.height());
    return QRectF(tsX + kTsLabelW, tsY + 2.0f, tsW - kTsLabelW, tsH - 4.0f);
}

static inline int TsValueFromX(float px)
{
    const QRectF tr = TsTrackRect();
    float frac = static_cast<float>((px - tr.left()) / tr.width());
    frac = std::max(0.0f, std::min(1.0f, frac));
    return std::max(kTsMin, std::min(kTsMax,
        kTsMin + static_cast<int>(std::round(frac * (kTsMax - kTsMin) / 5.0f) * 5.0f)));
}

// Auto-Scale Cap slider helpers (depend on kEditAutoScaleCapRect)
static constexpr int kAscMin = 100, kAscMax = 800;
static constexpr float kAscLabelW = 14.0f;

static inline QRectF AscTrackRect()
{
    const float x = static_cast<float>(kEditAutoScaleCapRect.left()) + 2.0f;
    const float y = static_cast<float>(kEditAutoScaleCapRect.top());
    const float w = static_cast<float>(kEditAutoScaleCapRect.width()) - 4.0f;
    const float h = static_cast<float>(kEditAutoScaleCapRect.height());
    return QRectF(x + kAscLabelW, y + 2.0f, w - kAscLabelW, h - 4.0f);
}

static inline int AscValueFromX(float px)
{
    const QRectF tr = AscTrackRect();
    float frac = static_cast<float>((px - tr.left()) / tr.width());
    frac = std::max(0.0f, std::min(1.0f, frac));
    return std::max(kAscMin, std::min(kAscMax,
        kAscMin + static_cast<int>(std::round(frac * (kAscMax - kAscMin) / 25.0f) * 25.0f)));
}
static const QRectF kEditPreviewBtnRect(186.0f, 15.0f, 54.0f, 10.0f);

// Crosshair panel rect (left side, below control bar)
static constexpr float kCrosshairPanelX = 2.0f;
static constexpr float kCrosshairPanelY = 28.0f;
// Max rows the crosshair panel can show — set large enough to fit all rows
// without scrolling (11 currently). At 8px/row + 4px padding, 16 rows = 132px
// which fits comfortably below the button bar (192 - 28 = 164px available).
static constexpr int kCrosshairMaxVisible = 16;

// Properties panel layout constants
static constexpr float kPropRowH    = 8.0f;
static constexpr float kPropLabelW  = 52.0f;
static constexpr float kPropCtrlW   = 36.0f;
static constexpr float kPropPanelW  = kPropLabelW + kPropCtrlW + 4.0f;
static constexpr float kAnchorGridCellW = (kPropPanelW - 4.0f) / 3.0f;
static constexpr float kAnchorGridCellH = 9.0f;
static constexpr float kAnchorGridH = kAnchorGridCellH * 3 + 4.0f;
static int s_editPropScroll = 0;
static constexpr int kPropMaxVisible = 8;

// ── Modern dark theme ────────────────────────────────────────────────────────
static const QColor kPanelBg        (10, 12, 24, 225);
static const QColor kPanelShadow    (0, 0, 0, 100);
static const QColor kPanelBorder    (50, 55, 80, 80);
static const QColor kAccent         (70, 140, 255);
static const QColor kAccentDim      (70, 140, 255, 60);
static const QColor kLabelColor     (120, 130, 165);
static const QColor kValueColor     (200, 210, 235);
static const QColor kCtrlBg         (18, 22, 42, 200);
static const QColor kBtnBg          (28, 32, 58, 220);
static const QColor kBtnOnBg        (25, 115, 55, 220);
static const QColor kBtnOffBg       (105, 32, 32, 210);
static const QColor kArrowBg        (28, 32, 58, 220);
static const QColor kElemSelBg      (70, 140, 255, 35);
static const QColor kElemHovBg      (70, 140, 255, 18);
static const QColor kElemNormBg     (50, 80, 130, 12);
static const QColor kElemHiddenSelBg(90, 90, 90, 45);
static const QColor kElemHiddenHovBg(70, 70, 70, 25);
static const QColor kElemHiddenBg   (50, 50, 50, 12);
static constexpr float kCornerRadius = 2.0f;
static constexpr float kBtnCorner    = 3.0f;

// Crosshair side panel (Inner/Outer) — positioned to the right of the main panel
static constexpr float kCrosshairSidePanelX  = kCrosshairPanelX + kPropPanelW + 2.0f;
static constexpr float kCrosshairPreviewX    = kCrosshairSidePanelX + kPropPanelW + 2.0f;
static constexpr int   kCrosshairPreviewSize = 64;

// ── Coordinate conversion ───────────────────────────────────────────────────
static inline QPointF WidgetToDS(const QPointF& pt)
{
    const float dsX = static_cast<float>((pt.x() - s_editOriginX) / s_editHudScale)
                      - (s_editTopStretchX - 1.0f) * 128.0f;
    const float dsY = static_cast<float>((pt.y() - s_editOriginY) / s_editHudScale);
    return QPointF(dsX, dsY);
}

// ── Snapshot / restore ──────────────────────────────────────────────────────
static void SnapshotEditConfig(Config::Table& cfg)
{
    s_editSnapshot.clear();
    s_editSnapshotBools.clear();
    for (int i = 0; i < kEditElemCount; ++i) {
        const HudEditElemDesc& d = kEditElems[i];
        s_editSnapshot[d.anchorKey] = cfg.GetInt(d.anchorKey);
        s_editSnapshot[d.ofsXKey]   = cfg.GetInt(d.ofsXKey);
        s_editSnapshot[d.ofsYKey]   = cfg.GetInt(d.ofsYKey);
        if (d.orientKey)  s_editSnapshot[d.orientKey]  = cfg.GetInt(d.orientKey);
        if (d.lengthKey)  s_editSnapshot[d.lengthKey]  = cfg.GetInt(d.lengthKey);
        if (d.widthKey && d.widthKey != d.lengthKey)
            s_editSnapshot[d.widthKey] = cfg.GetInt(d.widthKey);
        if (d.posModeKey) s_editSnapshot[d.posModeKey] = cfg.GetInt(d.posModeKey);
        if (d.showKey) {
            s_editSnapshot[d.showKey] = cfg.GetBool(d.showKey) ? 1 : 0;
            s_editSnapshotBools.insert(d.showKey);
        }
        if (d.colorRKey) {
            s_editSnapshot[d.colorRKey] = cfg.GetInt(d.colorRKey);
            s_editSnapshot[d.colorGKey] = cfg.GetInt(d.colorGKey);
            s_editSnapshot[d.colorBKey] = cfg.GetInt(d.colorBKey);
        }
    }
    s_editSnapshotDoubles.clear();
    s_editSnapshotStrings.clear();

    auto snapshotPropArray = [&](const HudEditPropDesc* arr, int count) {
        for (int p = 0; p < count; ++p) {
            const HudEditPropDesc& pr = arr[p];
            switch (pr.type) {
            case EditPropType::Bool:
                s_editSnapshot[pr.cfgKey] = cfg.GetBool(pr.cfgKey) ? 1 : 0;
                s_editSnapshotBools.insert(pr.cfgKey);
                break;
            case EditPropType::Enum:
            case EditPropType::Int:
                s_editSnapshot[pr.cfgKey] = cfg.GetInt(pr.cfgKey);
                break;
            case EditPropType::Float:
                s_editSnapshotDoubles[pr.cfgKey] = cfg.GetDouble(pr.cfgKey);
                break;
            case EditPropType::String:
                s_editSnapshotStrings[pr.cfgKey] = cfg.GetString(pr.cfgKey);
                break;
            case EditPropType::SubColor:
                s_editSnapshot[pr.cfgKey] = cfg.GetBool(pr.cfgKey) ? 1 : 0;
                s_editSnapshotBools.insert(pr.cfgKey);
                s_editSnapshot[pr.extra1] = cfg.GetInt(pr.extra1);
                s_editSnapshot[pr.extra2] = cfg.GetInt(pr.extra2);
                s_editSnapshot[pr.extra3] = cfg.GetInt(pr.extra3);
                break;
            default: break;
            }
        }
    };

    for (int i = 0; i < kEditElemCount; ++i)
        snapshotPropArray(kEditElems[i].props, kEditElems[i].propCount);

    s_editSnapshot["Metroid.Visual.CrosshairColorR"] = cfg.GetInt("Metroid.Visual.CrosshairColorR");
    s_editSnapshot["Metroid.Visual.CrosshairColorG"] = cfg.GetInt("Metroid.Visual.CrosshairColorG");
    s_editSnapshot["Metroid.Visual.CrosshairColorB"] = cfg.GetInt("Metroid.Visual.CrosshairColorB");
    snapshotPropArray(kPropsCrosshairMain,  kCrosshairMainCount);
    snapshotPropArray(kPropsCrosshairInner, kCrosshairInnerCount);
    snapshotPropArray(kPropsCrosshairOuter, kCrosshairOuterCount);
    s_editSnapshot["Metroid.Visual.HudTextScale"] = cfg.GetInt("Metroid.Visual.HudTextScale");
    s_editSnapshot["Metroid.Visual.HudAutoScaleEnable"] = cfg.GetBool("Metroid.Visual.HudAutoScaleEnable") ? 1 : 0;
    s_editSnapshotBools.insert("Metroid.Visual.HudAutoScaleEnable");
    s_editSnapshot["Metroid.Visual.HudAutoScaleCap"]       = cfg.GetInt("Metroid.Visual.HudAutoScaleCap");
    s_editSnapshot["Metroid.Visual.HudAutoScaleCapText"]   = cfg.GetInt("Metroid.Visual.HudAutoScaleCapText");
    s_editSnapshot["Metroid.Visual.HudAutoScaleCapIcons"]  = cfg.GetInt("Metroid.Visual.HudAutoScaleCapIcons");
    s_editSnapshot["Metroid.Visual.HudAutoScaleCapGauges"]    = cfg.GetInt("Metroid.Visual.HudAutoScaleCapGauges");
    s_editSnapshot["Metroid.Visual.HudAutoScaleCapCrosshair"] = cfg.GetInt("Metroid.Visual.HudAutoScaleCapCrosshair");
}

static void RestoreEditSnapshot(Config::Table& cfg)
{
    for (const auto& kv : s_editSnapshot) {
        if (s_editSnapshotBools.count(kv.first))
            cfg.SetBool(kv.first, kv.second != 0);
        else
            cfg.SetInt(kv.first, kv.second);
    }
    for (const auto& kv : s_editSnapshotDoubles)
        cfg.SetDouble(kv.first, kv.second);
    for (const auto& kv : s_editSnapshotStrings)
        cfg.SetString(kv.first, kv.second);
}

static void ResetEditToDefaults(Config::Table& cfg)
{
    toml::value defData(toml::table{});
    Config::Table defaults(defData, "Instance0");
    for (int i = 0; i < kEditElemCount; ++i) {
        const HudEditElemDesc& d = kEditElems[i];
        cfg.SetInt(d.anchorKey, defaults.GetInt(d.anchorKey));
        cfg.SetInt(d.ofsXKey,   defaults.GetInt(d.ofsXKey));
        cfg.SetInt(d.ofsYKey,   defaults.GetInt(d.ofsYKey));
        if (d.orientKey)  cfg.SetInt(d.orientKey,  defaults.GetInt(d.orientKey));
        if (d.lengthKey)  cfg.SetInt(d.lengthKey,  defaults.GetInt(d.lengthKey));
        if (d.widthKey && d.widthKey != d.lengthKey)
            cfg.SetInt(d.widthKey, defaults.GetInt(d.widthKey));
        if (d.posModeKey) cfg.SetInt(d.posModeKey, defaults.GetInt(d.posModeKey));
        if (d.showKey)    cfg.SetBool(d.showKey,   defaults.GetBool(d.showKey));
        if (d.colorRKey) {
            cfg.SetInt(d.colorRKey, defaults.GetInt(d.colorRKey));
            cfg.SetInt(d.colorGKey, defaults.GetInt(d.colorGKey));
            cfg.SetInt(d.colorBKey, defaults.GetInt(d.colorBKey));
        }
        for (int p = 0; p < d.propCount; ++p) {
            const HudEditPropDesc& pr = d.props[p];
            switch (pr.type) {
            case EditPropType::Bool:   cfg.SetBool(pr.cfgKey, defaults.GetBool(pr.cfgKey)); break;
            case EditPropType::Enum:
            case EditPropType::Int:    cfg.SetInt(pr.cfgKey,  defaults.GetInt(pr.cfgKey));  break;
            case EditPropType::Float:  cfg.SetDouble(pr.cfgKey, defaults.GetDouble(pr.cfgKey)); break;
            case EditPropType::String: cfg.SetString(pr.cfgKey, defaults.GetString(pr.cfgKey)); break;
            case EditPropType::SubColor:
                cfg.SetBool(pr.cfgKey, defaults.GetBool(pr.cfgKey));
                cfg.SetInt(pr.extra1, defaults.GetInt(pr.extra1));
                cfg.SetInt(pr.extra2, defaults.GetInt(pr.extra2));
                cfg.SetInt(pr.extra3, defaults.GetInt(pr.extra3));
                break;
            }
        }
    }

    auto resetPropArray = [&](const HudEditPropDesc* arr, int count) {
        for (int p = 0; p < count; ++p) {
            const HudEditPropDesc& pr = arr[p];
            switch (pr.type) {
            case EditPropType::Bool:   cfg.SetBool(pr.cfgKey, defaults.GetBool(pr.cfgKey));     break;
            case EditPropType::Enum:
            case EditPropType::Int:    cfg.SetInt(pr.cfgKey,  defaults.GetInt(pr.cfgKey));       break;
            case EditPropType::Float:  cfg.SetDouble(pr.cfgKey, defaults.GetDouble(pr.cfgKey)); break;
            default: break;
            }
        }
    };
    cfg.SetInt("Metroid.Visual.CrosshairColorR", defaults.GetInt("Metroid.Visual.CrosshairColorR"));
    cfg.SetInt("Metroid.Visual.CrosshairColorG", defaults.GetInt("Metroid.Visual.CrosshairColorG"));
    cfg.SetInt("Metroid.Visual.CrosshairColorB", defaults.GetInt("Metroid.Visual.CrosshairColorB"));
    resetPropArray(kPropsCrosshairMain,  kCrosshairMainCount);
    resetPropArray(kPropsCrosshairInner, kCrosshairInnerCount);
    resetPropArray(kPropsCrosshairOuter, kCrosshairOuterCount);
    cfg.SetInt("Metroid.Visual.HudTextScale", defaults.GetInt("Metroid.Visual.HudTextScale"));
    cfg.SetBool("Metroid.Visual.HudAutoScaleEnable", defaults.GetBool("Metroid.Visual.HudAutoScaleEnable"));
    cfg.SetInt("Metroid.Visual.HudAutoScaleCap",       defaults.GetInt("Metroid.Visual.HudAutoScaleCap"));
    cfg.SetInt("Metroid.Visual.HudAutoScaleCapText",   defaults.GetInt("Metroid.Visual.HudAutoScaleCapText"));
    cfg.SetInt("Metroid.Visual.HudAutoScaleCapIcons",  defaults.GetInt("Metroid.Visual.HudAutoScaleCapIcons"));
    cfg.SetInt("Metroid.Visual.HudAutoScaleCapGauges",    defaults.GetInt("Metroid.Visual.HudAutoScaleCapGauges"));
    cfg.SetInt("Metroid.Visual.HudAutoScaleCapCrosshair", defaults.GetInt("Metroid.Visual.HudAutoScaleCapCrosshair"));
}

// ── Bounding rect computation ───────────────────────────────────────────────
static QRectF ComputeEditBounds(int idx, Config::Table& cfg, float topStretchX)
{
    const HudEditElemDesc& d = kEditElems[idx];
    const int anchor = cfg.GetInt(d.anchorKey);
    const int ofsX   = cfg.GetInt(d.ofsXKey);
    const int ofsY   = cfg.GetInt(d.ofsYKey);
    int fx, fy;
    ApplyAnchor(anchor, ofsX, ofsY, fx, fy, topStretchX);

    const float hs  = (s_editHudScale > 0.0f) ? s_editHudScale : 1.0f;
    float tds = std::max(1.0f, cfg.GetInt("Metroid.Visual.HudTextScale") / 100.0f) / hs;

    // ── HP Gauge (idx=1) / Ammo Gauge (idx=4) ──────────────────────────────
    if (d.lengthKey != nullptr) {
        const float len = std::max(4.0f, cfg.GetInt(d.lengthKey) / hs);
        const float wid = std::max(1.0f, cfg.GetInt(d.widthKey) / hs);
        const int ori     = (d.orientKey != nullptr) ? cfg.GetInt(d.orientKey) : 0;
        const int posMode = d.posModeKey ? cfg.GetInt(d.posModeKey) : 1;

        if (posMode == 0) {
            // Relative to parent text element
            const int parentIdx = (idx == 1) ? 0 : 2; // HP→0, AmmoGauge→Weapon(2)
            const HudEditElemDesc& td = kEditElems[parentIdx];
            int tx, ty;
            ApplyAnchor(cfg.GetInt(td.anchorKey), cfg.GetInt(td.ofsXKey), cfg.GetInt(td.ofsYKey),
                        tx, ty, topStretchX);
            const int approxW = static_cast<int>(30.0f * tds);
            const int approxH = static_cast<int>(12.0f * tds);
            const char* gaugeAnchorKey = (idx == 1) ? "Metroid.Visual.HudHpGaugeAnchor"
                                                     : "Metroid.Visual.HudAmmoGaugeAnchor";
            const char* gaugeOfsXKey   = (idx == 1) ? "Metroid.Visual.HudHpGaugeOffsetX"
                                                     : "Metroid.Visual.HudAmmoGaugeOffsetX";
            const char* gaugeOfsYKey   = (idx == 1) ? "Metroid.Visual.HudHpGaugeOffsetY"
                                                     : "Metroid.Visual.HudAmmoGaugeOffsetY";
            int gx, gy;
            CalcGaugePos(tx, ty, approxW, approxH,
                         cfg.GetInt(gaugeAnchorKey), cfg.GetInt(gaugeOfsXKey), cfg.GetInt(gaugeOfsYKey),
                         len, wid, ori, gx, gy);
            return (ori == 1) ? QRectF(gx, gy, wid, len) : QRectF(gx, gy, len, wid);
        }
        // Independent: fx/fy come from PosAnchor + PosX/Y
        return (ori == 1) ? QRectF(fx, fy, wid, len) : QRectF(fx, fy, len, wid);
    }

    // ── Weapon icon (idx=3) ─────────────────────────────────────────────────
    if (idx == 3) {
        const int iconH = std::max(4, cfg.GetInt("Metroid.Visual.HudWeaponIconHeight"));
        const float dh  = static_cast<float>(iconH) / hs;
        // Use cached icon aspect ratio (don't reload); fall back to square
        const QImage& icon = s_weaponIcons[0];
        const float dw = (!icon.isNull() && icon.height() > 0)
                         ? dh * static_cast<float>(icon.width()) / static_cast<float>(icon.height())
                         : dh;
        float ix, iy;
        if (cfg.GetInt("Metroid.Visual.HudWeaponIconMode") == 0) {
            // Relative to Weapon/Ammo text (idx=2)
            const HudEditElemDesc& td = kEditElems[2];
            int tx, ty;
            ApplyAnchor(cfg.GetInt(td.anchorKey), cfg.GetInt(td.ofsXKey), cfg.GetInt(td.ofsYKey),
                        tx, ty, topStretchX);
            ix = static_cast<float>(tx) + cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetX");
            iy = static_cast<float>(ty) + cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetY");
        } else {
            ix = static_cast<float>(fx);
            iy = static_cast<float>(fy);
        }
        const int ancX = cfg.GetInt("Metroid.Visual.HudWeaponIconAnchorX");
        const int ancY = cfg.GetInt("Metroid.Visual.HudWeaponIconAnchorY");
        if (ancX == 1) ix -= dw * 0.5f; else if (ancX == 2) ix -= dw;
        if (ancY == 1) iy -= dh * 0.5f; else if (ancY == 2) iy -= dh;
        return QRectF(ix, iy, dw, dh);
    }

    // ── Bomb icon (idx=10) ──────────────────────────────────────────────────
    if (idx == 10) {
        const int iconH = std::max(4, cfg.GetInt("Metroid.Visual.HudBombIconHeight"));
        const float dh  = static_cast<float>(iconH) / hs;
        const QImage& icon = s_bombIcons[3];
        const float dw = (!icon.isNull() && icon.height() > 0)
                         ? dh * static_cast<float>(icon.width()) / static_cast<float>(icon.height())
                         : dh;
        float ix, iy;
        if (cfg.GetInt("Metroid.Visual.HudBombLeftIconMode") == 0) {
            // Relative to BombLeft text (idx=9)
            const HudEditElemDesc& td = kEditElems[9];
            int tx, ty;
            ApplyAnchor(cfg.GetInt(td.anchorKey), cfg.GetInt(td.ofsXKey), cfg.GetInt(td.ofsYKey),
                        tx, ty, topStretchX);
            ix = static_cast<float>(tx) + cfg.GetInt("Metroid.Visual.HudBombLeftIconOfsX");
            iy = static_cast<float>(ty) + cfg.GetInt("Metroid.Visual.HudBombLeftIconOfsY");
        } else {
            ix = static_cast<float>(fx);
            iy = static_cast<float>(fy);
        }
        const int ancX = cfg.GetInt("Metroid.Visual.HudBombLeftIconAnchorX");
        const int ancY = cfg.GetInt("Metroid.Visual.HudBombLeftIconAnchorY");
        if (ancX == 1) ix -= dw * 0.5f; else if (ancX == 2) ix -= dw;
        if (ancY == 1) iy -= dh * 0.5f; else if (ancY == 2) iy -= dh;
        return QRectF(ix, iy, dw, dh);
    }

    // ── Radar (idx=11) ──────────────────────────────────────────────────────
    if (idx == 11) {
        const float sz = static_cast<float>(cfg.GetInt("Metroid.Visual.BtmOverlayDstSize"));
        return QRectF(fx, fy, sz, sz);
    }

    // ── Text elements ───────────────────────────────────────────────────────
    // tds is in DS-space (textScalePct/100/hudScale). Enforce min clickable size.
    const float bw = std::max(14.0f, 60.0f * tds);
    const float bh = std::max(5.0f, 12.0f * tds);
    return QRectF(static_cast<float>(fx) - bw * 0.5f, static_cast<float>(fy) - bh * 0.5f, bw, bh);
}

static int CountBuiltinRows(const HudEditElemDesc& d) {
    int n = 1; // anchor always present
    if (d.showKey)   ++n;
    if (d.colorRKey) ++n;
    return n;
}

static QRectF ComputePropsPanelRect(const QRectF& elemRect, int totalRowCount)
{
    int visCount = std::min(totalRowCount, kPropMaxVisible);
    float h = visCount * kPropRowH + 4.0f + (s_anchorPickerOpen ? kAnchorGridH : 0.0f);
    float px = static_cast<float>(elemRect.right()) + 4.0f;
    float py = static_cast<float>(elemRect.top());
    if (px + kPropPanelW > 256.0f) px = static_cast<float>(elemRect.left()) - kPropPanelW - 2.0f;
    if (py + h > 192.0f) py = 192.0f - h;
    if (px < 0.0f) px = 0.0f;
    if (py < 26.0f) py = 26.0f;
    return QRectF(px, py, kPropPanelW, h);
}

static QRectF GetOrientToggleRect(int idx)
{
    const QRectF& r = s_editRects[idx];
    QRectF rect(r.right() - 8.0f, r.top() - 11.0f, 10.0f, 10.0f);
    if (rect.top() < 0.0f) rect.moveTop(r.top());
    return rect;
}

static void GetResizeHandles(int idx, Config::Table& cfg,
                              QRectF& lenHandle, QRectF& widHandle)
{
    const QRectF& r = s_editRects[idx];
    const int ori = kEditElems[idx].orientKey ? cfg.GetInt(kEditElems[idx].orientKey) : 0;
    const bool squareResize = kEditElems[idx].lengthKey && kEditElems[idx].widthKey
                              && strcmp(kEditElems[idx].lengthKey, kEditElems[idx].widthKey) == 0;
    if (ori == 1) {
        lenHandle = QRectF(r.center().x() - 3.0f, r.bottom() - 3.0f, 6.0f, 6.0f);
        widHandle = squareResize ? QRectF() : QRectF(r.right() - 3.0f, r.center().y() - 3.0f, 6.0f, 6.0f);
    } else {
        lenHandle = QRectF(r.right() - 3.0f, r.center().y() - 3.0f, 6.0f, 6.0f);
        widHandle = squareResize ? QRectF() : QRectF(r.center().x() - 3.0f, r.bottom() - 3.0f, 6.0f, 6.0f);
    }
}

// ── DrawEditHudPreview ──────────────────────────────────────────────────────
static void DrawEditHudPreview(QPainter* p, Config::Table& cfg, float tds, float hudScale, float topStretchX)
{
    if (!s_editEmu || s_editRomCopy.playerHP == 0) return;
    melonDS::NDS* nds = s_editEmu->getNDS();
    if (!nds) return;
    melonDS::u8* ram = nds->MainRAM;
    if (!ram) return;

    const CachedHudConfig& c = s_cache;
    const RomAddresses&     rom     = s_editRomCopy;
    const GameAddressesHot& addrHot = s_editAddrHotCopy;
    const uint32_t offP = static_cast<uint32_t>(s_editPlayerPosCopy) * Consts::PLAYER_ADDR_INC;

    const uint16_t currentHP = Read16(ram, rom.playerHP + offP);
    const uint16_t maxHP     = Read16(ram, rom.maxHP     + offP);
    DrawHP(p, currentHP, maxHP, c, tds);

    const uint8_t hunterID = Read8(ram, addrHot.chosenHunter);
    const bool    isAlt    = Read8(ram, addrHot.isAltForm) == 0x02;
    const bool    isBomber = (hunterID == static_cast<uint8_t>(HunterId::Samus) ||
                              hunterID == static_cast<uint8_t>(HunterId::Sylux));
    if (isBomber && isAlt)
        DrawBombLeft(p, ram, rom, offP, c, tds, hudScale);

    const bool isAdventure = Read8(ram, rom.isInAdventure) == 0x02;
    DrawMatchStatusHud(p, ram, rom, s_editPlayerPosCopy, isAdventure, c);
    DrawRankAndTime(p, ram, rom, s_editPlayerPosCopy, isAdventure, c, tds);

    const uint8_t viewMode    = Read8(ram, rom.baseViewMode + offP);
    const bool    isFirstPerson = (viewMode == 0x00);
    if (isFirstPerson) {
        const uint8_t  weapon          = Read8(ram, addrHot.currentWeapon);
        const uint32_t addrAmmoSpecial = rom.currentAmmoSpecial + offP;
        const uint32_t addrAmmoMissile = rom.currentAmmoMissile + offP;
        const uint16_t maxAmmoSpecial  = Read16(ram, rom.maxAmmoSpecial + offP);
        const uint16_t maxAmmoMissile  = Read16(ram, rom.maxAmmoMissile + offP);
        DrawWeaponAmmo(p, ram, weapon,
                       Read16(ram, addrAmmoSpecial), addrAmmoMissile,
                       maxAmmoSpecial, maxAmmoMissile, c, tds, hudScale);

        const bool isTrans = (Read8(ram, addrHot.jumpFlag) & 0x10) != 0;
        if (!isTrans && !isAlt)
            DrawCrosshair(p, ram, rom, c, hudScale, topStretchX);
    }
}

// ── Guard to prevent re-entrant QColorDialog ────────────────────────────────
static bool s_colorDialogOpen = false;

// ── DrawElemPropsPanel ──────────────────────────────────────────────────────
// Draws the unified properties panel for the currently selected element.
// Called from DrawEditOverlay in both normal mode and preview mode.
static void DrawElemPropsPanel(QPainter* p, Config::Table& cfg,
                               const QFont& smallFont, const QFont& normalFont)
{
    if (s_editSelected < 0 || s_editSelected >= kEditElemCount) return;
    const HudEditElemDesc& d = kEditElems[s_editSelected];
    const QRectF& selRect = s_editRects[s_editSelected];

    const int builtinRows = CountBuiltinRows(d);
    const int totalRows = builtinRows + d.propCount;
    if (totalRows <= 0) return;

    QRectF panelRect = ComputePropsPanelRect(selRect, totalRows);
    // Shadow
    p->setPen(Qt::NoPen); p->setBrush(kPanelShadow);
    p->drawRoundedRect(panelRect.translated(1.0, 1.0), kBtnCorner, kBtnCorner);
    // Panel bg
    p->setBrush(kPanelBg);
    p->setPen(QPen(kPanelBorder, 0.3));
    p->drawRoundedRect(panelRect, kBtnCorner, kBtnCorner);
    // Accent line at top
    p->setPen(QPen(kAccent, 0.6));
    p->drawLine(QPointF(panelRect.left() + 3, panelRect.top()),
                QPointF(panelRect.right() - 3, panelRect.top()));
    p->setBrush(Qt::NoBrush);

    int visCount = std::min(totalRows, kPropMaxVisible);
    int scrollOfs = std::min(s_editPropScroll, std::max(0, totalRows - kPropMaxVisible));
    for (int vi = 0; vi < visCount; ++vi) {
        int rowIdx = vi + scrollOfs;
        if (rowIdx >= totalRows) break;
        float rowY = static_cast<float>(panelRect.top()) + 2.0f + vi * kPropRowH;
        float rowX = static_cast<float>(panelRect.left()) + 2.0f;
        float ctrlX = rowX + kPropLabelW;
        float ctrlW = kPropCtrlW;

        int builtinIdx = rowIdx;
        bool isShowRow = false, isColorRow = false, isAnchorRow = false;
        if (d.showKey && builtinIdx == 0) {
            isShowRow = true;
        } else {
            if (d.showKey) --builtinIdx;
            if (d.colorRKey && builtinIdx == 0) {
                isColorRow = true;
            } else {
                if (d.colorRKey) --builtinIdx;
                if (builtinIdx == 0 && rowIdx < builtinRows) {
                    isAnchorRow = true;
                }
            }
        }

        if (isShowRow) {
            p->setFont(smallFont);
            p->setPen(kLabelColor);
            p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Show"));
            const bool visible = cfg.GetBool(d.showKey);
            QRectF btnR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
            p->setBrush(visible ? kBtnOnBg : kBtnOffBg);
            p->setPen(Qt::NoPen);
            p->drawRoundedRect(btnR, 1.5, 1.5);
            p->setBrush(Qt::NoBrush);
            p->setPen(kValueColor);
            p->drawText(btnR, Qt::AlignCenter, visible ? QStringLiteral("ON") : QStringLiteral("OFF"));
        } else if (isColorRow) {
            p->setFont(smallFont);
            p->setPen(kLabelColor);
            p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Color"));
            QColor curColor(cfg.GetInt(d.colorRKey), cfg.GetInt(d.colorGKey), cfg.GetInt(d.colorBKey));
            QRectF swR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
            p->setBrush(curColor); p->setPen(Qt::NoPen);
            p->drawRoundedRect(swR, 1.5, 1.5);
            p->setBrush(Qt::NoBrush);
        } else if (isAnchorRow) {
            static const char* const kAnchorArrow[9] = {
                "\xe2\x86\x96", "\xe2\x86\x91", "\xe2\x86\x97",
                "\xe2\x86\x90", "\xc2\xb7",      "\xe2\x86\x92",
                "\xe2\x86\x99", "\xe2\x86\x93", "\xe2\x86\x98",
            };
            int anchor = cfg.GetInt(d.anchorKey);
            float halfW = ctrlW / 2.0f;
            QRectF valR(ctrlX, rowY + 1.0f, halfW - 1.0f, kPropRowH - 2.0f);
            QRectF btnR(ctrlX + halfW, rowY + 1.0f, halfW, kPropRowH - 2.0f);
            p->setFont(smallFont);
            p->setPen(kLabelColor);
            p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                        Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Anchor"));
            p->setBrush(kCtrlBg); p->setPen(Qt::NoPen);
            p->drawRoundedRect(valR, 1.5, 1.5); p->setBrush(Qt::NoBrush);
            p->setBrush(s_anchorPickerOpen ? QColor(45, 100, 50, 230) : kBtnBg);
            p->setPen(Qt::NoPen);
            p->drawRoundedRect(btnR, 1.5, 1.5);
            p->setBrush(Qt::NoBrush);
            p->setPen(kValueColor);
            p->drawText(valR, Qt::AlignCenter,
                QString::fromUtf8(anchor >= 0 && anchor < 9 ? kAnchorArrow[anchor] : "?"));
            p->drawText(btnR, Qt::AlignCenter, QStringLiteral("Chng"));
        } else {
            int propIdx = rowIdx - builtinRows;
            if (propIdx < 0 || propIdx >= d.propCount) continue;
            const HudEditPropDesc& pr = d.props[propIdx];

            p->setFont(smallFont);
            p->setPen(kLabelColor);
            p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                         Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(pr.label));

            switch (pr.type) {
            case EditPropType::Bool: {
                bool val = cfg.GetBool(pr.cfgKey);
                QRectF btnR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                p->setBrush(val ? kBtnOnBg : kBtnOffBg);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(btnR, 1.5, 1.5);
                p->setBrush(Qt::NoBrush);
                p->setPen(kValueColor);
                p->drawText(btnR, Qt::AlignCenter, val ? QStringLiteral("ON") : QStringLiteral("OFF"));
                break;
            }
            case EditPropType::Int: {
                int val = cfg.GetInt(pr.cfgKey);
                float arrowW = 8.0f;
                QRectF leftArr(ctrlX, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                QRectF rightArr(ctrlX + ctrlW - arrowW, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                QRectF valR(ctrlX + arrowW, rowY + 1.0f, ctrlW - 2.0f * arrowW, kPropRowH - 2.0f);
                p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                p->drawRoundedRect(leftArr, 1.5, 1.5);
                p->drawRoundedRect(rightArr, 1.5, 1.5);
                p->setBrush(kCtrlBg);
                p->drawRoundedRect(valR, 0, 0);
                p->setBrush(Qt::NoBrush);
                p->setPen(kLabelColor);
                p->drawText(leftArr, Qt::AlignCenter, QStringLiteral("\u25C0"));
                p->drawText(rightArr, Qt::AlignCenter, QStringLiteral("\u25B6"));
                p->setPen(kValueColor);
                p->drawText(valR, Qt::AlignCenter, QString::number(val));
                break;
            }
            case EditPropType::Enum: {
                int val = cfg.GetInt(pr.cfgKey);
                float arrowW = 8.0f;
                QRectF leftArr(ctrlX, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                QRectF rightArr(ctrlX + ctrlW - arrowW, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                QRectF valR(ctrlX + arrowW, rowY + 1.0f, ctrlW - 2.0f * arrowW, kPropRowH - 2.0f);
                p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                p->drawRoundedRect(leftArr, 1.5, 1.5);
                p->drawRoundedRect(rightArr, 1.5, 1.5);
                p->setBrush(kCtrlBg);
                p->drawRoundedRect(valR, 0, 0);
                p->setBrush(Qt::NoBrush);
                p->setPen(kLabelColor);
                p->drawText(leftArr, Qt::AlignCenter, QStringLiteral("\u25C0"));
                p->drawText(rightArr, Qt::AlignCenter, QStringLiteral("\u25B6"));
                p->setPen(kValueColor);
                p->drawText(valR, Qt::AlignCenter, EditEnumLabel(pr.extra1, val));
                break;
            }
            case EditPropType::Float: {
                double val = cfg.GetDouble(pr.cfgKey);
                float arrowW = 8.0f;
                QRectF leftArr(ctrlX, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                QRectF rightArr(ctrlX + ctrlW - arrowW, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                QRectF valR(ctrlX + arrowW, rowY + 1.0f, ctrlW - 2.0f * arrowW, kPropRowH - 2.0f);
                p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                p->drawRoundedRect(leftArr, 1.5, 1.5);
                p->drawRoundedRect(rightArr, 1.5, 1.5);
                p->setBrush(kCtrlBg);
                p->drawRoundedRect(valR, 0, 0);
                p->setBrush(Qt::NoBrush);
                p->setPen(kLabelColor);
                p->drawText(leftArr, Qt::AlignCenter, QStringLiteral("\u25C0"));
                p->drawText(rightArr, Qt::AlignCenter, QStringLiteral("\u25B6"));
                p->setPen(kValueColor);
                p->drawText(valR, Qt::AlignCenter, QString::number(val, 'f', 2));
                break;
            }
            case EditPropType::String: {
                std::string val = cfg.GetString(pr.cfgKey);
                QRectF btnR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                p->setBrush(kCtrlBg); p->setPen(Qt::NoPen);
                p->drawRoundedRect(btnR, 1.5, 1.5); p->setBrush(Qt::NoBrush);
                p->setPen(kValueColor);
                QString display = QString::fromStdString(val);
                if (display.length() > 6) display = display.left(5) + QStringLiteral("\u2026");
                p->drawText(btnR, Qt::AlignCenter, display.isEmpty() ? QStringLiteral("...") : display);
                break;
            }
            case EditPropType::SubColor: {
                bool overall = cfg.GetBool(pr.cfgKey);
                float ovrW = 14.0f;
                QRectF ovrR(ctrlX, rowY + 1.0f, ovrW, kPropRowH - 2.0f);
                p->setBrush(overall ? QColor(70, 70, 35, 200) : kCtrlBg);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(ovrR, 1.5, 1.5); p->setBrush(Qt::NoBrush);
                p->setPen(overall ? QColor(220, 220, 130) : QColor(100, 100, 140));
                p->drawText(ovrR, Qt::AlignCenter, QStringLiteral("OVR"));
                QColor clr(cfg.GetInt(pr.extra1), cfg.GetInt(pr.extra2), cfg.GetInt(pr.extra3));
                QRectF swR(ctrlX + ovrW + 2.0f, rowY + 1.0f, ctrlW - ovrW - 2.0f, kPropRowH - 2.0f);
                p->setBrush(overall ? QColor(80, 80, 80) : clr);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(swR, 1.5, 1.5); p->setBrush(Qt::NoBrush);
                break;
            }
            case EditPropType::Color: break;
            }
        }
    }
    // Scroll indicator
    if (totalRows > kPropMaxVisible) {
        int scrollOfsI = std::min(s_editPropScroll, std::max(0, totalRows - kPropMaxVisible));
        float rowAreaH = visCount * kPropRowH;
        float indH = rowAreaH * kPropMaxVisible / totalRows;
        float indY = static_cast<float>(panelRect.top()) + 2.0f +
            (rowAreaH - indH) * scrollOfsI / (totalRows - kPropMaxVisible);
        QRectF indR(panelRect.right() - 2.0f, indY, 1.5f, indH);
        p->setPen(Qt::NoPen); p->setBrush(kPanelBorder);
        p->drawRoundedRect(indR, 0.75f, 0.75f);
        p->setBrush(Qt::NoBrush);
    }
    // Anchor picker 3×3 grid
    if (s_anchorPickerOpen) {
        static const char* const kAnchorArrowGrid[9] = {
            "\xe2\x86\x96", "\xe2\x86\x91", "\xe2\x86\x97",
            "\xe2\x86\x90", "\xc2\xb7",      "\xe2\x86\x92",
            "\xe2\x86\x99", "\xe2\x86\x93", "\xe2\x86\x98",
        };
        int curAnchor = cfg.GetInt(d.anchorKey);
        float gridLeft = static_cast<float>(panelRect.left()) + 2.0f;
        float gridTop  = static_cast<float>(panelRect.top()) + 2.0f + visCount * kPropRowH + 2.0f;
        float cellW = (kPropPanelW - 4.0f) / 3.0f;
        float cellH = kAnchorGridCellH;
        p->setFont(normalFont);
        for (int a = 0; a < 9; ++a) {
            int col = a % 3, row = a / 3;
            QRectF cell(gridLeft + col * cellW, gridTop + row * cellH, cellW - 1.0f, cellH - 1.0f);
            bool sel = (a == curAnchor);
            p->setPen(Qt::NoPen);
            p->setBrush(sel ? kBtnOnBg : kCtrlBg);
            p->drawRoundedRect(cell, kCornerRadius, kCornerRadius);
            p->setBrush(Qt::NoBrush);
            p->setPen(sel ? QPen(QColor(140, 255, 160), 0.5) : QPen(kPanelBorder, 0.3));
            p->drawRoundedRect(cell, kCornerRadius, kCornerRadius);
            p->setPen(sel ? QColor(200, 255, 210) : kLabelColor);
            p->drawText(cell, Qt::AlignCenter, QString::fromUtf8(kAnchorArrowGrid[a]));
        }
    }
    p->setFont(normalFont);
}

// ── DrawEditOverlay ─────────────────────────────────────────────────────────
static void DrawEditOverlay(QPainter* p, Config::Table& cfg, float topStretchX)
{
    if (!p) return;

    const float leftX = -(topStretchX - 1.0f) * 128.0f;
    const float tdsRaw = std::max(1.0f, cfg.GetInt("Metroid.Visual.HudTextScale") / 100.0f);
    const float hs     = (s_editHudScale > 0.0f) ? s_editHudScale : 1.0f;
    const float tds    = tdsRaw / hs;  // DS-space text draw scale

    QFont smallFont = p->font();
    smallFont.setPixelSize(4);
    QFont elemFont = p->font();
    elemFont.setPixelSize(std::max(3, static_cast<int>(3.5f * tds)));
    QFont normalFont = p->font();
    normalFont.setPixelSize(5);

    // ── Preview mode: actual HUD rendering ─────────────────────────────────
    if (s_editPreviewMode) {
        DrawEditHudPreview(p, cfg, tds, s_editHudScale, topStretchX);

        // Selection highlight over the live preview
        if (s_editSelected >= 0 && s_editSelected < kEditElemCount) {
            const QRectF& selR = s_editRects[s_editSelected];
            if (!selR.isEmpty()) {
                p->setBrush(Qt::NoBrush);
                p->setPen(QPen(QColor(255, 200, 80, 200), 0.8));
                p->drawRect(selR);
            }
            // Properties panel overlaid on the preview
            DrawElemPropsPanel(p, cfg, smallFont, normalFont);
        }

    } else {
    // ── Normal mode: element boxes ─────────────────────────────────────────

    p->fillRect(QRectF(leftX, 0.0f, 256.0f * topStretchX, 192.0f), QColor(0, 0, 0, 80));
    p->setPen(Qt::NoPen);

    for (int i = 0; i < kEditElemCount; ++i) {
        const QRectF& r = s_editRects[i];
        if (r.isEmpty()) continue;
        const bool sel = (i == s_editSelected);
        const bool hov = (i == s_editHovered);
        const HudEditElemDesc& d = kEditElems[i];

        const bool hidden = d.showKey && !cfg.GetBool(d.showKey);
        const QColor& bgc = hidden ? (sel ? kElemHiddenSelBg : hov ? kElemHiddenHovBg : kElemHiddenBg)
                                   : (sel ? kElemSelBg       : hov ? kElemHovBg       : kElemNormBg);
        p->setBrush(bgc);
        p->setPen(sel ? QPen(kAccent, 0.8)
                      : QPen(QColor(90, 120, 170, hov ? 60 : 30), 0.3));
        p->drawRoundedRect(r, kCornerRadius, kCornerRadius);
        p->setBrush(Qt::NoBrush);

        p->setFont(elemFont);
        const bool isGauge   = (d.lengthKey != nullptr);
        const bool isWpnIcon = (i == 3);
        const bool isBmbIcon = (i == 10);
        const bool isRadar   = (i == 11);

        if (isGauge && !hidden) {
            QColor gc(255, 255, 255);
            if (d.colorRKey)
                gc = QColor(cfg.GetInt(d.colorRKey), cfg.GetInt(d.colorGKey), cfg.GetInt(d.colorBKey));
            gc.setAlpha(180);
            const int ori = (d.orientKey != nullptr) ? cfg.GetInt(d.orientKey) : 0;
            QRectF fillR = r;
            if (ori == 1)
                fillR.setHeight(r.height() * 0.5);
            else
                fillR.setWidth(r.width() * 0.5);
            p->fillRect(fillR, gc);
            p->setPen(QPen(Qt::white, 0.3));
            if (ori == 1)
                p->drawLine(QPointF(r.left(), fillR.bottom()), QPointF(r.right(), fillR.bottom()));
            else
                p->drawLine(QPointF(fillR.right(), r.top()), QPointF(fillR.right(), r.bottom()));
        } else if (isWpnIcon && !hidden) {
            EnsureIconsLoaded();
            const QImage& icon = s_weaponIcons[0];
            if (!icon.isNull()) {
                p->drawImage(r, icon);
            } else {
                p->setPen(Qt::white);
                p->drawText(r, Qt::AlignCenter, QStringLiteral("WPN"));
            }
        } else if (isBmbIcon && !hidden) {
            EnsureBombIconsLoaded();
            const QImage& icon = s_bombIcons[3];
            if (!icon.isNull()) {
                p->drawImage(r, icon);
            } else {
                p->setPen(Qt::white);
                p->drawText(r, Qt::AlignCenter, QStringLiteral("BMB"));
            }
        } else if (isRadar && !hidden) {
            p->setPen(QPen(QColor(0x66, 0xDD, 0x66, 200), 0.5));
            p->setBrush(QColor(0x22, 0x88, 0x22, 80));
            float sz = std::min(static_cast<float>(r.width()), static_cast<float>(r.height()));
            QRectF circR(r.center().x() - sz * 0.5, r.center().y() - sz * 0.5, sz, sz);
            p->drawEllipse(circR);
            p->setBrush(Qt::NoBrush);
        } else {
            QColor tc(255, 255, 255);
            if (d.colorRKey)
                tc = QColor(cfg.GetInt(d.colorRKey), cfg.GetInt(d.colorGKey), cfg.GetInt(d.colorBKey));
            p->setPen(tc);

            const char* sampleText = nullptr;
            switch (i) {
            case 0:  sampleText = "100";     break;
            case 2:  sampleText = "PWR 50";  break;
            case 5:  sampleText = "1st | 5"; break;
            case 6:  sampleText = "#1";      break;
            case 7:  sampleText = "2:30";    break;
            case 8:  sampleText = "5:00";    break;
            case 9:  sampleText = "x3";      break;
            default: sampleText = d.name;    break;
            }

            if (r.height() > r.width() * 1.3) {
                p->save();
                p->translate(r.center());
                p->rotate(-90);
                QRectF textRect(-r.height() / 2.0, -r.width() / 2.0, r.height(), r.width());
                p->drawText(textRect, Qt::AlignCenter, QString::fromUtf8(sampleText));
                p->restore();
            } else {
                p->drawText(r, Qt::AlignCenter, QString::fromUtf8(sampleText));
            }
        }
        p->setFont(normalFont);
    }

    // Selected element extras
    if (s_editSelected >= 0 && s_editSelected < kEditElemCount) {
        const HudEditElemDesc& d = kEditElems[s_editSelected];

        // Orientation toggle (gauges only)
        if (d.orientKey != nullptr) {
            QRectF orientRect = GetOrientToggleRect(s_editSelected);
            p->setPen(Qt::NoPen); p->setBrush(kBtnBg);
            p->drawRoundedRect(orientRect, kBtnCorner, kBtnCorner);
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(kPanelBorder, 0.5));
            p->drawRoundedRect(orientRect, kBtnCorner, kBtnCorner);
            float lx = static_cast<float>(orientRect.left())  + 1.5f;
            float rx = static_cast<float>(orientRect.right()) - 2.0f;
            float ty = static_cast<float>(orientRect.top())   + 1.5f;
            float by = static_cast<float>(orientRect.bottom())- 2.0f;
            QPen ap(kLabelColor, 0.8f);
            ap.setCapStyle(Qt::RoundCap);
            p->setPen(ap);
            p->drawLine(QPointF(lx, by), QPointF(rx, by));
            p->drawLine(QPointF(rx, by), QPointF(rx, ty));
            p->drawLine(QPointF(lx, by), QPointF(lx + 1.8f, by - 1.5f));
            p->drawLine(QPointF(lx, by), QPointF(lx + 1.8f, by + 1.5f));
            p->drawLine(QPointF(rx, ty), QPointF(rx - 1.5f, ty + 1.8f));
            p->drawLine(QPointF(rx, ty), QPointF(rx + 1.5f, ty + 1.8f));
        }

        // Resize handles
        if (d.lengthKey != nullptr) {
            QRectF lenH, widH;
            GetResizeHandles(s_editSelected, cfg, lenH, widH);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(100, 140, 255, 200));
            p->drawRoundedRect(lenH, 1.0f, 1.0f);
            if (!widH.isEmpty())
                p->drawRoundedRect(widH, 1.0f, 1.0f);
            p->setBrush(Qt::NoBrush);
        }

        // Properties panel
        DrawElemPropsPanel(p, cfg, smallFont, normalFont);
    }

    } // end of normal (non-preview) element rendering

    // ── Button bar (always drawn in both modes) ─────────────────────────────

    p->setFont(normalFont);
    // Save
    p->setBrush(QColor(25, 105, 50, 220));
    p->setPen(Qt::NoPen);
    p->drawRoundedRect(kEditSaveRect, kBtnCorner, kBtnCorner);
    // Cancel
    p->setBrush(QColor(110, 32, 32, 220));
    p->drawRoundedRect(kEditCancelRect, kBtnCorner, kBtnCorner);
    // Reset
    p->setBrush(QColor(30, 35, 100, 220));
    p->drawRoundedRect(kEditResetRect, kBtnCorner, kBtnCorner);
    p->setBrush(Qt::NoBrush);
    p->setPen(kValueColor);
    p->drawText(kEditSaveRect,   Qt::AlignCenter, QStringLiteral("\u2713 Save"));
    p->drawText(kEditCancelRect, Qt::AlignCenter, QStringLiteral("\u2717 Cancel"));
    p->drawText(kEditResetRect,  Qt::AlignCenter, QStringLiteral("\u21ba Reset"));

    // ── Text Scale slider ───────────────────────────────────────────────────
    {
        int txSc = std::max(kTsMin, std::min(kTsMax, cfg.GetInt("Metroid.Visual.HudTextScale")));
        p->setBrush(kPanelBg);
        p->setPen(QPen(kPanelBorder, 0.3));
        p->drawRoundedRect(kEditTextScaleRect, kBtnCorner, kBtnCorner);
        p->setBrush(Qt::NoBrush);
        p->setFont(smallFont);

        const float tsX = static_cast<float>(kEditTextScaleRect.left()) + 2.0f;
        const float tsY = static_cast<float>(kEditTextScaleRect.top());
        const float tsH = static_cast<float>(kEditTextScaleRect.height());

        // Label
        p->setPen(kLabelColor);
        p->drawText(QRectF(tsX, tsY, kTsLabelW, tsH), Qt::AlignLeft | Qt::AlignVCenter,
                    QStringLiteral("Text"));

        // Track
        const QRectF tr = TsTrackRect();
        const float frac = static_cast<float>(txSc - kTsMin) / (kTsMax - kTsMin);
        p->setBrush(kCtrlBg);
        p->setPen(Qt::NoPen);
        p->drawRoundedRect(tr, 1.0, 1.0);
        p->setBrush(kAccentDim);
        p->drawRoundedRect(QRectF(tr.left(), tr.top(), tr.width() * frac, tr.height()), 1.0, 1.0);
        p->setBrush(Qt::NoBrush);
        // Thumb
        const float thumbX = tr.left() + tr.width() * frac - 1.0f;
        p->fillRect(QRectF(thumbX, tsY + 0.5f, 2.0f, tsH - 1.0f), QColor(200, 220, 255, 240));
        // Track border
        p->setPen(QPen(kPanelBorder, 0.4));
        p->drawRoundedRect(tr, 1.0, 1.0);
        // Value text
        p->setPen(kValueColor);
        p->setFont(smallFont);
        p->drawText(tr, Qt::AlignCenter, QString::number(txSc) + QStringLiteral("%"));
    }

    // ── Auto-Scale Global Cap slider ────────────────────────────────────────
    {
        int ascVal = std::clamp(cfg.GetInt("Metroid.Visual.HudAutoScaleCap"), kAscMin, kAscMax);
        p->setBrush(kPanelBg);
        p->setPen(QPen(kPanelBorder, 0.3));
        p->drawRoundedRect(kEditAutoScaleCapRect, kBtnCorner, kBtnCorner);
        p->setBrush(Qt::NoBrush);
        p->setFont(smallFont);

        const float ascX = static_cast<float>(kEditAutoScaleCapRect.left()) + 2.0f;
        const float ascY = static_cast<float>(kEditAutoScaleCapRect.top());
        const float ascH = static_cast<float>(kEditAutoScaleCapRect.height());

        // Label
        p->setPen(kLabelColor);
        p->drawText(QRectF(ascX, ascY, kAscLabelW, ascH), Qt::AlignLeft | Qt::AlignVCenter,
                    QStringLiteral("Auto"));

        // Track
        const QRectF atr = AscTrackRect();
        const float ascFrac = static_cast<float>(ascVal - kAscMin) / (kAscMax - kAscMin);
        p->setBrush(kCtrlBg);
        p->setPen(Qt::NoPen);
        p->drawRoundedRect(atr, 1.0, 1.0);
        p->setBrush(QColor(60, 110, 60, 180));
        p->drawRoundedRect(QRectF(atr.left(), atr.top(), atr.width() * ascFrac, atr.height()), 1.0, 1.0);
        p->setBrush(Qt::NoBrush);
        // Thumb
        const float ascThumbX = atr.left() + atr.width() * ascFrac - 1.0f;
        p->fillRect(QRectF(ascThumbX, ascY + 0.5f, 2.0f, ascH - 1.0f), QColor(180, 240, 180, 240));
        // Track border
        p->setPen(QPen(kPanelBorder, 0.4));
        p->drawRoundedRect(atr, 1.0, 1.0);
        // Value text
        p->setPen(kValueColor);
        p->setFont(smallFont);
        p->drawText(atr, Qt::AlignCenter, QString::number(ascVal) + QStringLiteral("%"));
    }

    // ── Crosshair toggle button ─────────────────────────────────────────────
    {
        p->setBrush(s_crosshairPanelOpen ? QColor(35, 90, 45, 220) : kPanelBg);
        p->setPen(Qt::NoPen);
        p->drawRoundedRect(kEditCrosshairBtnRect, kBtnCorner, kBtnCorner);
        p->setBrush(Qt::NoBrush);
        p->setFont(normalFont);
        p->setPen(s_crosshairPanelOpen ? kValueColor : kLabelColor);
        p->drawText(kEditCrosshairBtnRect, Qt::AlignCenter,
                    s_crosshairPanelOpen ? QStringLiteral("Crosshair \u25bc")
                                         : QStringLiteral("Crosshair \u25b6"));
    }

    // ── Preview toggle button ───────────────────────────────────────────────
    {
        p->setBrush(s_editPreviewMode ? QColor(80, 65, 18, 220) : kPanelBg);
        p->setPen(Qt::NoPen);
        p->drawRoundedRect(kEditPreviewBtnRect, kBtnCorner, kBtnCorner);
        p->setBrush(Qt::NoBrush);
        p->setFont(normalFont);
        p->setPen(s_editPreviewMode ? QColor(255, 210, 100) : kLabelColor);
        p->drawText(kEditPreviewBtnRect, Qt::AlignCenter,
                    s_editPreviewMode ? QStringLiteral("\u25a0 Preview ON")
                                      : QStringLiteral("\u25b6 Preview"));
    }

    // ── Crosshair panel (visible in both modes when open) ───────────────────
    if (s_crosshairPanelOpen) {
        const int totalRows = CountCrosshairRows();
        const int visCount  = std::min(totalRows, kCrosshairMaxVisible);
        const float panelH  = visCount * kPropRowH + 4.0f;
        const QRectF panelRect(kCrosshairPanelX, kCrosshairPanelY, kPropPanelW, panelH);

        const int scrollOfs = std::min(s_crosshairPanelScroll,
                                       std::max(0, totalRows - kCrosshairMaxVisible));

        p->setBrush(kPanelShadow);
        p->setPen(Qt::NoPen);
        p->drawRoundedRect(panelRect.translated(1.0, 1.0), kBtnCorner, kBtnCorner);
        p->setBrush(kPanelBg);
        p->setPen(QPen(kPanelBorder, 0.3));
        p->drawRoundedRect(panelRect, kBtnCorner, kBtnCorner);
        p->setPen(QPen(kAccent, 0.5));
        p->drawLine(panelRect.left()+kBtnCorner, panelRect.top(), panelRect.right()-kBtnCorner, panelRect.top());
        p->setBrush(Qt::NoBrush);
        if (totalRows > visCount) {
            const float sbW    = 2.5f;
            const float sbX    = panelRect.right() - sbW - 0.5f;
            const float sbY    = panelRect.top()   + 2.0f;
            const float sbH    = panelH - 4.0f;
            const float thumbH = sbH * (float)visCount / totalRows;
            const float thumbY = sbY + sbH * (float)scrollOfs / totalRows;
            p->fillRect(QRectF(sbX, sbY, sbW, sbH),       kCtrlBg);
            p->fillRect(QRectF(sbX, thumbY, sbW, thumbH), kAccentDim);
        }

        const float rowX  = kCrosshairPanelX + 2.0f;
        const float ctrlX = rowX + kPropLabelW;
        const float ctrlW = kPropCtrlW;

        p->setFont(smallFont);

        for (int vi = 0; vi < visCount; ++vi) {
            const int rowIdx = vi + scrollOfs;
            const float rowY = kCrosshairPanelY + 2.0f + vi * kPropRowH;
            bool isColorRow, isInnerHdr, isOuterHdr;
            const HudEditPropDesc* pr = GetCrosshairPropForRow(rowIdx, isColorRow, isInnerHdr, isOuterHdr);

            if (isColorRow) {
                int r = cfg.GetInt("Metroid.Visual.CrosshairColorR");
                int g = cfg.GetInt("Metroid.Visual.CrosshairColorG");
                int b = cfg.GetInt("Metroid.Visual.CrosshairColorB");
                p->setPen(kLabelColor);
                p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                            Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Color"));
                QRectF swR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                p->setBrush(QColor(r, g, b));
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(swR, 1.5, 1.5); p->setBrush(Qt::NoBrush);
            } else if (isInnerHdr) {
                QRectF hdrR(rowX, rowY + 0.5f, kPropLabelW + ctrlW, kPropRowH - 1.0f);
                p->setBrush(s_innerSectionOpen ? QColor(35, 90, 45, 210) : kBtnBg);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(hdrR, 1.5, 1.5);
                p->setBrush(Qt::NoBrush);
                p->setPen(s_innerSectionOpen ? kValueColor : kLabelColor);
                p->drawText(hdrR, Qt::AlignCenter,
                    s_innerSectionOpen ? QStringLiteral("Inner \u25c4") : QStringLiteral("Inner \u25ba"));
            } else if (isOuterHdr) {
                QRectF hdrR(rowX, rowY + 0.5f, kPropLabelW + ctrlW, kPropRowH - 1.0f);
                p->setBrush(s_outerSectionOpen ? QColor(35, 90, 45, 210) : kBtnBg);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(hdrR, 1.5, 1.5);
                p->setBrush(Qt::NoBrush);
                p->setPen(s_outerSectionOpen ? kValueColor : kLabelColor);
                p->drawText(hdrR, Qt::AlignCenter,
                    s_outerSectionOpen ? QStringLiteral("Outer \u25c4") : QStringLiteral("Outer \u25ba"));
            } else if (pr) {
                p->setPen(kLabelColor);
                p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                            Qt::AlignLeft | Qt::AlignVCenter, QString::fromLatin1(pr->label));
                switch (pr->type) {
                case EditPropType::Bool: {
                    bool val = cfg.GetBool(pr->cfgKey);
                    QRectF swR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                    p->setBrush(val ? kBtnOnBg : kBtnOffBg);
                    p->setPen(Qt::NoPen);
                    p->drawRoundedRect(swR, 1.5, 1.5);
                    p->setBrush(Qt::NoBrush);
                    p->setPen(kValueColor);
                    p->drawText(swR, Qt::AlignCenter, val ? QStringLiteral("ON") : QStringLiteral("OFF"));
                    break;
                }
                case EditPropType::Int: {
                    int val = cfg.GetInt(pr->cfgKey);
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF valR2(ctrlX + btnW2, rowY + 1.0f, ctrlW - btnW2 * 2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                    p->drawRoundedRect(decR, 1.5, 1.5);
                    p->drawRoundedRect(incR, 1.5, 1.5);
                    p->setBrush(kCtrlBg);
                    p->drawRoundedRect(valR2, 0, 0);
                    p->setBrush(Qt::NoBrush);
                    p->setPen(kLabelColor);
                    p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                    p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                    p->setPen(kValueColor);
                    p->drawText(valR2, Qt::AlignCenter, QString::number(val));
                    break;
                }
                case EditPropType::Enum: {
                    int val = cfg.GetInt(pr->cfgKey);
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF valR2(ctrlX + btnW2, rowY + 1.0f, ctrlW - btnW2 * 2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                    p->drawRoundedRect(decR, 1.5, 1.5);
                    p->drawRoundedRect(incR, 1.5, 1.5);
                    p->setBrush(kCtrlBg);
                    p->drawRoundedRect(valR2, 0, 0);
                    p->setBrush(Qt::NoBrush);
                    p->setPen(kLabelColor);
                    p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                    p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                    p->setPen(kValueColor);
                    p->drawText(valR2, Qt::AlignCenter, EditEnumLabel(pr->extra1, val));
                    break;
                }
                case EditPropType::Float: {
                    double val = cfg.GetDouble(pr->cfgKey);
                    int pct = static_cast<int>(val * 100.0 + 0.5);
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF valR2(ctrlX + btnW2, rowY + 1.0f, ctrlW - btnW2 * 2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                    p->drawRoundedRect(decR, 1.5, 1.5);
                    p->drawRoundedRect(incR, 1.5, 1.5);
                    p->setBrush(kCtrlBg);
                    p->drawRoundedRect(valR2, 0, 0);
                    p->setBrush(Qt::NoBrush);
                    p->setPen(kLabelColor);
                    p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                    p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                    p->setPen(kValueColor);
                    p->drawText(valR2, Qt::AlignCenter, QString::number(pct) + QStringLiteral("%"));
                    break;
                }
                default: break;
                }
            }
        }

        // Inner / Outer side panel
        {
            const HudEditPropDesc* sideProps = nullptr;
            int sidePropCount = 0;
            if (s_innerSectionOpen) {
                sideProps = kPropsCrosshairInner; sidePropCount = kCrosshairInnerCount;
            } else if (s_outerSectionOpen) {
                sideProps = kPropsCrosshairOuter; sidePropCount = kCrosshairOuterCount;
            }
            if (sideProps) {
                const float sidePanelH = sidePropCount * kPropRowH + 4.0f;
                const QRectF sideRect(kCrosshairSidePanelX, kCrosshairPanelY, kPropPanelW, sidePanelH);
                p->setBrush(kPanelShadow);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(sideRect.translated(1.0, 1.0), kBtnCorner, kBtnCorner);
                p->setBrush(kPanelBg);
                p->setPen(QPen(kPanelBorder, 0.3));
                p->drawRoundedRect(sideRect, kBtnCorner, kBtnCorner);
                p->setPen(QPen(kAccent, 0.5));
                p->drawLine(sideRect.left()+kBtnCorner, sideRect.top(), sideRect.right()-kBtnCorner, sideRect.top());
                p->setBrush(Qt::NoBrush);

                const float sRowX  = kCrosshairSidePanelX + 2.0f;
                const float sCtrlX = sRowX + kPropLabelW;

                for (int i = 0; i < sidePropCount; ++i) {
                    const HudEditPropDesc& pr2 = sideProps[i];
                    const float rowY = kCrosshairPanelY + 2.0f + i * kPropRowH;
                    p->setPen(kLabelColor);
                    p->drawText(QRectF(sRowX, rowY, kPropLabelW, kPropRowH),
                                Qt::AlignLeft | Qt::AlignVCenter, QString::fromLatin1(pr2.label));
                    switch (pr2.type) {
                    case EditPropType::Bool: {
                        bool val = cfg.GetBool(pr2.cfgKey);
                        QRectF swR(sCtrlX, rowY + 1.0f, kPropCtrlW, kPropRowH - 2.0f);
                        p->setBrush(val ? kBtnOnBg : kBtnOffBg);
                        p->setPen(Qt::NoPen);
                        p->drawRoundedRect(swR, 1.5, 1.5);
                        p->setBrush(Qt::NoBrush);
                        p->setPen(kValueColor);
                        p->drawText(swR, Qt::AlignCenter, val ? QStringLiteral("ON") : QStringLiteral("OFF"));
                        break;
                    }
                    case EditPropType::Int: {
                        int val = cfg.GetInt(pr2.cfgKey);
                        float btnW2 = 8.0f;
                        QRectF decR(sCtrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                        QRectF valR2(sCtrlX + btnW2, rowY + 1.0f, kPropCtrlW - btnW2 * 2, kPropRowH - 2.0f);
                        QRectF incR(sCtrlX + kPropCtrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                        p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                        p->drawRoundedRect(decR, 1.5, 1.5);
                        p->drawRoundedRect(incR, 1.5, 1.5);
                        p->setBrush(kCtrlBg);
                        p->drawRoundedRect(valR2, 0, 0);
                        p->setBrush(Qt::NoBrush);
                        p->setPen(kLabelColor);
                        p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                        p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                        p->setPen(kValueColor);
                        p->drawText(valR2, Qt::AlignCenter, QString::number(val));
                        break;
                    }
                    case EditPropType::Float: {
                        double val = cfg.GetDouble(pr2.cfgKey);
                        int pct = static_cast<int>(val * 100.0 + 0.5);
                        float btnW2 = 8.0f;
                        QRectF decR(sCtrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                        QRectF valR2(sCtrlX + btnW2, rowY + 1.0f, kPropCtrlW - btnW2 * 2, kPropRowH - 2.0f);
                        QRectF incR(sCtrlX + kPropCtrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                        p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                        p->drawRoundedRect(decR, 1.5, 1.5);
                        p->drawRoundedRect(incR, 1.5, 1.5);
                        p->setBrush(kCtrlBg);
                        p->drawRoundedRect(valR2, 0, 0);
                        p->setBrush(Qt::NoBrush);
                        p->setPen(kLabelColor);
                        p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                        p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                        p->setPen(kValueColor);
                        p->drawText(valR2, Qt::AlignCenter, QString::number(pct) + QStringLiteral("%"));
                        break;
                    }
                    default: break;
                    }
                }
            }
        }

        // Crosshair preview
        {
            const float pvX = kCrosshairPreviewX;
            const float pvY = kCrosshairPanelY;
            const float pvS = static_cast<float>(kCrosshairPreviewSize);
            const float fcx = pvX + pvS * 0.5f;
            const float fcy = pvY + pvS * 0.5f;

            p->setBrush(QColor(8, 10, 22, 220));
            p->setPen(QPen(kPanelBorder, 0.3));
            p->drawRoundedRect(QRectF(pvX, pvY, pvS, pvS), kBtnCorner, kBtnCorner);
            p->setBrush(Qt::NoBrush);

            const int cr  = cfg.GetInt("Metroid.Visual.CrosshairColorR");
            const int cg  = cfg.GetInt("Metroid.Visual.CrosshairColorG");
            const int cb2 = cfg.GetInt("Metroid.Visual.CrosshairColorB");
            const bool tStyle    = cfg.GetBool("Metroid.Visual.CrosshairTStyle");
            const bool innerShow = cfg.GetBool("Metroid.Visual.CrosshairInnerShow");
            const bool outerShow = cfg.GetBool("Metroid.Visual.CrosshairOuterShow");
            const bool centerDot = cfg.GetBool("Metroid.Visual.CrosshairCenterDot");
            constexpr float PVS = 2.0f;

            // Helper lambda: draw arms using float rects with properly scaled center gap
            auto drawArms = [&](float lenX, float lenY, float offset, float thick,
                                bool tSt, const QColor& clr) {
                const float ht = thick * 0.5f;
                if (lenX > 0.0f) {
                    p->fillRect(QRectF(fcx - offset - lenX, fcy - ht, lenX, thick), clr);
                    p->fillRect(QRectF(fcx + offset + PVS,  fcy - ht, lenX, thick), clr);
                }
                if (lenY > 0.0f) {
                    p->fillRect(QRectF(fcx - ht, fcy + offset + PVS,  thick, lenY), clr);
                    if (!tSt)
                        p->fillRect(QRectF(fcx - ht, fcy - offset - lenY, thick, lenY), clr);
                }
            };

            p->setClipRect(QRectF(pvX + 1.0f, pvY + 1.0f, pvS - 2.0f, pvS - 2.0f));
            if (outerShow) {
                const float oLenX  = cfg.GetInt("Metroid.Visual.CrosshairOuterLengthX") * PVS;
                const float oLenY  = cfg.GetInt("Metroid.Visual.CrosshairOuterLengthY") * PVS;
                const float oThick = std::max(1.0f, cfg.GetInt("Metroid.Visual.CrosshairOuterThickness") * PVS);
                const float oOfs   = cfg.GetInt("Metroid.Visual.CrosshairOuterOffset") * PVS;
                const float oOpac  = static_cast<float>(cfg.GetDouble("Metroid.Visual.CrosshairOuterOpacity"));
                drawArms(oLenX, oLenY, oOfs, oThick, tStyle,
                         QColor(cr, cg, cb2, static_cast<int>(oOpac * 255.0f)));
            }
            if (innerShow) {
                const float iLenX  = cfg.GetInt("Metroid.Visual.CrosshairInnerLengthX") * PVS;
                const float iLenY  = cfg.GetInt("Metroid.Visual.CrosshairInnerLengthY") * PVS;
                const float iThick = std::max(1.0f, cfg.GetInt("Metroid.Visual.CrosshairInnerThickness") * PVS);
                const float iOfs   = cfg.GetInt("Metroid.Visual.CrosshairInnerOffset") * PVS;
                const float iOpac  = static_cast<float>(cfg.GetDouble("Metroid.Visual.CrosshairInnerOpacity"));
                drawArms(iLenX, iLenY, iOfs, iThick, tStyle,
                         QColor(cr, cg, cb2, static_cast<int>(iOpac * 255.0f)));
            }
            if (centerDot) {
                const float dotThick = std::max(1.0f, cfg.GetInt("Metroid.Visual.CrosshairDotThickness") * PVS);
                const float dotOpac = static_cast<float>(cfg.GetDouble("Metroid.Visual.CrosshairDotOpacity"));
                const float dh = dotThick * 0.5f;
                QColor dotColor(cr, cg, cb2, static_cast<int>(dotOpac * 255.0f));
                p->fillRect(QRectF(fcx - dh, fcy - dh, dotThick, dotThick), dotColor);
            }
            p->setClipping(false);

            p->setFont(smallFont);
            p->setPen(QColor(100, 100, 140));
            p->drawText(QRectF(pvX, pvY + pvS - kPropRowH, pvS, kPropRowH),
                        Qt::AlignCenter, QStringLiteral("preview"));
        }

        p->setFont(normalFont);
    }
}

// ── Public API ──────────────────────────────────────────────────────────────

void CustomHud_EnterEditMode(EmuInstance* emu, Config::Table& cfg)
{
    s_editEmu           = emu;
    s_editSelected      = -1;
    s_editHovered       = -1;
    s_dragging          = false;
    s_resizingLength    = false;
    s_resizingWidth     = false;
    s_editPreviewMode   = false;
    SnapshotEditConfig(cfg);
    s_editMode = true;
    CustomHud_InvalidateConfigCache();
    NotifySelectionChanged(-1);

    if (auto* core = emu->getEmuThread()->GetMelonPrimeCore())
        core->isCursorMode = true;
}

void CustomHud_ExitEditMode(bool save, Config::Table& cfg)
{
    if (!save) {
        RestoreEditSnapshot(cfg);
    }
    CustomHud_InvalidateConfigCache();
    if (save) Config::Save();

    s_editMode           = false;
    s_editSelected       = -1;
    s_editHovered           = -1;
    s_dragging              = false;
    s_textScaleDragging     = false;
    s_anchorPickerOpen      = false;
    s_crosshairPanelOpen    = false;
    s_innerSectionOpen      = false;
    s_outerSectionOpen      = false;
    s_crosshairPanelScroll  = 0;
    s_resizingLength     = false;
    s_resizingWidth      = false;
    s_editPreviewMode    = false;
    s_editEmu            = nullptr;
    NotifySelectionChanged(-1);
}

bool CustomHud_IsEditMode()
{
    return s_editMode;
}

void CustomHud_SetEditSelectionCallback(std::function<void(int)> cb)
{
    s_editSelectionCb = std::move(cb);
}

int CustomHud_GetSelectedElement()
{
    return s_editSelected;
}

void CustomHud_UpdateEditContext(float originX, float originY,
                                  float hudScale, float topStretchX)
{
    s_editOriginX     = originX;
    s_editOriginY     = originY;
    s_editHudScale    = hudScale;
    s_editTopStretchX = topStretchX;
}

void CustomHud_EditMousePress(QPointF pt, Qt::MouseButton btn, Config::Table& cfg)
{
    // Accept both left and right mouse buttons
    if (btn != Qt::LeftButton && btn != Qt::RightButton) return;
    const QPointF ds = WidgetToDS(pt);

    // Right-click: select element under cursor for property editing (both modes)
    if (btn == Qt::RightButton) {
        // Absorb click on button bar items
        if (kEditSaveRect.contains(ds) || kEditCancelRect.contains(ds) ||
            kEditResetRect.contains(ds) || kEditTextScaleRect.contains(ds) ||
            kEditCrosshairBtnRect.contains(ds) || kEditPreviewBtnRect.contains(ds))
            return;
        // Absorb click inside open properties panel
        if (s_editSelected >= 0) {
            const HudEditElemDesc& d = kEditElems[s_editSelected];
            const int totalRows = CountBuiltinRows(d) + d.propCount;
            if (totalRows > 0) {
                QRectF panelRect = ComputePropsPanelRect(s_editRects[s_editSelected], totalRows);
                if (panelRect.contains(ds)) return;
            }
        }
        // Hit-test elements and select
        for (int i = 0; i < kEditElemCount; ++i) {
            if (!s_editRects[i].isEmpty() && s_editRects[i].contains(ds)) {
                if (s_editSelected != i) {
                    s_editSelected = i;
                    s_editPropScroll = 0;
                    s_anchorPickerOpen = false;
                    NotifySelectionChanged(i);
                }
                return;
            }
        }
        // Right-click on empty space: deselect
        if (s_editSelected >= 0) {
            s_editSelected = -1;
            s_anchorPickerOpen = false;
            NotifySelectionChanged(-1);
        }
        return;
    }

    // ── Left-click priority system ──────────────────────────────────────────

    // Priority 1: Save / Cancel / Reset
    if (kEditSaveRect.contains(ds)) {
        CustomHud_ExitEditMode(true, cfg);
        return;
    }
    if (kEditCancelRect.contains(ds)) {
        CustomHud_ExitEditMode(false, cfg);
        return;
    }
    if (kEditResetRect.contains(ds)) {
        ResetEditToDefaults(cfg);
        s_editSelected = -1;
        CustomHud_InvalidateConfigCache();
        NotifySelectionChanged(-1);
        return;
    }

    // Priority 1b: Text Scale slider
    if (kEditTextScaleRect.contains(ds)) {
        const QRectF tr = TsTrackRect();
        if (tr.contains(ds)) {
            cfg.SetInt("Metroid.Visual.HudTextScale", TsValueFromX(static_cast<float>(ds.x())));
            CustomHud_InvalidateConfigCache();
            s_textScaleDragging = true;
        }
        return;
    }

    // Priority 1b2: Auto-Scale Cap slider
    if (kEditAutoScaleCapRect.contains(ds)) {
        const QRectF atr = AscTrackRect();
        if (atr.contains(ds)) {
            cfg.SetInt("Metroid.Visual.HudAutoScaleCap", AscValueFromX(static_cast<float>(ds.x())));
            CustomHud_InvalidateConfigCache();
            s_autoScaleCapDragging = true;
        }
        return;
    }

    // Priority 1c: Crosshair panel toggle
    if (kEditCrosshairBtnRect.contains(ds)) {
        s_crosshairPanelOpen = !s_crosshairPanelOpen;
        s_crosshairPanelScroll = 0;
        return;
    }

    // Priority 1d: Preview toggle
    if (kEditPreviewBtnRect.contains(ds)) {
        s_editPreviewMode = !s_editPreviewMode;
        if (s_editPreviewMode) {
            s_anchorPickerOpen = false;
        }
        return;
    }

    // Priority 1e: Crosshair panel clicks (when open — works in both modes)
    if (s_crosshairPanelOpen) {
        const int totalRows = CountCrosshairRows();
        const int visCount  = std::min(totalRows, kCrosshairMaxVisible);
        const float panelH  = visCount * kPropRowH + 4.0f;
        const QRectF panelRect(kCrosshairPanelX, kCrosshairPanelY, kPropPanelW, panelH);

        if (panelRect.contains(ds)) {
            const float rowX  = kCrosshairPanelX + 2.0f;
            const float ctrlX = rowX + kPropLabelW;
            const float ctrlW = kPropCtrlW;
            const int scrollOfs = std::min(s_crosshairPanelScroll,
                                           std::max(0, totalRows - kCrosshairMaxVisible));

            for (int vi = 0; vi < visCount; ++vi) {
                const int rowIdx = vi + scrollOfs;
                const float rowY = kCrosshairPanelY + 2.0f + vi * kPropRowH;
                QRectF rowRect(rowX, rowY, kPropLabelW + ctrlW, kPropRowH);
                if (!rowRect.contains(ds)) continue;

                bool isColorRow, isInnerHdr, isOuterHdr;
                const HudEditPropDesc* pr = GetCrosshairPropForRow(rowIdx, isColorRow, isInnerHdr, isOuterHdr);
                if (isColorRow) {
                    if (s_colorDialogOpen) return;
                    s_colorDialogOpen = true;
                    int r = cfg.GetInt("Metroid.Visual.CrosshairColorR");
                    int g = cfg.GetInt("Metroid.Visual.CrosshairColorG");
                    int b = cfg.GetInt("Metroid.Visual.CrosshairColorB");
                    QColor chosen = QColorDialog::getColor(QColor(r, g, b), nullptr,
                        QStringLiteral("Crosshair Color"));
                    s_colorDialogOpen = false;
                    if (chosen.isValid()) {
                        cfg.SetInt("Metroid.Visual.CrosshairColorR", chosen.red());
                        cfg.SetInt("Metroid.Visual.CrosshairColorG", chosen.green());
                        cfg.SetInt("Metroid.Visual.CrosshairColorB", chosen.blue());
                        CustomHud_InvalidateConfigCache();
                    }
                    return;
                }
                if (isInnerHdr) {
                    s_innerSectionOpen = !s_innerSectionOpen;
                    if (s_innerSectionOpen) s_outerSectionOpen = false;
                    return;
                }
                if (isOuterHdr) {
                    s_outerSectionOpen = !s_outerSectionOpen;
                    if (s_outerSectionOpen) s_innerSectionOpen = false;
                    return;
                }
                if (!pr) return;

                switch (pr->type) {
                case EditPropType::Bool:
                    cfg.SetBool(pr->cfgKey, !cfg.GetBool(pr->cfgKey));
                    CustomHud_InvalidateConfigCache();
                    return;
                case EditPropType::Enum:
                case EditPropType::Int: {
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    int val = cfg.GetInt(pr->cfgKey);
                    int step = pr->step > 0 ? pr->step : 1;
                    if (decR.contains(ds))
                        cfg.SetInt(pr->cfgKey, std::max(pr->minVal, val - step));
                    else if (incR.contains(ds))
                        cfg.SetInt(pr->cfgKey, std::min(pr->maxVal, val + step));
                    CustomHud_InvalidateConfigCache();
                    return;
                }
                case EditPropType::Float: {
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    int pct = static_cast<int>(cfg.GetDouble(pr->cfgKey) * 100.0 + 0.5);
                    int step = pr->step > 0 ? pr->step : 5;
                    if (decR.contains(ds))
                        cfg.SetDouble(pr->cfgKey, std::max(pr->minVal, pct - step) / 100.0);
                    else if (incR.contains(ds))
                        cfg.SetDouble(pr->cfgKey, std::min(pr->maxVal, pct + step) / 100.0);
                    CustomHud_InvalidateConfigCache();
                    return;
                }
                default: return;
                }
            }
            return; // absorbed by panel
        }

        // Side panel clicks (Inner / Outer props)
        {
            const HudEditPropDesc* sideClickProps = nullptr;
            int sideClickCount = 0;
            if (s_innerSectionOpen) {
                sideClickProps = kPropsCrosshairInner; sideClickCount = kCrosshairInnerCount;
            } else if (s_outerSectionOpen) {
                sideClickProps = kPropsCrosshairOuter; sideClickCount = kCrosshairOuterCount;
            }
            if (sideClickProps) {
                const float sidePanelH = sideClickCount * kPropRowH + 4.0f;
                const QRectF sideRect(kCrosshairSidePanelX, kCrosshairPanelY, kPropPanelW, sidePanelH);
                if (sideRect.contains(ds)) {
                    const float sRowX  = kCrosshairSidePanelX + 2.0f;
                    const float sCtrlX = sRowX + kPropLabelW;
                    for (int i = 0; i < sideClickCount; ++i) {
                        const float rowY = kCrosshairPanelY + 2.0f + i * kPropRowH;
                        QRectF rowRect(sRowX, rowY, kPropLabelW + kPropCtrlW, kPropRowH);
                        if (!rowRect.contains(ds)) continue;
                        const HudEditPropDesc& pr2 = sideClickProps[i];
                        switch (pr2.type) {
                        case EditPropType::Bool:
                            cfg.SetBool(pr2.cfgKey, !cfg.GetBool(pr2.cfgKey));
                            CustomHud_InvalidateConfigCache();
                            return;
                        case EditPropType::Int: {
                            float btnW2 = 8.0f;
                            QRectF decR(sCtrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                            QRectF incR(sCtrlX + kPropCtrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                            int val = cfg.GetInt(pr2.cfgKey);
                            int step = pr2.step > 0 ? pr2.step : 1;
                            if (decR.contains(ds))
                                cfg.SetInt(pr2.cfgKey, std::max(pr2.minVal, val - step));
                            else if (incR.contains(ds))
                                cfg.SetInt(pr2.cfgKey, std::min(pr2.maxVal, val + step));
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        case EditPropType::Float: {
                            float btnW2 = 8.0f;
                            QRectF decR(sCtrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                            QRectF incR(sCtrlX + kPropCtrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                            int pct = static_cast<int>(cfg.GetDouble(pr2.cfgKey) * 100.0 + 0.5);
                            int step = pr2.step > 0 ? pr2.step : 5;
                            if (decR.contains(ds))
                                cfg.SetDouble(pr2.cfgKey, std::max(pr2.minVal, pct - step) / 100.0);
                            else if (incR.contains(ds))
                                cfg.SetDouble(pr2.cfgKey, std::min(pr2.maxVal, pct + step) / 100.0);
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        default: return;
                        }
                    }
                    return; // absorbed by side panel
                }
            }
        }
    }

    // Priority 2–4: properties panel, anchor picker, orientation, resize (works in both modes)
    if (s_editSelected >= 0 && s_editSelected < kEditElemCount) {
        const HudEditElemDesc& d = kEditElems[s_editSelected];

        // Priority 2: Properties panel
        {
            const int builtinRows = CountBuiltinRows(d);
            const int totalRows = builtinRows + d.propCount;
            if (totalRows > 0) {
                QRectF panelRect = ComputePropsPanelRect(s_editRects[s_editSelected], totalRows);
                if (panelRect.contains(ds)) {
                    int visCount = std::min(totalRows, kPropMaxVisible);
                    int scrollOfs = std::min(s_editPropScroll, std::max(0, totalRows - kPropMaxVisible));
                    float panelInnerY = static_cast<float>(panelRect.top()) + 2.0f;
                    float rowX = static_cast<float>(panelRect.left()) + 2.0f;
                    for (int vi = 0; vi < visCount; ++vi) {
                        int rowIdx = vi + scrollOfs;
                        if (rowIdx >= totalRows) break;
                        float rowY = panelInnerY + vi * kPropRowH;
                        float ctrlX = rowX + kPropLabelW;
                        float ctrlW = kPropCtrlW;
                        QRectF rowRect(ctrlX, rowY, ctrlW, kPropRowH);
                        if (!rowRect.contains(ds)) continue;

                        int builtinIdx = rowIdx;
                        bool isShowRow = false, isColorRow = false, isAnchorRow = false;
                        if (d.showKey && builtinIdx == 0) {
                            isShowRow = true;
                        } else {
                            if (d.showKey) --builtinIdx;
                            if (d.colorRKey && builtinIdx == 0) {
                                isColorRow = true;
                            } else {
                                if (d.colorRKey) --builtinIdx;
                                if (builtinIdx == 0 && rowIdx < builtinRows) {
                                    isAnchorRow = true;
                                }
                            }
                        }

                        if (isShowRow) {
                            cfg.SetBool(d.showKey, !cfg.GetBool(d.showKey));
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        if (isColorRow) {
                            if (s_colorDialogOpen) return;
                            QColor cur(cfg.GetInt(d.colorRKey), cfg.GetInt(d.colorGKey), cfg.GetInt(d.colorBKey));
                            s_colorDialogOpen = true;
                            QColor picked = QColorDialog::getColor(cur, nullptr, QStringLiteral("Pick Color"));
                            s_colorDialogOpen = false;
                            if (picked.isValid()) {
                                cfg.SetInt(d.colorRKey, picked.red());
                                cfg.SetInt(d.colorGKey, picked.green());
                                cfg.SetInt(d.colorBKey, picked.blue());
                                CustomHud_InvalidateConfigCache();
                            }
                            return;
                        }
                        if (isAnchorRow) {
                            float halfW = ctrlW / 2.0f;
                            QRectF btnR(ctrlX + halfW, rowRect.top(), halfW, kPropRowH);
                            if (btnR.contains(ds)) {
                                s_anchorPickerOpen = !s_anchorPickerOpen;
                            }
                            return;
                        }

                        int propIdx = rowIdx - builtinRows;
                        if (propIdx < 0 || propIdx >= d.propCount) continue;
                        const HudEditPropDesc& pr = d.props[propIdx];

                        switch (pr.type) {
                        case EditPropType::Bool:
                            cfg.SetBool(pr.cfgKey, !cfg.GetBool(pr.cfgKey));
                            CustomHud_InvalidateConfigCache();
                            return;
                        case EditPropType::Enum:
                        case EditPropType::Int: {
                            float arrowW = 8.0f;
                            int step = pr.step > 0 ? pr.step : 1;
                            int val = cfg.GetInt(pr.cfgKey);
                            if (ds.x() < ctrlX + arrowW) val = std::max(pr.minVal, val - step);
                            else if (ds.x() > ctrlX + ctrlW - arrowW) val = std::min(pr.maxVal, val + step);
                            cfg.SetInt(pr.cfgKey, val);
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        case EditPropType::Float: {
                            float arrowW = 8.0f;
                            double step = (pr.step > 0 ? pr.step : 5) / 100.0;
                            double val = cfg.GetDouble(pr.cfgKey);
                            double minV = pr.minVal / 100.0, maxV = pr.maxVal / 100.0;
                            if (ds.x() < ctrlX + arrowW) val = std::max(minV, val - step);
                            else if (ds.x() > ctrlX + ctrlW - arrowW) val = std::min(maxV, val + step);
                            cfg.SetDouble(pr.cfgKey, val);
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        case EditPropType::String: {
                            if (s_colorDialogOpen) return;
                            s_colorDialogOpen = true;
                            QString cur = QString::fromStdString(cfg.GetString(pr.cfgKey));
                            bool ok = false;
                            QString txt = QInputDialog::getText(nullptr,
                                QStringLiteral("Edit ") + QString::fromUtf8(pr.label),
                                QString::fromUtf8(pr.label), QLineEdit::Normal, cur, &ok);
                            s_colorDialogOpen = false;
                            if (ok) {
                                cfg.SetString(pr.cfgKey, txt.toStdString());
                                CustomHud_InvalidateConfigCache();
                            }
                            return;
                        }
                        case EditPropType::SubColor: {
                            float ovrW = 14.0f;
                            if (ds.x() < ctrlX + ovrW) {
                                cfg.SetBool(pr.cfgKey, !cfg.GetBool(pr.cfgKey));
                                CustomHud_InvalidateConfigCache();
                            } else if (!s_colorDialogOpen) {
                                QColor cur(cfg.GetInt(pr.extra1), cfg.GetInt(pr.extra2), cfg.GetInt(pr.extra3));
                                s_colorDialogOpen = true;
                                QColor picked = QColorDialog::getColor(cur, nullptr,
                                    QStringLiteral("Pick ") + QString::fromUtf8(pr.label) + QStringLiteral(" Color"));
                                s_colorDialogOpen = false;
                                if (picked.isValid()) {
                                    cfg.SetInt(pr.extra1, picked.red());
                                    cfg.SetInt(pr.extra2, picked.green());
                                    cfg.SetInt(pr.extra3, picked.blue());
                                    cfg.SetBool(pr.cfgKey, false);
                                    CustomHud_InvalidateConfigCache();
                                }
                            }
                            return;
                        }
                        case EditPropType::Color: break;
                        }
                    }
                    // Anchor grid click
                    if (s_anchorPickerOpen) {
                        float gridLeft = static_cast<float>(panelRect.left()) + 2.0f;
                        float gridTop  = static_cast<float>(panelRect.top()) + 2.0f +
                                         visCount * kPropRowH + 2.0f;
                        float cellW = (kPropPanelW - 4.0f) / 3.0f;
                        float cellH = kAnchorGridCellH;
                        for (int a = 0; a < 9; ++a) {
                            int col = a % 3, row = a / 3;
                            QRectF cell(gridLeft + col * cellW, gridTop + row * cellH, cellW, cellH);
                            if (cell.contains(ds)) {
                                const int oldAnchor = cfg.GetInt(d.anchorKey);
                                if (a != oldAnchor) {
                                    const int oldOfsX = cfg.GetInt(d.ofsXKey);
                                    const int oldOfsY = cfg.GetInt(d.ofsYKey);
                                    int finalX, finalY, newBaseX, newBaseY;
                                    ApplyAnchor(oldAnchor, oldOfsX, oldOfsY, finalX, finalY, s_editTopStretchX);
                                    ApplyAnchor(a, 0, 0, newBaseX, newBaseY, s_editTopStretchX);
                                    cfg.SetInt(d.anchorKey, a);
                                    cfg.SetInt(d.ofsXKey, finalX - newBaseX);
                                    cfg.SetInt(d.ofsYKey, finalY - newBaseY);
                                    CustomHud_InvalidateConfigCache();
                                }
                                s_anchorPickerOpen = false;
                                return;
                            }
                        }
                    }
                    return; // absorbed by panel
                }
            }
        }

        // Priority 3: Orientation toggle (normal mode only — in preview mode it's hard to see)
        if (!s_editPreviewMode && d.orientKey != nullptr && GetOrientToggleRect(s_editSelected).contains(ds)) {
            cfg.SetInt(d.orientKey, cfg.GetInt(d.orientKey) == 0 ? 1 : 0);
            CustomHud_InvalidateConfigCache();
            s_editRects[s_editSelected] = ComputeEditBounds(s_editSelected, cfg, s_editTopStretchX);
            return;
        }

        // Priority 4: Resize handles (normal mode only)
        if (!s_editPreviewMode && d.lengthKey != nullptr) {
            QRectF lenH, widH;
            GetResizeHandles(s_editSelected, cfg, lenH, widH);
            if (lenH.contains(ds)) {
                s_resizingLength = true;
                s_resizeStartVal = cfg.GetInt(d.lengthKey);
                s_resizeStartDS  = ds;
                return;
            }
            if (widH.contains(ds)) {
                s_resizingWidth  = true;
                s_resizeStartVal = cfg.GetInt(d.widthKey);
                s_resizeStartDS  = ds;
                return;
            }
        }
    }

    // Priority 5: Element drag (left-click on element — works in both modes)
    for (int i = 0; i < kEditElemCount; ++i) {
        if (!s_editRects[i].contains(ds)) continue;
        const HudEditElemDesc& di = kEditElems[i];

        // Auto-switch gauge PosMode from text-relative (0) to independent (1)
        if (di.posModeKey != nullptr && cfg.GetInt(di.posModeKey) == 0) {
            if (UNLIKELY(!s_cache.valid) || s_cache.lastHudScale != s_editHudScale) {
                RefreshCachedConfig(cfg, s_editTopStretchX, s_editHudScale);
                s_cache.valid = true;
            } else if (s_cache.lastStretchX != s_editTopStretchX) {
                RecomputeAnchorPositions(s_editTopStretchX);
            }
            int visualX = 0, visualY = 0;
            if (i == 1) { visualX = s_cache.hp.hpGaugePosX;       visualY = s_cache.hp.hpGaugePosY; }
            if (i == 4) { visualX = s_cache.weapon.ammoGaugePosX;  visualY = s_cache.weapon.ammoGaugePosY; }
            cfg.SetInt(di.posModeKey, 1);
            cfg.SetInt(di.anchorKey, 0);
            cfg.SetInt(di.ofsXKey, visualX);
            cfg.SetInt(di.ofsYKey, visualY);
            CustomHud_InvalidateConfigCache();
        }

        s_editSelected  = i;
        s_editPropScroll = 0;
        s_anchorPickerOpen = false;
        s_dragging      = true;
        s_dragStartDS   = ds;
        s_dragStartOfsX = cfg.GetInt(di.ofsXKey);
        s_dragStartOfsY = cfg.GetInt(di.ofsYKey);
        s_editHovered   = i;
        NotifySelectionChanged(i);
        return;
    }

    s_editSelected = -1;  // deselect
    s_anchorPickerOpen = false;
    NotifySelectionChanged(-1);
}

void CustomHud_EditMouseMove(QPointF pt, Config::Table& cfg)
{
    const QPointF ds = WidgetToDS(pt);

    if (s_textScaleDragging) {
        cfg.SetInt("Metroid.Visual.HudTextScale", TsValueFromX(static_cast<float>(ds.x())));
        CustomHud_InvalidateConfigCache();
        return;
    }

    if (s_autoScaleCapDragging) {
        cfg.SetInt("Metroid.Visual.HudAutoScaleCap", AscValueFromX(static_cast<float>(ds.x())));
        CustomHud_InvalidateConfigCache();
        return;
    }

    if (s_dragging && s_editSelected >= 0) {
        const HudEditElemDesc& d = kEditElems[s_editSelected];
        const int newOfsX = static_cast<int>(std::round(s_dragStartOfsX + ds.x() - s_dragStartDS.x()));
        const int newOfsY = static_cast<int>(std::round(s_dragStartOfsY + ds.y() - s_dragStartDS.y()));
        cfg.SetInt(d.ofsXKey, std::max(-512, std::min(512, newOfsX)));
        cfg.SetInt(d.ofsYKey, std::max(-512, std::min(512, newOfsY)));
        CustomHud_InvalidateConfigCache();
        return;
    }

    if ((s_resizingLength || s_resizingWidth) && s_editSelected >= 0) {
        const HudEditElemDesc& d = kEditElems[s_editSelected];
        const int ori = d.orientKey ? cfg.GetInt(d.orientKey) : 0;
        const bool squareResize = d.lengthKey && d.widthKey
                                  && strcmp(d.lengthKey, d.widthKey) == 0;
        // Gauge configs are in actual pixels; radar (square) stays DS-space
        const double hs = squareResize ? 1.0 : static_cast<double>(s_editHudScale);
        const int maxLen = squareResize ? 192 : 512;
        const int minLen = squareResize ? 16  : 4;
        const int maxWid = squareResize ? 192 : 128;
        const int minWid = squareResize ? 16  : 1;

        if (s_resizingLength) {
            const double delta = ((ori == 1) ? ds.y() - s_resizeStartDS.y()
                                             : ds.x() - s_resizeStartDS.x()) * hs;
            const int newVal = std::max(minLen, std::min(maxLen,
                static_cast<int>(std::round(s_resizeStartVal + delta))));
            cfg.SetInt(d.lengthKey, newVal);
            if (squareResize) cfg.SetInt(d.widthKey, newVal);
            CustomHud_InvalidateConfigCache();
        } else {
            const double delta = ((ori == 1) ? ds.x() - s_resizeStartDS.x()
                                             : ds.y() - s_resizeStartDS.y()) * hs;
            const int newVal = std::max(minWid, std::min(maxWid,
                static_cast<int>(std::round(s_resizeStartVal + delta))));
            cfg.SetInt(d.widthKey, newVal);
            if (squareResize) cfg.SetInt(d.lengthKey, newVal);
            CustomHud_InvalidateConfigCache();
        }
        return;
    }

    s_editHovered = -1;
    for (int i = 0; i < kEditElemCount; ++i) {
        if (s_editRects[i].contains(ds)) { s_editHovered = i; break; }
    }
}

void CustomHud_EditMouseRelease(QPointF pt, Qt::MouseButton btn, Config::Table& cfg)
{
    Q_UNUSED(pt);
    Q_UNUSED(btn);
    Q_UNUSED(cfg);
    s_dragging          = false;
    s_resizingLength    = false;
    s_resizingWidth     = false;
    s_textScaleDragging = false;
    s_autoScaleCapDragging = false;
}

void CustomHud_EditMouseWheel(QPointF pt, int delta, Config::Table& cfg)
{
    if (!s_editMode) return;
    QPointF ds = WidgetToDS(pt);

    // Text Scale slider wheel
    if (kEditTextScaleRect.contains(ds)) {
        int val = cfg.GetInt("Metroid.Visual.HudTextScale");
        val = std::max(kTsMin, std::min(kTsMax, val + (delta > 0 ? 5 : -5)));
        cfg.SetInt("Metroid.Visual.HudTextScale", val);
        CustomHud_InvalidateConfigCache();
        return;
    }

    // Auto-Scale Cap slider wheel
    if (kEditAutoScaleCapRect.contains(ds)) {
        int val = cfg.GetInt("Metroid.Visual.HudAutoScaleCap");
        val = std::clamp(val + (delta > 0 ? 25 : -25), kAscMin, kAscMax);
        cfg.SetInt("Metroid.Visual.HudAutoScaleCap", val);
        CustomHud_InvalidateConfigCache();
        return;
    }

    // Crosshair panel scroll
    if (s_crosshairPanelOpen) {
        const int totalRows = CountCrosshairRows();
        if (totalRows > kCrosshairMaxVisible) {
            const int visCount = std::min(totalRows, kCrosshairMaxVisible);
            const float panelH = visCount * kPropRowH + 4.0f;
            const QRectF panelRect(kCrosshairPanelX, kCrosshairPanelY, kPropPanelW, panelH);
            if (panelRect.contains(ds)) {
                int maxScroll = totalRows - kCrosshairMaxVisible;
                if (delta > 0) s_crosshairPanelScroll = std::max(0, s_crosshairPanelScroll - 1);
                else if (delta < 0) s_crosshairPanelScroll = std::min(maxScroll, s_crosshairPanelScroll + 1);
                return;
            }
        }
    }

    // Element props panel scroll
    if (s_editSelected < 0) return;
    const HudEditElemDesc& d = kEditElems[s_editSelected];
    const int totalRows = CountBuiltinRows(d) + d.propCount;
    if (totalRows <= kPropMaxVisible) return;

    QRectF panelRect = ComputePropsPanelRect(s_editRects[s_editSelected], totalRows);
    if (!panelRect.contains(ds)) return;

    int maxScroll = totalRows - kPropMaxVisible;
    if (delta > 0) s_editPropScroll = std::max(0, s_editPropScroll - 1);
    else if (delta < 0) s_editPropScroll = std::min(maxScroll, s_editPropScroll + 1);
}
