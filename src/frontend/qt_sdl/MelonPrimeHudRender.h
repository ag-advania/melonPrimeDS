#ifndef MELON_PRIME_HUD_RENDER_H
#define MELON_PRIME_HUD_RENDER_H

#ifdef MELONPRIME_CUSTOM_HUD

#include <cstdint>
#include <functional>
#include <QMouseEvent>

class EmuInstance;
class QPainter;
class QImage;
namespace Config { class Table; }

namespace MelonPrime {

    struct RomAddresses;
    struct GameAddressesHot;

    // =========================================================================
    //  CustomHud_Render
    //
    //  Main entry point — call once per frame from the OSD/overlay render path.
    //  Draws HP, weapon icon + ammo count, and crosshair onto the overlay.
    //
    //  Parameters:
    //    emu            — EmuInstance for NDS memory access
    //    localCfg       — config table (reads Metroid.Visual.CustomHUD,
    //                     Metroid.Visual.CrosshairSize)
    //    rom            — ROM address table (resolved for current ROM)
    //    addrHot        — player-position-adjusted hot addresses
    //    playerPosition — current player position index (for +0xF30 offset)
    //    topPaint       — QPainter for the top-screen overlay
    //    btmPaint       — QPainter for the bottom-screen overlay
    //    topBuffer      — QImage backing the top overlay (cleared inside)
    //    btmBuffer      — QImage backing the bottom overlay (cleared inside)
    //    isInGame       — whether the game is currently in a match
    //    topStretchX    — widescreen X stretch factor (1.0=4:3, >1.0=wide)
    // =========================================================================
    void CustomHud_Render(
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
        float topStretchX = 1.0f,
        float hudScale = 1.0f
    );

    // Returns true if the custom HUD setting is enabled in config.
    bool CustomHud_IsEnabled(Config::Table& localCfg);

    // Returns true when the shared HUD hide condition is active.
    bool CustomHud_ShouldHideForGameplayState(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition);

    // Returns true when the radar overlay should be drawn on the top screen.
    bool CustomHud_ShouldDrawRadarOverlay(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition);

    // Ensure the no-HUD patch is reverted when custom HUD is disabled.
    // Call every frame from Screen.cpp even when the HUD overlay is not rendered.
    void CustomHud_EnsurePatchRestored(
        EmuInstance* emu,
        Config::Table& localCfg,
        const RomAddresses& rom,
        uint8_t playerPosition,
        bool isInGame
    );

    // Reset patch tracking state (call on emu stop/reset).
    void CustomHud_ResetPatchState();

    // Invalidate cached config (call when settings are saved).
    void CustomHud_InvalidateConfigCache();

    // Returns the current config cache generation counter.
    // Incremented every time the config cache is refreshed.
    // Screen.cpp uses this to skip re-reading config per-frame.
    uint32_t CustomHud_GetCacheEpoch();

    // Cache battle settings at match join (call from HandleGameJoinInit).
    void CustomHud_OnMatchJoin(uint8_t* ram, const RomAddresses& rom);

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

    // =========================================================================
    //  HUD Layout Editor
    // =========================================================================

    // Enter interactive HUD layout editing mode. Frees the cursor and draws
    // draggable element boxes on the top screen. Call from the UI thread only.
    void CustomHud_EnterEditMode(EmuInstance* emu, Config::Table& cfg);

    // Commit (save=true) or discard (save=false) changes and leave edit mode.
    void CustomHud_ExitEditMode(bool save, Config::Table& cfg);

    // Returns true while the HUD layout editor is active.
    bool CustomHud_IsEditMode();

    // Update the coordinate context used by mouse handlers (call each render).
    void CustomHud_UpdateEditContext(float originX, float originY,
                                     float hudScale, float topStretchX);

    // Forward mouse events from the screen panel to the layout editor.
    void CustomHud_EditMousePress  (QPointF pt, Qt::MouseButton btn, Config::Table& cfg);
    void CustomHud_EditMouseMove   (QPointF pt, Config::Table& cfg);
    void CustomHud_EditMouseRelease(QPointF pt, Qt::MouseButton btn, Config::Table& cfg);
    void CustomHud_EditMouseWheel(QPointF pt, int delta, Config::Table& cfg);

    // Register a callback invoked whenever the selected element changes in edit mode.
    // Pass nullptr to clear. The int argument is the element index (-1 = none).
    void CustomHud_SetEditSelectionCallback(std::function<void(int)> cb);

    // Returns the currently selected element index (-1 = none).
    int  CustomHud_GetSelectedElement();

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_RENDER_H

