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

// Generator-only dialog source surface for the Enemy Target HUD.
// This file is parsed by generate-hud-prop-schema.py and is not compiled.
static const HudWidgetProp kSecEnemyTarget[] = {
    {"Requires Show Enemy HP Meter Online, under GAME FEATURE IMPROVEMENTS. Metroid Prime Hunters does not produce the last-hit target information on Nintendo WFC / Wiimmfi unless that fix is enabled, so these settings stay disabled until you turn it on.", HWType::Label, "", 0, 0, 0, nullptr, nullptr},
    {"Show", HWType::Bool, MP_HUD_PROP_KEY_HudEnemyTargetShow, 0, 0, 0, nullptr, nullptr},
    {"Anchor", HWType::Anchor9, MP_HUD_PROP_KEY_HudEnemyTargetAnchor, 0, 8, 1, nullptr, nullptr},
    {"Offset X", HWType::Int, MP_HUD_PROP_KEY_HudEnemyTargetX, -256, 256, 1, nullptr, nullptr},
    {"Offset Y", HWType::Int, MP_HUD_PROP_KEY_HudEnemyTargetY, -256, 256, 1, nullptr, nullptr},
    {"Scale %", HWType::Int, MP_HUD_PROP_KEY_HudEnemyTargetScale, 10, 800, 1, nullptr, nullptr},
    {"Text Scale %", HWType::Int, MP_HUD_PROP_KEY_HudEnemyTargetTextScale, 10, 800, 1, nullptr, nullptr},
    {"Row Spacing", HWType::Int, MP_HUD_PROP_KEY_HudEnemyTargetRowSpacing, 0, 16, 1, nullptr, nullptr},
    {"Opacity", HWType::Opacity, MP_HUD_PROP_KEY_HudEnemyTargetOpacity, 0, 100, 5, nullptr, nullptr},
    {"Name", HWType::Bool, MP_HUD_PROP_KEY_HudEnemyTargetNameShow, 0, 0, 0, nullptr, nullptr},
    {"Name Color", HWType::Color3, MP_HUD_PROP_KEY_HudEnemyTargetColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudEnemyTargetColorG, MP_HUD_PROP_KEY_HudEnemyTargetColorB},
    {"Team Color", HWType::Bool, MP_HUD_PROP_KEY_HudEnemyTargetTeamColor, 0, 0, 0, nullptr, nullptr},
    {"Hunter Icon", HWType::Bool, MP_HUD_PROP_KEY_HudEnemyTargetHunterIconShow, 0, 0, 0, nullptr, nullptr},
    {"Hunter Icon Height", HWType::Int, MP_HUD_PROP_KEY_HudEnemyTargetHunterIconHeight, 0, 48, 1, nullptr, nullptr},
    {"HP Mode", HWType::EnemyTargetHpMode, MP_HUD_PROP_KEY_HudEnemyTargetHpMode, 0, 3, 1, nullptr, nullptr},
    {"HP Number", HWType::Bool, MP_HUD_PROP_KEY_HudEnemyTargetHpNumberShow, 0, 0, 0, nullptr, nullptr},
    {"Bar Length", HWType::Int, MP_HUD_PROP_KEY_HudEnemyTargetBarLength, 8, 192, 1, nullptr, nullptr},
    {"Bar Height", HWType::Int, MP_HUD_PROP_KEY_HudEnemyTargetBarHeight, 1, 20, 1, nullptr, nullptr},
    {"HP Color", HWType::Color3, MP_HUD_PROP_KEY_HudEnemyTargetHpColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudEnemyTargetHpColorG, MP_HUD_PROP_KEY_HudEnemyTargetHpColorB},
    {"HP Back Color", HWType::Color3, MP_HUD_PROP_KEY_HudEnemyTargetHpBackColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudEnemyTargetHpBackColorG, MP_HUD_PROP_KEY_HudEnemyTargetHpBackColorB},
    {"Score", HWType::Bool, MP_HUD_PROP_KEY_HudEnemyTargetScoreShow, 0, 0, 0, nullptr, nullptr},
    {"Score Color", HWType::Color3, MP_HUD_PROP_KEY_HudEnemyTargetScoreColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudEnemyTargetScoreColorG, MP_HUD_PROP_KEY_HudEnemyTargetScoreColorB},
    {"Outline", HWType::Bool, MP_HUD_PROP_KEY_HudEnemyTargetOutline, 0, 0, 0, nullptr, nullptr},
    {"Outline Color", HWType::Color3, MP_HUD_PROP_KEY_HudEnemyTargetOutlineColorR, 0, 255, 1,
        MP_HUD_PROP_KEY_HudEnemyTargetOutlineColorG, MP_HUD_PROP_KEY_HudEnemyTargetOutlineColorB},
    {"Outline Opacity", HWType::Opacity, MP_HUD_PROP_KEY_HudEnemyTargetOutlineOpacity, 0, 100, 5, nullptr, nullptr},
    {"Outline Thickness", HWType::Int, MP_HUD_PROP_KEY_HudEnemyTargetOutlineThickness, 1, 10, 1, nullptr, nullptr},
};

// Extend the generated HUD SCALE section with the scoreboard-specific cap.
// The generator merges duplicate section names by config key before it writes
// MelonPrimeInputConfigHudDialogProps.inc, so a row can change widget type
// without leaving a stale generated duplicate behind.
static const HudWidgetProp kSecTextScale[] = {
    {"Auto Scale Scoreboard Cap %", HWType::Int, MP_HUD_PROP_KEY_HudAutoScaleCapScoreboard,
        100, 800, 25, nullptr, nullptr},
};
