#ifndef MELON_PRIME_PATCH_OSD_COLOR_H
#define MELON_PRIME_PATCH_OSD_COLOR_H

#ifdef MELONPRIME_DS

class EmuInstance;
namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {

    struct RomAddresses;
    struct MelonPrimePatchState;

    void OsdColor_ApplyOnce(MelonPrimePatchState& state,
                             EmuInstance* emu, Config::Table& localCfg,
                             const RomAddresses& rom);
    void OsdColor_RestoreOnce(MelonPrimePatchState& state,
                              melonDS::NDS* nds, const RomAddresses& rom);
    void OsdColor_InvalidatePatch(MelonPrimePatchState& state);
    void OsdColor_ResetPatchState(MelonPrimePatchState& state);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_OSD_COLOR_H
