#ifndef MELON_PRIME_CUSTOM_HUD_H
#define MELON_PRIME_CUSTOM_HUD_H

#ifdef MELONPRIME_CUSTOM_HUD

#include <cstdint>

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

    // Reset patch tracking state (call on emu stop/reset).
    void CustomHud_ResetPatchState();

    // Invalidate cached config (call when settings are saved).
    void CustomHud_InvalidateConfigCache();

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

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_CUSTOM_HUD_H

