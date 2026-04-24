#ifndef MELON_PRIME_PATCH_ASPECT_RATIO_H
#define MELON_PRIME_PATCH_ASPECT_RATIO_H

#ifdef MELONPRIME_DS

class EmuInstance;
namespace Config { class Table; }

namespace MelonPrime {

    struct RomAddresses;

    void InGameAspectRatio_ApplyOnce(EmuInstance* emu, Config::Table& localCfg,
                                      const RomAddresses& rom);
    void InGameAspectRatio_ResetPatchState();

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_ASPECT_RATIO_H
