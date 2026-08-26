// This file is a unity-build fragment included by
// MelonPrimeHudRenderRuntime.inc.  Do not add it to CMakeLists.txt.

// =========================================================================
//  Config and visual generation state
// =========================================================================

// P-3: Called from settings dialog save to trigger config re-read next frame
static void CustomHud_InvalidateConfigCache()
{
    s_cache.valid = false;
    s_bombTintCacheValid = false;
    for (int i = 0; i < 9; i++) s_weaponTintValid[i] = false;
    s_effectiveRadarHunterID = 0xFF; // P-13: force re-evaluation of effective radar color
    // Force EnsureHudFont() to re-read the painter font next frame; Screen.cpp rebuilds
    // overlayFont from CustomHud_ResolveBaseFont when the HUD-font setting changes, and the
    // rebuild bumps s_textCacheGen so stale glyph bitmaps are re-rasterised.
    s_frameFpx = 0;
    // Bump epoch immediately so Screen.cpp's m_hudEnabled cache refreshes on the
    // next frame. Without this, when CustomHUD is toggled off, the stale cache
    // keeps Render() running for one extra frame and re-applies NoHudPatch.
    if (++s_cacheEpoch == 0) s_cacheEpoch = 1;
}

void CustomHud_InvalidateConfigCache(CustomHudConfigState& hudConfig)
{
    const ScopedHudConfigState active(hudConfig);
    CustomHud_InvalidateConfigCache();
}

uint32_t CustomHud_GetCacheEpoch(const CustomHudConfigState& hudConfig)
{
    return hudConfig.cacheEpoch;
}

uint32_t CustomHud_GetVisualGeneration(const CustomHudConfigState& hudConfig)
{
    return hudConfig.visualGeneration;
}

uint32_t CustomHud_GetVisualGameFrame(EmuInstance* emu,
                                      const void** ndsIdentity)
{
    melonDS::NDS* nds = emu ? emu->getNDS() : nullptr;
    if (ndsIdentity)
        *ndsIdentity = nds;
    return nds ? nds->NumFrames : 0;
}
