#ifndef MELON_PRIME_PATCH_SHOW_ENEMY_HP_METER_ONLINE_H
#define MELON_PRIME_PATCH_SHOW_ENEMY_HP_METER_ONLINE_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {
struct MelonPrimePatchState;

void ShowEnemyHpMeterOnline_ApplyOnce(MelonPrimePatchState& state, melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex);
void ShowEnemyHpMeterOnline_RestoreOnce(MelonPrimePatchState& state, melonDS::NDS* nds, uint8_t romGroupIndex);
void ShowEnemyHpMeterOnline_ResetPatchState(MelonPrimePatchState& state);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_SHOW_ENEMY_HP_METER_ONLINE_H
