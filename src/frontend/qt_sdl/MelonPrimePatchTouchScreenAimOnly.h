#ifndef MELON_PRIME_PATCH_TOUCH_SCREEN_AIM_ONLY_H
#define MELON_PRIME_PATCH_TOUCH_SCREEN_AIM_ONLY_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {
struct MelonPrimePatchState;

void TouchScreenAimOnly_ApplyOnce(MelonPrimePatchState& state, melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex);
void TouchScreenAimOnly_RestoreOnce(MelonPrimePatchState& state, melonDS::NDS* nds, uint8_t romGroupIndex);
void TouchScreenAimOnly_ResetPatchState(MelonPrimePatchState& state);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_TOUCH_SCREEN_AIM_ONLY_H
