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
        bool isInGame
    );

    // Returns true if the custom HUD setting is enabled in config.
    bool CustomHud_IsEnabled(Config::Table& localCfg);

    // Reset patch tracking state (call on emu stop/reset).
    void CustomHud_ResetPatchState();

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_CUSTOM_HUD_H
