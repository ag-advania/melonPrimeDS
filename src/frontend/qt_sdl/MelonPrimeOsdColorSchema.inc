// This file is a unity-build fragment included by MelonPrimeInputConfigHudDialogProps.inc
// and MelonPrimePatchOsdColor.cpp. Do not add it to CMakeLists.txt.

#ifndef MELONPRIME_OSD_COLOR_SCHEMA_INC
#define MELONPRIME_OSD_COLOR_SCHEMA_INC

// X(id, dialogLabel, keyPrefix, literalList, restoreValue)
#define MP_OSD_COLOR_LITERAL_ROWS(X) \
    X(LostLives,     "Lost Lives",     OsdColorLostLives,     OsdLiteral_LostLives,     kOsdOrigLit) \
    X(KillDeath,     "Kill / Death",   OsdColorKillDeath,     OsdLiteral_KillDeath,     kOsdOrigLit) \
    X(ReturnBase,    "Return to Base", OsdColorReturnBase,    OsdLiteral_ReturnBase,    kOsdOrigLit) \
    X(NoAmmo,        "No Ammo",        OsdColorNoAmmo,        OsdLiteral_NoAmmo,        kOsdOrigLitNoAmmo) \
    X(CowardDetect,  "Coward Detect",  OsdColorCowardDetect,  OsdLiteral_CowardDetect,  kOsdOrigLit) \
    X(AcquiringNode, "Acquiring Node", OsdColorAcquiringNode, OsdLiteral_AcquiringNode, kOsdOrigLit) \
    X(Turret,        "Turret",         OsdColorTurret,        OsdLiteral_Turret,        kOsdOrigLit) \
    X(OctoReset,     "Octo Reset",     OsdColorOctoReset,     OsdLiteral_OctoReset,     kOsdOrigLit) \
    X(OctoDrop,      "Octo Drop",      OsdColorOctoDrop,      OsdLiteral_OctoDrop,      kOsdOrigLit) \
    X(OctoCond,      "Octo Condition", OsdColorOctoCond,      OsdLiteral_OctoCond,      kOsdOrigLit) \
    X(OctoMissing,   "Octo Missing",   OsdColorOctoMissing,   OsdLiteral_OctoMissing,   kOsdOrigLit)

// X(id, dialogLabel, sectionKey, propArray)
#define MP_OSD_COLOR_LITERAL_SUBSECTIONS(X) \
    X(LostLives,     "Lost Lives",     "Metroid.UI.SectionOsdLostLives",     kSecOsdLostLives) \
    X(KillDeath,     "Kill / Death",   "Metroid.UI.SectionOsdKillDeath",     kSecOsdKillDeath) \
    X(ReturnBase,    "Return to Base", "Metroid.UI.SectionOsdReturnBase",    kSecOsdReturnBase) \
    X(NoAmmo,        "No Ammo",        "Metroid.UI.SectionOsdNoAmmo",        kSecOsdNoAmmo) \
    X(CowardDetect,  "Coward Detect",  "Metroid.UI.SectionOsdCowardDetect",  kSecOsdCowardDetect) \
    X(AcquiringNode, "Acquiring Node", "Metroid.UI.SectionOsdAcquiringNode", kSecOsdAcquiringNode) \
    X(Turret,        "Turret",         "Metroid.UI.SectionOsdTurret",        kSecOsdTurret) \
    X(OctoReset,     "Octo Reset",     "Metroid.UI.SectionOsdOctoReset",     kSecOsdOctoReset) \
    X(OctoDrop,      "Octo Drop",      "Metroid.UI.SectionOsdOctoDrop",      kSecOsdOctoDrop) \
    X(OctoCond,      "Octo Condition", "Metroid.UI.SectionOsdOctoCond",      kSecOsdOctoCond) \
    X(OctoMissing,   "Octo Missing",   "Metroid.UI.SectionOsdOctoMissing",   kSecOsdOctoMissing)

// X(id, dialogLabel, sectionKey, propArray, keyPrefix, description, colorLabel)
#define MP_OSD_COLOR_SLOT_ROWS(X) \
    X(KillDeath, "Slot: Kill / Death  [flags=0x02]", "Metroid.UI.SectionOsdSlotKillDeath", kSecOsdSlotKillDeath, OsdColorSlotKillDeath, "Applied once on settings close to currently displayed messages (flags=0x02).\nNew messages use the 'Kill / Death' literal color above.", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)") \
    X(Node, "Slot: Node Capture  [flags=0x11]", "Metroid.UI.SectionOsdSlotNode", kSecOsdSlotNode, OsdColorSlotNode, "Applied once on settings close to currently displayed messages (flags=0x11).\nNew messages use 'Acquiring Node' or 'Node Stolen' literal colors above.", "Color  (acquiring node / node stolen H211)") \
    X(Objective, "Slot: Objective     [flags=0x01]", "Metroid.UI.SectionOsdSlotObjective", kSecOsdSlotObjective, OsdColorSlotObjective, "Applied once on settings close to currently displayed messages (flags=0x01).\nNew messages use their individual literal colors above (No Ammo / Return to Base / Octo ...).", "Color  (AMMO DEPLETED / return to base / bounty / octolith events)") \
    X(System, "Slot: System / Misc [flags=0x00]", "Metroid.UI.SectionOsdSlotSystem", kSecOsdSlotSystem, OsdColorSlotSystem, "Applied once on settings close to currently displayed messages (flags=0x00).\nNew messages use their individual literal colors above (Lost Lives / Coward Detect / Turret ...).\nNote: HEADSHOT! (H228) is flags=0x00, not 0x02.", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)")

#define MP_OSD_COLOR_KEY_R(prefix) MP_HUD_PROP_KEY_##prefix##R
#define MP_OSD_COLOR_KEY_G(prefix) MP_HUD_PROP_KEY_##prefix##G
#define MP_OSD_COLOR_KEY_B(prefix) MP_HUD_PROP_KEY_##prefix##B

#ifdef MELONPRIME_OSD_COLOR_EMIT_DIALOG_PROPS

static const HudWidgetProp kSecOsdH211[] = {
    { "Enable Separate Color", HWType::Bool, MP_HUD_PROP_KEY_OsdColorH211, 0, 0, 0, nullptr, nullptr },
    { "Color (Default: Red)", HWType::Color3, MP_HUD_PROP_KEY_OsdColorH211R, 0, 255, 1, MP_HUD_PROP_KEY_OsdColorH211G, MP_HUD_PROP_KEY_OsdColorH211B },
};

#define MP_OSD_COLOR_EMIT_LITERAL_DIALOG_SECTION(id, dialogLabel, keyPrefix, literalList, restoreValue) \
static const HudWidgetProp kSecOsd##id[] = { \
    { "Color", HWType::Color3, MP_OSD_COLOR_KEY_R(keyPrefix), 0, 255, 1, MP_OSD_COLOR_KEY_G(keyPrefix), MP_OSD_COLOR_KEY_B(keyPrefix) }, \
};
MP_OSD_COLOR_LITERAL_ROWS(MP_OSD_COLOR_EMIT_LITERAL_DIALOG_SECTION)
#undef MP_OSD_COLOR_EMIT_LITERAL_DIALOG_SECTION

static const HudWidgetProp kSecOsdGlobal[] = {
    { "Enable OSD Color Patch", HWType::Bool, MP_HUD_PROP_KEY_OsdColor, 0, 0, 0, nullptr, nullptr },
    { "Global Color", HWType::Color3, MP_HUD_PROP_KEY_OsdColorR, 0, 255, 1, MP_HUD_PROP_KEY_OsdColorG, MP_HUD_PROP_KEY_OsdColorB },
    { "Use Global Color for All", HWType::Bool, MP_HUD_PROP_KEY_OsdColorApplyGlobal, 0, 0, 0, nullptr, nullptr },
};

#define MP_OSD_COLOR_EMIT_SLOT_DIALOG_SECTION(id, dialogLabel, sectionKey, propArray, keyPrefix, description, colorLabel) \
static const HudWidgetProp propArray[] = { \
    { description, HWType::Label, "", 0, 0, 0, nullptr, nullptr }, \
    { colorLabel, HWType::Color3, MP_OSD_COLOR_KEY_R(keyPrefix), 0, 255, 1, MP_OSD_COLOR_KEY_G(keyPrefix), MP_OSD_COLOR_KEY_B(keyPrefix) }, \
};
MP_OSD_COLOR_SLOT_ROWS(MP_OSD_COLOR_EMIT_SLOT_DIALOG_SECTION)
#undef MP_OSD_COLOR_EMIT_SLOT_DIALOG_SECTION

#endif // MELONPRIME_OSD_COLOR_EMIT_DIALOG_PROPS

#endif // MELONPRIME_OSD_COLOR_SCHEMA_INC
