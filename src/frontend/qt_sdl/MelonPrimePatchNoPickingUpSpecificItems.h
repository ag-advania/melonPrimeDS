#ifndef MELON_PRIME_PATCH_NO_PICKING_UP_SPECIFIC_ITEMS_H
#define MELON_PRIME_PATCH_NO_PICKING_UP_SPECIFIC_ITEMS_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {
struct MelonPrimePatchState;

void NoPickingUpSpecificItems_ApplyOnce(MelonPrimePatchState& state, melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex);
void NoPickingUpSpecificItems_RestoreOnce(MelonPrimePatchState& state, melonDS::NDS* nds, uint8_t romGroupIndex);
void NoPickingUpSpecificItems_ResetPatchState(MelonPrimePatchState& state);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_NO_PICKING_UP_SPECIFIC_ITEMS_H
