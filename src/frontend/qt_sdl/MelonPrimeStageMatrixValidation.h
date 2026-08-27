#ifndef MELON_PRIME_STAGE_MATRIX_VALIDATION_H
#define MELON_PRIME_STAGE_MATRIX_VALIDATION_H

#ifdef MELONPRIME_DS

#include "MelonPrimePatchState.h"

#include <algorithm>
#include <cstdint>

namespace MelonPrime::StageMatrixValidation {

// The retry state is host bookkeeping, not guest state. A bounded exponential
// backoff keeps a bad/unfinished image cheap while still allowing a delayed
// ROM load to become eligible without another lifecycle event.
inline constexpr uint8_t kMaxRetryBackoffFrames = 60u;

inline void ResetRetry(
    MelonPrimePatchState::ExpandStageMatrixPatchState& patch) noexcept
{
    patch.validationRetryFrames = 0u;
    patch.validationBackoffFrames = 1u;
}

inline bool ConsumeRetryCooldown(
    MelonPrimePatchState::ExpandStageMatrixPatchState& patch) noexcept
{
    if (patch.validationRetryFrames == 0u)
        return false;
    --patch.validationRetryFrames;
    return true;
}

inline void MarkCandidateAbsent(
    MelonPrimePatchState::ExpandStageMatrixPatchState& patch) noexcept
{
    patch.status = MelonPrimePatchState::ExpandStageMatrixStatus::WaitingForLoad;
    patch.candidateSeen = false;
    ResetRetry(patch);
}

inline void MarkValidationRetry(
    MelonPrimePatchState::ExpandStageMatrixPatchState& patch) noexcept
{
    patch.status = MelonPrimePatchState::ExpandStageMatrixStatus::WaitingForLoad;
    patch.candidateSeen = true;

    const uint8_t currentBackoff = patch.validationBackoffFrames == 0u
        ? 1u
        : patch.validationBackoffFrames;
    patch.validationRetryFrames = currentBackoff;
    const unsigned doubledBackoff = static_cast<unsigned>(currentBackoff) * 2u;
    patch.validationBackoffFrames = static_cast<uint8_t>(std::min(
        doubledBackoff, static_cast<unsigned>(kMaxRetryBackoffFrames)));
}

inline void MarkVerified(
    MelonPrimePatchState::ExpandStageMatrixPatchState& patch) noexcept
{
    patch.status = MelonPrimePatchState::ExpandStageMatrixStatus::Verified;
    patch.candidateSeen = true;
    patch.pendingRestore = false;
    ResetRetry(patch);
}

inline void MarkGuestUnloaded(
    MelonPrimePatchState::ExpandStageMatrixPatchState& patch) noexcept
{
    patch.status = MelonPrimePatchState::ExpandStageMatrixStatus::WaitingForLoad;
    patch.candidateSeen = false;
    patch.appliedBase = false;
    patch.appliedExtra = false;
    ResetRetry(patch);
}

} // namespace MelonPrime::StageMatrixValidation

#endif // MELONPRIME_DS
#endif // MELON_PRIME_STAGE_MATRIX_VALIDATION_H
