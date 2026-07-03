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
    SUB("Zoom Crosshair", MelonPrime::CfgKey::SectionHudCrosshairZoom, kSecCrosshairZoom),
    SUB("Inner Lines",  MelonPrime::CfgKey::SectionHudCrosshairInner,  kSecCrosshairInner),
    SUB("Outer Lines",  MelonPrime::CfgKey::SectionHudCrosshairOuter,  kSecCrosshairOuter),
};

// HP sub-sections
static const HudSubSec kSubsHp[] = {
    SUB("HP Number Position",     MelonPrime::CfgKey::SectionHudHp,            kSecHp),
    SUB("HP Label Color By Value",MelonPrime::CfgKey::SectionHudHpTextRamp,    kSecHpTextRamp),
    SUB("HP Outline",             MelonPrime::CfgKey::SectionHudHpOutline,      kSecHpOutline),
    SUB("HP Gauge",               MelonPrime::CfgKey::SectionHudHpGauge,        kSecHpGauge),
    SUB("HP Gauge Color By Value",MelonPrime::CfgKey::SectionHudHpGaugeRamp,    kSecHpGaugeRamp),
    SUB("HP Gauge Outline",       MelonPrime::CfgKey::SectionHudHpGaugeOutline, kSecHpGaugeOutline),
};

// Ammo sub-sections
static const HudSubSec kSubsAmmo[] = {
    SUB("Ammo Number Position",     MelonPrime::CfgKey::SectionHudWeaponAmmo,        kSecWeaponAmmo),
    SUB("Ammo Label Color By Value",MelonPrime::CfgKey::SectionHudAmmoTextRamp,      kSecAmmoTextRamp),
    SUB("Ammo Outline",             MelonPrime::CfgKey::SectionHudWeaponOutline,      kSecWeaponOutline),
    SUB("Ammo Gauge",               MelonPrime::CfgKey::SectionHudAmmoGauge,          kSecAmmoGauge),
    SUB("Ammo Gauge Color By Value",MelonPrime::CfgKey::SectionHudAmmoGaugeRamp,      kSecAmmoGaugeRamp),
    SUB("Ammo Gauge Outline",       MelonPrime::CfgKey::SectionHudAmmoGaugeOutline,   kSecAmmoGaugeOutline),
};

// Weapon Icon sub-sections
static const HudSubSec kSubsWpnIcon[] = {
    SUB("Weapon Icon",               MelonPrime::CfgKey::SectionHudWpnIcon,              kSecWpnIcon),
    SUB("Weapon Icon Outline",       MelonPrime::CfgKey::SectionHudWeaponIconOutline,    kSecWeaponIconOutline),
    SUB("Weapon Icon Color Overlay", MelonPrime::CfgKey::SectionHudWpnIconTints,         kSecWpnIconTints),
};

// Weapon Inventory sub-sections
static const HudSubSec kSubsWpnInventory[] = {
    SUB("Weapon Inventory",              MelonPrime::CfgKey::SectionHudWeaponInventory,              kSecWeaponInventory),
    SUB("Weapon Inventory Highlight",    MelonPrime::CfgKey::SectionHudWeaponInventoryHighlight,     kSecWeaponInventoryHighlight),
    SUB("Weapon Inventory Outline",      MelonPrime::CfgKey::SectionHudWeaponInventoryOutline,       kSecWeaponInventoryOutline),
    SUB("Weapon Inventory Icon Outline", MelonPrime::CfgKey::SectionHudWeaponInventoryIconOutline,   kSecWeaponInventoryIconOutline),
};

// HP / AMMO sub-sections
static const HudSubSec kSubsHpAmmo[] = {
    SUB_NEST("HP",               MelonPrime::CfgKey::SectionHudHpGrp,       kSubsHp),
    SUB_NEST("Ammo",             MelonPrime::CfgKey::SectionHudAmmoGrp,     kSubsAmmo),
    SUB_NEST("Weapon Icon",      MelonPrime::CfgKey::SectionHudWpnIconGrp,  kSubsWpnIcon),
    SUB_NEST("Weapon Inventory", MelonPrime::CfgKey::SectionHudWpnInvGrp,   kSubsWpnInventory),
};

// Rank/Time sub-sub-sections
static const HudSubSec kSubsRankTime[] = {
    SUB("Rank",            MelonPrime::CfgKey::SectionHudRank,          kSecRank),
    SUB("Time Left",       MelonPrime::CfgKey::SectionHudTimeLeft,       kSecTimeLeft),
    SUB("Time Limit",      MelonPrime::CfgKey::SectionHudTimeLimit,      kSecTimeLimit),
    SUB("Rank Outline",       MelonPrime::CfgKey::SectionHudRankOutline,      kSecRankOutline),
    SUB("Time Left Outline",  MelonPrime::CfgKey::SectionHudTimeLeftOutline,  kSecTimeLeftOutline),
    SUB("Time Limit Outline", MelonPrime::CfgKey::SectionHudTimeLimitOutline, kSecTimeLimitOutline),
};

// Bomb sub-sections
static const HudSubSec kSubsBomb[] = {
    SUB("Bomb Left",         MelonPrime::CfgKey::SectionHudBombLeft,        kSecBombLeft),
    SUB("Bomb Left Outline", MelonPrime::CfgKey::SectionHudBombLeftOutline, kSecBombLeftOutline),
    SUB("Bomb Icon",         MelonPrime::CfgKey::SectionHudBombIcon,        kSecBombIcon),
    SUB("Bomb Icon Outline", MelonPrime::CfgKey::SectionHudBombIconOutline, kSecBombIconOutline),
};

// MATCH STATUS HUD sub-sections
static const HudSubSec kSubsMatchStatus[] = {
    SUB("Score",                    MelonPrime::CfgKey::SectionHudMatchStatus,       kSecMatchStatus),
    SUB("Score Outline",            MelonPrime::CfgKey::SectionHudMatchStatusOutline, kSecMatchStatusOutline),
    SUB_NEST("Rank / Time",         MelonPrime::CfgKey::SectionHudRankTime,          kSubsRankTime),
    SUB_NEST("Bomb",                MelonPrime::CfgKey::SectionHudBombGrp,           kSubsBomb),
};

// HUD RADAR sub-sections
static const HudSubSec kSubsRadar[] = {
    SUB("Radar Settings",       MelonPrime::CfgKey::SectionHudRadarSettings,      kSecRadar),
    SUB("Radar Outline",        MelonPrime::CfgKey::SectionHudRadarOutline,       kSecRadarOutline),
    SUB("Frame Outline",        MelonPrime::CfgKey::SectionHudRadarFrameOutline,  kSecRadarFrameOutline),
};

// IN-GAME OSD COLOR sub-sections
#define MP_OSD_COLOR_EMIT_LITERAL_SUBSECTION(id, dialogLabel, sectionKey, propArray) \
    SUB(dialogLabel, sectionKey, propArray),
#define MP_OSD_COLOR_EMIT_SLOT_SUBSECTION(id, dialogLabel, sectionKey, propArray, keyPrefix, description, colorLabel) \
    SUB(dialogLabel, sectionKey, propArray),
static const HudSubSec kSubsOsdColor[] = {
    SUB("Node Stolen (H211)",  MelonPrime::CfgKey::SectionOsdH211,         kSecOsdH211),
    MP_OSD_COLOR_LITERAL_SUBSECTIONS(MP_OSD_COLOR_EMIT_LITERAL_SUBSECTION)
    // Slot override colors (flags-based, effective when "Use Global Color for All" is OFF)
    MP_OSD_COLOR_SLOT_ROWS(MP_OSD_COLOR_EMIT_SLOT_SUBSECTION)
};
#undef MP_OSD_COLOR_EMIT_LITERAL_SUBSECTION
#undef MP_OSD_COLOR_EMIT_SLOT_SUBSECTION

// Main section groups
static const HudMainSec kHudMainSections[] = {
    { "DISABLE DEFAULT HUD", MelonPrime::CfgKey::SectionDisableDefaultHud,
      _P(kSecDisableDefaultHud), nullptr, 0, /*preview*/ 0 },
    { "OUTLINE OVERRIDE",  MelonPrime::CfgKey::SectionHudGlobalOutline,
      _P(kSecGlobalOutline), nullptr, 0, /*preview*/ 0 },
        { "HUD SCALE",         MelonPrime::CfgKey::SectionHudTextScale,
      _P(kSecTextScale), nullptr, 0, /*preview*/ 0 },
    { "HUD FONT",          MelonPrime::CfgKey::SectionHudFont,
      _P(kSecFont), nullptr, 0, /*preview*/ 0 },
    { "CROSSHAIR",         MelonPrime::CfgKey::SectionHudCrosshair,
      _P(kSecCrosshair), kSubsCrosshair, static_cast<int>(sizeof(kSubsCrosshair)/sizeof(kSubsCrosshair[0])), /*preview*/ 1 },
    { "HP / AMMO",         MelonPrime::CfgKey::SectionHudHpAmmo,
      nullptr, 0, kSubsHpAmmo, static_cast<int>(sizeof(kSubsHpAmmo)/sizeof(kSubsHpAmmo[0])), /*preview*/ 2 },
    { "MATCH STATUS HUD",  MelonPrime::CfgKey::SectionHudMatchStatusGrp,
      nullptr, 0, kSubsMatchStatus, static_cast<int>(sizeof(kSubsMatchStatus)/sizeof(kSubsMatchStatus[0])), /*preview*/ 3 },
    { "HUD RADAR",         MelonPrime::CfgKey::SectionHudRadar,
      nullptr, 0, kSubsRadar, static_cast<int>(sizeof(kSubsRadar)/sizeof(kSubsRadar[0])), /*preview*/ 4 },
    { "IN-GAME OSD COLOR", MelonPrime::CfgKey::SectionOsdColor,
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
