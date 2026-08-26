#ifndef MELON_PRIME_HUD_CONFIG_STATE_H
#define MELON_PRIME_HUD_CONFIG_STATE_H

#ifdef MELONPRIME_CUSTOM_HUD

// =========================================================================
//  Custom HUD per-instance config-state boundary.
//
//  The narrow base every other Custom HUD header sits on: the opaque
//  per-instance state handle, its creation, and the config-cache generation
//  pair (invalidate / read epoch).  Nothing here pulls in Qt widget or event
//  types, so a consumer that only owns or invalidates HUD state does not have
//  to see the renderer, the editor, or the native-HUD patch lifecycle.
// =========================================================================

#include <cstdint>
#include <memory>

namespace Config { class Table; }

namespace MelonPrime {

    struct RomAddresses;
    struct GameAddressesHot;
    struct CustomHudConfigState;

    std::shared_ptr<CustomHudConfigState> CustomHud_CreateConfigState();

    // Returns true if the custom HUD setting is enabled in config.
    bool CustomHud_IsEnabled(Config::Table& localCfg);

    // Invalidate cached config (call when settings are saved).
    void CustomHud_InvalidateConfigCache(CustomHudConfigState& hudConfig);

    // Returns the current config cache generation counter.
    // Incremented every time the config cache is refreshed.
    // Screen.cpp uses this to skip re-reading config per-frame.
    uint32_t CustomHud_GetCacheEpoch(const CustomHudConfigState& hudConfig);

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_CONFIG_STATE_H
