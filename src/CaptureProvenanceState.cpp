/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "CaptureProvenanceState.h"

namespace melonDS
{

void CaptureProvenanceState::ResetSemanticState() noexcept
{
    Initialized = false;
    SemanticFrame = 0;
    SemanticCaptureGeneration = 0;
    SemanticEpoch = 0;
    SubmissionSerial = 0;
    CompletionValue = 0;
}

void CaptureProvenanceState::BeginNewEpoch() noexcept
{
    // Order matters only in that the epoch must change: every provenance
    // record already handed out carries the old one, and now stops matching.
    Initialized = false;
    ++Epoch;
    SemanticFrame = 0;
    SemanticCaptureGeneration = 0;
    SemanticEpoch = 0;
    SubmissionSerial = 0;
    CompletionValue = 0;
}

void CaptureProvenanceState::RecordSemanticSubmission(
    u64 frame, u64 captureGeneration) noexcept
{
    Initialized = true;
    SemanticFrame = frame;
    SemanticCaptureGeneration = captureGeneration;
    SemanticEpoch = Epoch;
}

void CaptureProvenanceState::CommitSubmissionSerial(u64 serial) noexcept
{
    SubmissionSerial = serial;
}

NativeCaptureStateIdentity CaptureProvenanceState::GetIdentity(
    CaptureOwner owner) const noexcept
{
    NativeCaptureStateIdentity identity{};
    identity.Valid = Initialized
        && SemanticEpoch == Epoch
        && SemanticFrame != 0u
        && CompletionValue != 0u;
    identity.Owner = owner;
    identity.Epoch = Epoch;
    identity.SemanticFrame = SemanticFrame;
    identity.CaptureGeneration = SemanticCaptureGeneration;
    identity.CompletionValue = CompletionValue;
    return identity;
}

bool CaptureProvenanceState::AcceptsBlock(
    const CaptureBlockProvenance& expected, CaptureOwner owner) const noexcept
{
    if (!Initialized)
        return false;
    if (expected.Owner != owner)
        return false;
    // Two epoch tests, not one: the block must belong to this renderer, and
    // this renderer must have recorded something native in that same epoch.
    if (expected.Epoch != Epoch || expected.Epoch != SemanticEpoch)
        return false;
    if (expected.SemanticFrame == 0u || expected.SemanticFrame > SemanticFrame)
        return false;
    if (expected.CaptureGeneration == 0u
        || expected.CaptureGeneration > SemanticCaptureGeneration)
    {
        return false;
    }
    if (expected.CompletionValue == 0u || expected.CompletionValue > CompletionValue)
        return false;
    return true;
}

} // namespace melonDS
