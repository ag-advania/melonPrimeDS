#ifndef MELON_PRIME_PATCH_DISABLE_DOUBLE_DAMAGE_MULTIPLIER_H
#define MELON_PRIME_PATCH_DISABLE_DOUBLE_DAMAGE_MULTIPLIER_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {
struct MelonPrimePatchState;

void DisableDoubleDamageMultiplier_ApplyOnce(MelonPrimePatchState& state, melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex);
void DisableDoubleDamageMultiplier_RestoreOnce(MelonPrimePatchState& state, melonDS::NDS* nds, uint8_t romGroupIndex);
void DisableDoubleDamageMultiplier_ResetPatchState(MelonPrimePatchState& state);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_DISABLE_DOUBLE_DAMAGE_MULTIPLIER_H
