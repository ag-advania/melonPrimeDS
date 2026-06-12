// This file is a unity-build fragment included by MelonPrimeInputConfig.cpp.
// Do not add it to CMakeLists.txt.

enum class HWType { Bool, Int, Float, String, Anchor9, Align3, Color3, HorizVert, Label,
                    RelIndep,      // 0=Relative to Text, 1=Independent
                    PosMode3,      // 0=Gauge->Text, 1=Independent, 2=Text->Gauge
                    GaugeAnchor5,  // 0=Below, 1=Above, 2=Right, 3=Left, 4=Center
                    AnchorY3,      // 0=Top, 1=Center, 2=Bottom
                    LabelPos5,     // 0=Above, 1=Below, 2=Left, 3=Right, 4=Center
                    Double,        // lo/hi/step stored as x100 integers (e.g. 10,800,25 -> 0.10-8.00 step 0.25)
                    FontMode,      // 0=Default(MPH), 1=System font, 2=Font file (int combo)
                    FontFamily,    // QFontComboBox + picker -> family string
                    FontFile,      // QLineEdit + Browse -> font-file path string
                    FontWeight,    // combo index 0..8 -> Thin..Black
                    ZoomTransitionStyle, // 0..12 crosshair zoom transition presets
                    DotShape,      // 0=Square, 1=Circle (center-dot shape)
                  };

struct HudWidgetProp {
    const char* label;
    HWType      type;
    const char* cfgKey;
    int         min, max, step;
    const char* cfgKeyG;   // Color3 only
    const char* cfgKeyB;   // Color3 only
};

struct HudSubSec {
    const char* title;
    const char* cfgToggleKey;
    const HudWidgetProp* props;
    int propCount;
    const HudSubSec* children;   // nested sub-sections (e.g., Rank/Time inside "Rank / Time")
    int childCount;
};

struct HudMainSec {
    const char* title;
    const char* cfgToggleKey;
    const HudWidgetProp* directProps;   // props shown at group level (before sub-sections)
    int directPropCount;
    const HudSubSec* subs;
    int subCount;
    int previewKind; // 0=None, 1=Crosshair, 2=HpAmmo, 3=MatchStatus, 4=Radar
};

#include "../MelonPrimeHudPropSchema.inc"
#include "MelonPrimeInputConfigHudDialogProps.inc"

#define _P(arr) arr, static_cast<int>(sizeof(arr)/sizeof(arr[0]))
#define SUB(title, key, arr)               { title, key, _P(arr), nullptr, 0 }
#define SUB_NEST(title, key, ch)           { title, key, nullptr, 0, ch, static_cast<int>(sizeof(ch)/sizeof(ch[0])) }

// CROSSHAIR sub-sections
static const HudSubSec kSubsCrosshair[] = {
    SUB("Zoom Crosshair", "Metroid.UI.SectionHudCrosshairZoom", kSecCrosshairZoom),
    SUB("Inner Lines",  "Metroid.UI.SectionHudCrosshairInner",  kSecCrosshairInner),
    SUB("Outer Lines",  "Metroid.UI.SectionHudCrosshairOuter",  kSecCrosshairOuter),
};

// HP sub-sections
static const HudSubSec kSubsHp[] = {
    SUB("HP Number Position",     "Metroid.UI.SectionHudHp",            kSecHp),
    SUB("HP Label Color By Value","Metroid.UI.SectionHudHpTextRamp",    kSecHpTextRamp),
    SUB("HP Outline",             "Metroid.UI.SectionHudHpOutline",      kSecHpOutline),
    SUB("HP Gauge",               "Metroid.UI.SectionHudHpGauge",        kSecHpGauge),
    SUB("HP Gauge Color By Value","Metroid.UI.SectionHudHpGaugeRamp",    kSecHpGaugeRamp),
    SUB("HP Gauge Outline",       "Metroid.UI.SectionHudHpGaugeOutline", kSecHpGaugeOutline),
};

// Ammo sub-sections
static const HudSubSec kSubsAmmo[] = {
    SUB("Ammo Number Position",     "Metroid.UI.SectionHudWeaponAmmo",        kSecWeaponAmmo),
    SUB("Ammo Label Color By Value","Metroid.UI.SectionHudAmmoTextRamp",      kSecAmmoTextRamp),
    SUB("Ammo Outline",             "Metroid.UI.SectionHudWeaponOutline",      kSecWeaponOutline),
    SUB("Ammo Gauge",               "Metroid.UI.SectionHudAmmoGauge",          kSecAmmoGauge),
    SUB("Ammo Gauge Color By Value","Metroid.UI.SectionHudAmmoGaugeRamp",      kSecAmmoGaugeRamp),
    SUB("Ammo Gauge Outline",       "Metroid.UI.SectionHudAmmoGaugeOutline",   kSecAmmoGaugeOutline),
};

// Weapon Icon sub-sections
static const HudSubSec kSubsWpnIcon[] = {
    SUB("Weapon Icon",               "Metroid.UI.SectionHudWpnIcon",              kSecWpnIcon),
    SUB("Weapon Icon Outline",       "Metroid.UI.SectionHudWeaponIconOutline",    kSecWeaponIconOutline),
    SUB("Weapon Icon Color Overlay", "Metroid.UI.SectionHudWpnIconTints",         kSecWpnIconTints),
};

// Weapon Inventory sub-sections
static const HudSubSec kSubsWpnInventory[] = {
    SUB("Weapon Inventory",              "Metroid.UI.SectionHudWeaponInventory",              kSecWeaponInventory),
    SUB("Weapon Inventory Highlight",    "Metroid.UI.SectionHudWeaponInventoryHighlight",     kSecWeaponInventoryHighlight),
    SUB("Weapon Inventory Outline",      "Metroid.UI.SectionHudWeaponInventoryOutline",       kSecWeaponInventoryOutline),
    SUB("Weapon Inventory Icon Outline", "Metroid.UI.SectionHudWeaponInventoryIconOutline",   kSecWeaponInventoryIconOutline),
};

// HP / AMMO sub-sections
static const HudSubSec kSubsHpAmmo[] = {
    SUB_NEST("HP",               "Metroid.UI.SectionHudHpGrp",       kSubsHp),
    SUB_NEST("Ammo",             "Metroid.UI.SectionHudAmmoGrp",     kSubsAmmo),
    SUB_NEST("Weapon Icon",      "Metroid.UI.SectionHudWpnIconGrp",  kSubsWpnIcon),
    SUB_NEST("Weapon Inventory", "Metroid.UI.SectionHudWpnInvGrp",   kSubsWpnInventory),
};

// Rank/Time sub-sub-sections
static const HudSubSec kSubsRankTime[] = {
    SUB("Rank",            "Metroid.UI.SectionHudRank",          kSecRank),
    SUB("Time Left",       "Metroid.UI.SectionHudTimeLeft",       kSecTimeLeft),
    SUB("Time Limit",      "Metroid.UI.SectionHudTimeLimit",      kSecTimeLimit),
    SUB("Rank Outline",       "Metroid.UI.SectionHudRankOutline",      kSecRankOutline),
    SUB("Time Left Outline",  "Metroid.UI.SectionHudTimeLeftOutline",  kSecTimeLeftOutline),
    SUB("Time Limit Outline", "Metroid.UI.SectionHudTimeLimitOutline", kSecTimeLimitOutline),
};

// Bomb sub-sections
static const HudSubSec kSubsBomb[] = {
    SUB("Bomb Left",         "Metroid.UI.SectionHudBombLeft",        kSecBombLeft),
    SUB("Bomb Left Outline", "Metroid.UI.SectionHudBombLeftOutline", kSecBombLeftOutline),
    SUB("Bomb Icon",         "Metroid.UI.SectionHudBombIcon",        kSecBombIcon),
    SUB("Bomb Icon Outline", "Metroid.UI.SectionHudBombIconOutline", kSecBombIconOutline),
};

// MATCH STATUS HUD sub-sections
static const HudSubSec kSubsMatchStatus[] = {
    SUB("Score",                    "Metroid.UI.SectionHudMatchStatus",       kSecMatchStatus),
    SUB("Score Outline",            "Metroid.UI.SectionHudMatchStatusOutline", kSecMatchStatusOutline),
    SUB_NEST("Rank / Time",         "Metroid.UI.SectionHudRankTime",          kSubsRankTime),
    SUB_NEST("Bomb",                "Metroid.UI.SectionHudBombGrp",           kSubsBomb),
};

// HUD RADAR sub-sections
static const HudSubSec kSubsRadar[] = {
    SUB("Radar Settings",       "Metroid.UI.SectionHudRadarSettings",      kSecRadar),
    SUB("Radar Outline",        "Metroid.UI.SectionHudRadarOutline",       kSecRadarOutline),
    SUB("Frame Outline",        "Metroid.UI.SectionHudRadarFrameOutline",  kSecRadarFrameOutline),
};

// IN-GAME OSD COLOR sub-sections
#define MP_OSD_COLOR_EMIT_LITERAL_SUBSECTION(id, dialogLabel, sectionKey, propArray) \
    SUB(dialogLabel, sectionKey, propArray),
#define MP_OSD_COLOR_EMIT_SLOT_SUBSECTION(id, dialogLabel, sectionKey, propArray, keyPrefix, description, colorLabel) \
    SUB(dialogLabel, sectionKey, propArray),
static const HudSubSec kSubsOsdColor[] = {
    SUB("Node Stolen (H211)",  "Metroid.UI.SectionOsdH211",         kSecOsdH211),
    MP_OSD_COLOR_LITERAL_SUBSECTIONS(MP_OSD_COLOR_EMIT_LITERAL_SUBSECTION)
    // Slot override colors (flags-based, effective when "Use Global Color for All" is OFF)
    MP_OSD_COLOR_SLOT_ROWS(MP_OSD_COLOR_EMIT_SLOT_SUBSECTION)
};
#undef MP_OSD_COLOR_EMIT_LITERAL_SUBSECTION
#undef MP_OSD_COLOR_EMIT_SLOT_SUBSECTION

// Main section groups
static const HudMainSec kHudMainSections[] = {
    { "DISABLE DEFAULT HUD", "Metroid.UI.SectionDisableDefaultHud",
      _P(kSecDisableDefaultHud), nullptr, 0, /*preview*/ 0 },
    { "OUTLINE OVERRIDE",  "Metroid.UI.SectionHudGlobalOutline",
      _P(kSecGlobalOutline), nullptr, 0, /*preview*/ 0 },
        { "HUD SCALE",         "Metroid.UI.SectionHudTextScale",
      _P(kSecTextScale), nullptr, 0, /*preview*/ 0 },
    { "HUD FONT",          "Metroid.UI.SectionHudFont",
      _P(kSecFont), nullptr, 0, /*preview*/ 0 },
    { "CROSSHAIR",         "Metroid.UI.SectionHudCrosshair",
      _P(kSecCrosshair), kSubsCrosshair, static_cast<int>(sizeof(kSubsCrosshair)/sizeof(kSubsCrosshair[0])), /*preview*/ 1 },
    { "HP / AMMO",         "Metroid.UI.SectionHudHpAmmo",
      nullptr, 0, kSubsHpAmmo, static_cast<int>(sizeof(kSubsHpAmmo)/sizeof(kSubsHpAmmo[0])), /*preview*/ 2 },
    { "MATCH STATUS HUD",  "Metroid.UI.SectionHudMatchStatusGrp",
      nullptr, 0, kSubsMatchStatus, static_cast<int>(sizeof(kSubsMatchStatus)/sizeof(kSubsMatchStatus[0])), /*preview*/ 3 },
    { "HUD RADAR",         "Metroid.UI.SectionHudRadar",
      nullptr, 0, kSubsRadar, static_cast<int>(sizeof(kSubsRadar)/sizeof(kSubsRadar[0])), /*preview*/ 4 },
    { "IN-GAME OSD COLOR", "Metroid.UI.SectionOsdColor",
      _P(kSecOsdGlobal), kSubsOsdColor, static_cast<int>(sizeof(kSubsOsdColor)/sizeof(kSubsOsdColor[0])), /*preview*/ 0 },
};
static constexpr int kHudMainSectionCount = static_cast<int>(sizeof(kHudMainSections) / sizeof(kHudMainSections[0]));

static const char* kAnchorLabels[] = {
    "Top Left", "Top Center", "Top Right",
    "Middle Left", "Middle Center", "Middle Right",
    "Bottom Left", "Bottom Center", "Bottom Right",
};

static const char* kAlignLabels[] = { "Left", "Center", "Right" };

static QString cfgKeyToObjName(const char* key)
{
    QString s = QString::fromUtf8(key);
    s.replace('.', '_');
    return s;
}
