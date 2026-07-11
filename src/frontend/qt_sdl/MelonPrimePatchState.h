#ifndef MELON_PRIME_PATCH_STATE_H
#define MELON_PRIME_PATCH_STATE_H

#ifdef MELONPRIME_DS

#include "MelonPrimePatchCommon.h"

#include <cstdint>

namespace MelonPrime {

struct MelonPrimePatchState {
    bool aspectRatioApplied = false;

    struct OsdColorState {
        bool applied = false;
        int h211Mode = 0;
        bool configDirty = true;
    } osdColor;

    StaticWordPatchState instantAimFollow;
    StaticWordPatchState showHeadshotOnline;
    StaticWordPatchState showEnemyHpOnline;
    StaticWordPatchState disableDoubleDamage;

    struct NoSpecificItemPickupState {
        bool hasAppliedRomGroup = false;
        uint8_t appliedRomGroupIndex = 0xFFu;
        uint8_t appliedMask = 0;
    } noSpecificItemPickup;

    bool expandStageMatrixPendingRestore = false;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_STATE_H
