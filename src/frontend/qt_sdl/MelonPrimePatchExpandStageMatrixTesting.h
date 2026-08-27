#ifndef MELON_PRIME_PATCH_EXPAND_STAGE_MATRIX_TESTING_H
#define MELON_PRIME_PATCH_EXPAND_STAGE_MATRIX_TESTING_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace MelonPrime {

struct MelonPrimePatchState;

// Callback-based fake RAM seam for the deterministic developer test. The
// production entry point uses the same templated implementation with NDS
// callbacks, so the test does not carry a second state-machine model.
struct StageMatrixTestMemory {
    void* context = nullptr;
    uint8_t (*read8)(void*, uint32_t) = nullptr;
    uint32_t (*read32)(void*, uint32_t) = nullptr;
    void (*write8)(void*, uint32_t, uint8_t) = nullptr;
};

void ExpandStageMatrix_ApplyIfLoadedForTesting(
    MelonPrimePatchState& state,
    StageMatrixTestMemory& memory,
    bool enabled,
    bool extraEnabled,
    uint8_t romGroupIndex);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_EXPAND_STAGE_MATRIX_TESTING_H
