// This file is a unity-build fragment included by
// MelonPrimeHudRenderRuntime.inc.  Do not add it to CMakeLists.txt.

// =========================================================================
//  Custom HUD enablement and visibility policy
// =========================================================================

// =========================================================================
//  Config key (only for IsEnabled — hot path uses s_cache)
// =========================================================================
static constexpr const char* kCfgCustomHud = MP_HUD_PROP_KEY_CustomHUD;
bool CustomHud_IsEnabled(Config::Table& localCfg)
{
    // Dormant diagnostic override used by the real-ROM raster differential.
    // Cache the process environment once so the normal per-frame HUD query
    // remains a single predictable branch plus the existing config lookup.
    static const bool diagnosticForcedOff = [] {
        const char* value = std::getenv("MELONPRIME_TEST_CUSTOM_HUD_OFF");
        return value && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    if (diagnosticForcedOff)
        return false;
    return localCfg.GetBool(kCfgCustomHud);
}

// States in which the game shows its own UI over the gameplay view. Custom HUD
// hides, and the native helmet layers are handed back (see the NOHUD_HELMET
// strip at the call sites).
static inline bool ShouldHideForGameplayState(bool isStartPressed,
                                             uint16_t currentHP,
                                             bool isGameOver,
                                             bool isAdventure,
                                             bool isMapOrUserActionPaused)
{
    return isStartPressed ||
           (isAdventure && isMapOrUserActionPaused) ||
           currentHP == 0 ||
           isGameOver;
}

// Single decision point for the Custom HUD crosshair. The whole-overlay hide
// rules (native UI, adventure camera scenes) return before this is consulted;
// what remains are the states where the rest of the HUD keeps drawing but the
// game has no reticle of its own: third person, alt form, morph transition, and
// an active Scan Visor.
static inline bool ShouldDrawCustomHudCrosshair(const HudRuntimeState& st)
{
    return st.isFirstPerson && !st.isTrans && !st.isAlt && !st.isAdventureScanVisor;
}

bool CustomHud_ShouldHideForGameplayState(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition)
{
    if (!emu) return true;

    HudRuntimeState st = {};
    if (!ReadHudRuntimeBaseState(emu, rom, playerPosition, st))
        return true;

    if (HudRuntimeFrameCacheMatches(st)
        && s_hudRuntimeFrameCache.gameplayVisibilityValid)
        return !s_hudRuntimeFrameCache.gameplayVisible;

    ReadHudRuntimeAdventurePauseState(rom, st);
    const bool visible =
        !ShouldHideForGameplayState(
            st.isStartPressed,
            st.currentHP,
            st.isGameOver,
            st.isAdventure,
            st.isMapOrUserActionPaused)
        && !st.isAdventureCameraScene;

    if (HudRuntimeFrameCacheMatches(st))
    {
        s_hudRuntimeFrameCache.gameplayVisible = visible;
        s_hudRuntimeFrameCache.gameplayVisibilityValid = true;
    }
    return !visible;
}

bool CustomHud_ShouldDrawRadarOverlay(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition)
{
    return !CustomHud_ShouldHideForGameplayState(emu, rom, playerPosition);
}
