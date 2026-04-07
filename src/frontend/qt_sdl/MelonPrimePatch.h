#ifndef MELON_PRIME_PATCH_H
#define MELON_PRIME_PATCH_H

#ifdef MELONPRIME_DS

#include <cstdint>

class EmuInstance;
namespace Config { class Table; }

namespace MelonPrime {

    struct RomAddresses;

    // Apply in-game aspect ratio patch once (call after ROM detection).
    void InGameAspectRatio_ApplyOnce(EmuInstance* emu, Config::Table& localCfg,
                                      const RomAddresses& rom);

    // Reset patch tracking state (call on emu stop/reset).
    void InGameAspectRatio_ResetPatchState();

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_H
