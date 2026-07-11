#ifndef MELON_PRIME_PATCH_INSTANT_AIM_FOLLOW_H
#define MELON_PRIME_PATCH_INSTANT_AIM_FOLLOW_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {
struct MelonPrimePatchState;

void InstantAimFollow_ApplyOnce(
    MelonPrimePatchState& state,
    melonDS::NDS* nds,
    Config::Table& cfg,
    uint8_t romGroupIndex);

void InstantAimFollow_RestoreOnce(
    MelonPrimePatchState& state,
    melonDS::NDS* nds,
    uint8_t romGroupIndex);

void InstantAimFollow_ResetPatchState(MelonPrimePatchState& state);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_INSTANT_AIM_FOLLOW_H
