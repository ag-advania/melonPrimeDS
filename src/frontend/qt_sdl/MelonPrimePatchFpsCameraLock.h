#ifndef MELON_PRIME_PATCH_FPS_CAMERA_LOCK_H
#define MELON_PRIME_PATCH_FPS_CAMERA_LOCK_H

#ifdef MELONPRIME_DS

#include <cstdint>

namespace Config { class Table; }
namespace melonDS { class NDS; }

namespace MelonPrime {
struct MelonPrimePatchState;

void FpsCameraLock_ApplyOnce(
    MelonPrimePatchState& state,
    melonDS::NDS* nds,
    Config::Table& cfg,
    uint8_t romGroupIndex);

void FpsCameraLock_RestoreOnce(
    MelonPrimePatchState& state,
    melonDS::NDS* nds,
    uint8_t romGroupIndex);

void FpsCameraLock_ResetPatchState(MelonPrimePatchState& state);

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_FPS_CAMERA_LOCK_H
