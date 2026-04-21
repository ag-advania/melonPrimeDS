#ifndef MELON_PRIME_PATCH_H
#define MELON_PRIME_PATCH_H

#ifdef MELONPRIME_DS

#include <cstdint>

class EmuInstance;
namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {

    struct RomAddresses;

    // Apply in-game aspect ratio patch once (call after ROM detection).
    void InGameAspectRatio_ApplyOnce(EmuInstance* emu, Config::Table& localCfg,
                                      const RomAddresses& rom);

    // Reset patch tracking state (call on emu stop/reset).
    void InGameAspectRatio_ResetPatchState();

    // Apply OSD color patch once (call after game join).
    // Writes a single BGR555 color to all OSD message literal pool addresses,
    // and installs the H211 "node stolen" color-match shim.
    void OsdColor_ApplyOnce(EmuInstance* emu, Config::Table& localCfg,
                             const RomAddresses& rom);

    // Restore OSD color literals and H211 shim to original ROM values (call on game leave).
    // No-op if the patch was not applied.
    void OsdColor_RestoreOnce(melonDS::NDS* nds, const RomAddresses& rom);

    // Reset OSD color patch tracking state (call on emu stop/reset).
    void OsdColor_ResetPatchState();

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_H
