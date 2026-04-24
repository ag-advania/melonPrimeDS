#ifndef MELON_PRIME_PATCH_OSD_COLOR_H
#define MELON_PRIME_PATCH_OSD_COLOR_H

#ifdef MELONPRIME_DS

class EmuInstance;
namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {

    struct RomAddresses;

    void OsdColor_ApplyOnce(EmuInstance* emu, Config::Table& localCfg,
                             const RomAddresses& rom);
    void OsdColor_RestoreOnce(melonDS::NDS* nds, const RomAddresses& rom);
    void OsdColor_InvalidatePatch();
    void OsdColor_ResetPatchState();

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_OSD_COLOR_H
