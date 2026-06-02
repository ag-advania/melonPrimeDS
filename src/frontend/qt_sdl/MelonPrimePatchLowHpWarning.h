#ifndef MELON_PRIME_PATCH_LOW_HP_WARNING_H
#define MELON_PRIME_PATCH_LOW_HP_WARNING_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {

    // Rewrites MPH's low-HP-warning `cmp r0,#0x19` immediate to a configurable
    // threshold. v3 "Setup Apply" method: called once per match join (from
    // HandleGameJoinInit), reads the current battle DamageLevel, and writes the `cmp`
    // word. No dynamic hook / code cave; the in-match low-HP check stays vanilla
    // `cmp r0,#imm`. Mode=Disabled is a no-op.
    void LowHpWarning_ApplyOnce(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex);
    void LowHpWarning_ResetPatchState();

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_LOW_HP_WARNING_H
