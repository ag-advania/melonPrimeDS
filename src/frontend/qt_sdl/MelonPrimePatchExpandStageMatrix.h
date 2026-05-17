#ifndef MELON_PRIME_PATCH_EXPAND_STAGE_MATRIX_H
#define MELON_PRIME_PATCH_EXPAND_STAGE_MATRIX_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {

// Dynamically writes 0x01 to the 14 verified OK-only stage/mode cells in the
// multiplayer stage select compatibility matrix.  Applied only when the guard
// confirms the matrix data block is loaded (strict 3-point check).
void ExpandStageMatrix_ApplyIfLoaded(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex);
void ExpandStageMatrix_ResetPatchState();

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_EXPAND_STAGE_MATRIX_H
