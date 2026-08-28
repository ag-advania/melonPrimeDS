#ifndef MELON_PRIME_HUD_RENDER_H
#define MELON_PRIME_HUD_RENDER_H

#ifdef MELONPRIME_CUSTOM_HUD

// =========================================================================
//  Custom HUD render entry points.
//
//  Drawing and the font resolution that drawing needs -- nothing else.  The
//  neighbouring responsibilities live in their own headers so a consumer
//  only takes on what it actually calls:
//
//    MelonPrimeHudConfigState.h    per-instance state, config cache epoch
//    MelonPrimeHudPresentationState.h lightweight host presentation queries
//    MelonPrimeHudRuntime.h        gameplay visibility, visual generation
//    MelonPrimeHudRadar.h          radar colour-key source preparation
//    MelonPrimeHudPatchLifecycle.h native HUD patch apply/restore/reset
//    MelonPrimeHudEdit.h           on-screen layout editor
//    MelonPrimeHudGoldenHarness.h  developer-only golden hash harness
//
//  All of them are implemented by the same MelonPrimeHudRender.cpp unity
//  translation unit; this split is an API boundary, not a link boundary.
// =========================================================================

#include <cstdint>
#include <QRect>

#include "MelonPrimeHudConfigState.h"

class EmuInstance;
class QPainter;
class QImage;
class QFont;

namespace MelonPrime {

    // =========================================================================
    //  CustomHud_Render
    //
    //  Main entry point — call once per frame from the OSD/overlay render path.
    //  Draws HP, weapon icon + ammo count, and crosshair onto the overlay.
    //
    //  Parameters:
    //    emu            — EmuInstance for NDS memory access
    //    localCfg       — config table for cold/cache refresh and drawing helpers
    //    rom            — ROM address table (resolved for current ROM)
    //    addrHot        — player-position-adjusted hot addresses
    //    playerPosition — current player position index (for +0xF30 offset)
    //    topPaint       — QPainter for the top-screen overlay
    //    btmPaint       — QPainter for the bottom-screen overlay
    //    topBuffer      — QImage backing the top overlay (cleared inside)
    //    btmBuffer      — QImage backing the bottom overlay (cleared inside)
    //    isInGame      — whether the game is currently in a match
    //    hudEnabledSnapshot — epoch-coherent HUD enabled decision from Screen.cpp;
    //                         never re-read the live config in this function
    //    topStretchX    — widescreen X stretch factor (1.0=4:3, >1.0=wide)
    //    hudOriginXds   — left black-bar width in DS units (m_hudOriginX / hudScale).
    //                     Non-zero when game content is pillarboxed inside the window.
    //                     Shifts the painter so DS x=0 maps to the left game edge,
    //                     allowing elements at DS x<0 or x>256 to appear in black bars.
    //    hudOriginYds   — top black-bar height in DS units (m_hudOriginY / hudScale).
    // =========================================================================
    // Returns the dirty pixel rect of everything rendered into the overlay (in overlay space).
    // Screen.cpp uses this to limit the GPU texture upload and overlay clear to the HUD region.
    // Returns an empty QRect if nothing was drawn.
    QRect CustomHud_Render(
        CustomHudConfigState& hudConfig,
        EmuInstance* emu,
        Config::Table& localCfg,
        const RomAddresses& rom,
        const GameAddressesHot& addrHot,
        uint8_t playerPosition,
        QPainter* topPaint,
        QPainter* btmPaint,
        QImage* topBuffer,
        QImage* btmBuffer,
        bool isInGame,
        bool hudEnabledSnapshot,
        float topStretchX = 1.0f,
        float hudScale = 1.0f,
        float hudOriginXds = 0.0f,
        float hudOriginYds = 0.0f
    );

    // Resolve the base HUD font (family + style strategy only; caller sets pixel size)
    // from the Metroid.Visual.HudFont* config keys:
    //   HudFontMode 0 = bundled MPH (default), 1 = system font family, 2 = font file.
    // Falls back to the bundled MPH font on empty/invalid selection.
    QFont CustomHud_ResolveBaseFont(Config::Table& localCfg);

    // Resolve the base render pixel size for the HUD font:
    //   mode 0 (MPH)  -> kCustomHudFontSize (6, the pixel font's native size).
    //   mode 1/2      -> Metroid.Visual.HudFontSize (clamped), so system/file fonts can
    //                    be rasterised larger and sharper. HudTextScale/auto-scale still apply.
    int CustomHud_ResolveFontPixelSize(Config::Table& localCfg);

    // =========================================================================
    //  DrawBottomScreenOverlay
    //
    //  Render a region of the bottom DS screen onto the top-screen overlay.
    //  Controlled by Metroid.Visual.BtmOverlay* config keys.
    //
    //  Parameters:
    //    localCfg  — config table
    //    topPaint  — QPainter for the top-screen overlay
    //    btmBuffer — QImage of bottom screen (256x192 ARGB)
    //    hunterID  — current player character (MelonPrime::HunterId ordering)
    // =========================================================================
    void DrawBottomScreenOverlay(Config::Table& localCfg, QPainter* topPaint, QImage* btmBuffer, uint8_t hunterID);

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_RENDER_H
