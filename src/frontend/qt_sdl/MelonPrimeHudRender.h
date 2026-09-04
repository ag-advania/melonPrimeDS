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
#include "MelonPrimeHudDirtyRegions.h"

class EmuInstance;
class QPainter;
class QImage;
class QFont;

namespace MelonPrime {

    class NativePaintPerf;

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
    //    overlayRetained    - true when `topBuffer` still holds exactly the pixels
    //                         this function last wrote into it. The overlay is
    //                         retained across presented frames and this call owns
    //                         clearing it, so a presenter must pass false whenever
    //                         it reallocated, refilled, or otherwise cannot vouch
    //                         for the buffer (resize, DPI change, renderer switch,
    //                         first frame on a panel).
    //    outDirty           - receives the overlay regions whose pixels changed.
    //                         This is what a backend uploads. Bounded and
    //                         allocation free; a presenter that only understands
    //                         one rectangle can use the return value instead.
    //    outContent         - receives the regions where the HUD currently has
    //                         pixels at all. This is what a backend composites,
    //                         and it is deliberately not the dirty set: the game
    //                         frame underneath is repainted every presentation,
    //                         so the whole HUD has to be blended back over it
    //                         even on a frame where nothing changed.
    // =========================================================================
    // Returns the bounding rectangle of `outDirty` (in overlay space), or an
    // empty QRect when no overlay pixel changed.
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
        float hudOriginYds = 0.0f,
        QPainter* directScoreboardPaint = nullptr,
        NativePaintPerf* nativePaintPerf = nullptr,
        bool overlayRetained = false,
        HudDirtyRegionSet* outDirty = nullptr,
        HudDirtyRegionSet* outContent = nullptr
    );

    // Redraw only the already planned scoreboard into a presentation target.
    // This is used by the native Software presenter when the game frame is
    // repainted but the immutable HUD visual frame is reused; it performs no
    // NDS/RAM reads and never changes the retained overlay dirty rectangle.
    QRect CustomHud_RenderCachedScoreboard(
        CustomHudConfigState& hudConfig,
        Config::Table& localCfg,
        QPainter* scoreboardPaint,
        float topStretchX = 1.0f,
        float hudScale = 1.0f,
        float hudOriginXds = 0.0f,
        float hudOriginYds = 0.0f,
        NativePaintPerf* nativePaintPerf = nullptr
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
