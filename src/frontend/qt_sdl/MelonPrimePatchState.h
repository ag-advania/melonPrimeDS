#ifndef MELON_PRIME_PATCH_STATE_H
#define MELON_PRIME_PATCH_STATE_H

#ifdef MELONPRIME_DS

#include "MelonPrimePatchCommon.h"
#include "MelonPrimeRuntimeConfig.h"

#include <cstdint>

namespace MelonPrime {

struct MelonPrimePatchState {
    OutOfGamePatchSnapshot outOfGamePatches{};

    struct FixWifiPatchState {
        enum class Status : uint8_t {
            Unchecked,
            Applied,
            Rejected,
            Unsupported,
        };

        Status status = Status::Unchecked;
        uint8_t romGroupIndex = 0xFFu;
    } fixWifi;

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

    enum class ExpandStageMatrixStatus : uint8_t {
        Unknown,
        WaitingForLoad,
        Verified,
    };

    struct ExpandStageMatrixPatchState {
        ExpandStageMatrixStatus status = ExpandStageMatrixStatus::Unknown;
        uint8_t romGroupIndex = 0xFFu;
        bool candidateSeen = false;
        bool appliedBase = false;
        bool appliedExtra = false;
        bool pendingRestore = false;
    } expandStageMatrix;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS
#endif // MELON_PRIME_PATCH_STATE_H
