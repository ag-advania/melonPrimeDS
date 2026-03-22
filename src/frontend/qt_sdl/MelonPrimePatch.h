#ifndef MELON_PRIME_PATCH_H
#define MELON_PRIME_PATCH_H

#ifdef MELONPRIME_DS

#include <cstdint>

class EmuInstance;
namespace Config { class Table; }

namespace MelonPrime {

    struct RomAddresses;

    // Call once per frame from the render path.
    // Applies or restores the in-game aspect ratio scaling patch
    // based on config settings.
    void InGameAspectRatio_Tick(EmuInstance* emu, Config::Table& localCfg,
                            const RomAddresses& rom, bool isInGame,
                            int screenAspectId);

    // Reset patch tracking state (call on emu stop/reset).
    void InGameAspectRatio_ResetPatchState();

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_H
