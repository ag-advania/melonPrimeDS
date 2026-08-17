// Generator-only dialog source surface for the match scoreboard.
// This file is parsed by generate-hud-prop-schema.py and is not compiled.
static const HudWidgetProp kSecScoreboard[] = {
    {"Show", HWType::Bool, MP_HUD_PROP_KEY_HudScoreboardShow, 0, 0, 0, nullptr, nullptr},
    {"Anchor", HWType::Anchor9, MP_HUD_PROP_KEY_HudScoreboardAnchor, 0, 8, 1, nullptr, nullptr},
    {"Offset X", HWType::Int, MP_HUD_PROP_KEY_HudScoreboardX, -256, 256, 1, nullptr, nullptr},
    {"Offset Y", HWType::Int, MP_HUD_PROP_KEY_HudScoreboardY, -256, 256, 1, nullptr, nullptr},
    {"Column Spacing", HWType::Int, MP_HUD_PROP_KEY_HudScoreboardColumnSpacing, 0, 32, 1, nullptr, nullptr},
    {"Right Padding", HWType::Int, MP_HUD_PROP_KEY_HudScoreboardRightPadding, 0, 32, 1, nullptr, nullptr},
    {"Scoreboard Scale %", HWType::Int, MP_HUD_PROP_KEY_HudScoreboardScale, 10, 800, 1, nullptr, nullptr},
    {"Row Spacing", HWType::Int, MP_HUD_PROP_KEY_HudScoreboardRowSpacing, 0, 16, 1, nullptr, nullptr},
    {"Hunter Icon Height", HWType::Int, MP_HUD_PROP_KEY_HudScoreboardHunterIconHeight, 0, 48, 1, nullptr, nullptr},
    {"Hunter Icon Background Color", HWType::Color3, MP_HUD_PROP_KEY_HudScoreboardHunterIconBackgroundColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardHunterIconBackgroundColorG, MP_HUD_PROP_KEY_HudScoreboardHunterIconBackgroundColorB},
    {"Stars Height", HWType::Int, MP_HUD_PROP_KEY_HudScoreboardStarsHeight, 0, 24, 1, nullptr, nullptr},
    {"Opacity", HWType::Opacity, MP_HUD_PROP_KEY_HudScoreboardOpacity, 0, 100, 5, nullptr, nullptr},
    {"Background Opacity", HWType::Opacity, MP_HUD_PROP_KEY_HudScoreboardBackgroundOpacity, 0, 100, 5, nullptr, nullptr},
    {"Text Scale %", HWType::Int, MP_HUD_PROP_KEY_HudScoreboardTextScale, 10, 800, 1, nullptr, nullptr},
    {"Rank", HWType::Bool, MP_HUD_PROP_KEY_HudScoreboardRankShow, 0, 0, 0, nullptr, nullptr},
    {"Outline", HWType::Bool, MP_HUD_PROP_KEY_HudScoreboardOutline, 0, 0, 0, nullptr, nullptr},
    {"Outline Color", HWType::Color3, MP_HUD_PROP_KEY_HudScoreboardOutlineColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardOutlineColorG, MP_HUD_PROP_KEY_HudScoreboardOutlineColorB},
    {"Outline Opacity", HWType::Opacity, MP_HUD_PROP_KEY_HudScoreboardOutlineOpacity, 0, 100, 5, nullptr, nullptr},
    {"Outline Thickness", HWType::Int, MP_HUD_PROP_KEY_HudScoreboardOutlineThickness, 1, 10, 1, nullptr, nullptr},
    {"Text Color", HWType::Color3, MP_HUD_PROP_KEY_HudScoreboardTextColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardTextColorG, MP_HUD_PROP_KEY_HudScoreboardTextColorB},
    {"Header Color", HWType::Color3, MP_HUD_PROP_KEY_HudScoreboardHeaderColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardHeaderColorG, MP_HUD_PROP_KEY_HudScoreboardHeaderColorB},
    {"Local Player Color", HWType::Color3, MP_HUD_PROP_KEY_HudScoreboardLocalPlayerColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardLocalPlayerColorG, MP_HUD_PROP_KEY_HudScoreboardLocalPlayerColorB},
    {"Team Row Color", HWType::Color3, MP_HUD_PROP_KEY_HudScoreboardTeamRowColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardTeamRowColorG, MP_HUD_PROP_KEY_HudScoreboardTeamRowColorB},
    {"Background Color", HWType::Color3, MP_HUD_PROP_KEY_HudScoreboardBackgroundColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudScoreboardBackgroundColorG, MP_HUD_PROP_KEY_HudScoreboardBackgroundColorB},
};

// Extend the generated HUD SCALE section with the scoreboard-specific cap.
// The generator merges duplicate section names by config key before it writes
// MelonPrimeInputConfigHudDialogProps.inc, so a row can change widget type
// without leaving a stale generated duplicate behind.
static const HudWidgetProp kSecTextScale[] = {
    {"Auto Scale Scoreboard Cap %", HWType::Int, MP_HUD_PROP_KEY_HudAutoScaleCapScoreboard,
        100, 800, 25, nullptr, nullptr},
};
