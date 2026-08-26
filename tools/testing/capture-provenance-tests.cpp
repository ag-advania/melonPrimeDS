/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// Acceptance rules for native Display Capture provenance.
//
// This state decides whether a capture block recorded some frames ago may
// still be served from the renderer's native mirror. A wrong "yes" does not
// crash: it hands the emulated program VRAM contents from the wrong frame, or
// from a renderer that no longer exists, and the symptom surfaces much later
// as a corrupted in-game camera feed. Both backends now share this logic, so
// a mistake here would be wrong twice.
//
// Every comparison it makes is deliberately one-sided, and each of those
// asymmetries is pinned down below.

#include <cstdio>

#include "CaptureProvenanceState.h"

using melonDS::CaptureOwner;
using melonDS::CaptureBlockProvenance;
using melonDS::CaptureProvenanceState;

namespace
{

int gFailures = 0;

void Check(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    ++gFailures;
}

// A state that has recorded one native semantic frame, plus a provenance
// record that exactly matches it.
struct Recorded
{
    CaptureProvenanceState State{1000u};
    CaptureBlockProvenance Block;

    Recorded()
    {
        State.RecordSemanticSubmission(42u, 7u);
        State.CommitSubmissionSerial(5u);
        State.SetCompletionValue(5u);

        Block.Owner = CaptureOwner::NativeDX12;
        Block.Epoch = State.GetEpoch();
        Block.SemanticFrame = 42u;
        Block.CaptureGeneration = 7u;
        Block.CompletionValue = 5u;
    }

    [[nodiscard]] bool Accepts() const
    {
        return State.AcceptsBlock(Block, CaptureOwner::NativeDX12);
    }
};

void TestFreshState()
{
    CaptureProvenanceState state{1000u};
    CaptureBlockProvenance block;
    block.Owner = CaptureOwner::NativeDX12;
    block.Epoch = state.GetEpoch();
    block.SemanticFrame = 1u;
    block.CaptureGeneration = 1u;
    block.CompletionValue = 1u;

    Check(!state.IsInitialized(), "a fresh state has recorded nothing");
    Check(state.MirrorNeedsFullCopy(), "a fresh mirror needs a full copy");
    Check(!state.AcceptsBlock(block, CaptureOwner::NativeDX12),
        "nothing is readable before anything is recorded");
    Check(!state.GetIdentity(CaptureOwner::NativeDX12).Valid,
        "a fresh identity is not valid");
    Check(state.PeekNextSubmissionSerial() == 1u, "submission serials start at 1");
}

void TestRecordedStateAccepts()
{
    Recorded r;
    Check(r.Accepts(), "the block that was just recorded is readable");
    Check(!r.State.MirrorNeedsFullCopy(),
        "a mirror recorded in the current epoch needs no full copy");

    const auto identity = r.State.GetIdentity(CaptureOwner::NativeDX12);
    Check(identity.Valid, "a recorded identity is valid");
    Check(identity.Owner == CaptureOwner::NativeDX12, "the identity carries its owner");
    Check(identity.Epoch == r.State.GetEpoch(), "the identity carries the epoch");
    Check(identity.SemanticFrame == 42u, "the identity carries the semantic frame");
    Check(identity.CaptureGeneration == 7u, "the identity carries the generation");
    Check(identity.CompletionValue == 5u, "the identity carries the completion value");
}

void TestOwnerIsolation()
{
    Recorded r;
    // A block written by the other backend is refused, never reinterpreted:
    // the two renderers' mirrors are different memory.
    Check(!r.State.AcceptsBlock(r.Block, CaptureOwner::NativeVulkan),
        "a block is not readable under a different backend's owner");

    r.Block.Owner = CaptureOwner::NativeVulkan;
    Check(!r.Accepts(), "a block owned by another backend is refused");

    r.Block.Owner = CaptureOwner::None;
    Check(!r.Accepts(), "an unowned block is refused");
}

void TestEpochIsolation()
{
    Recorded r;
    r.Block.Epoch = r.State.GetEpoch() + 1u;
    Check(!r.Accepts(), "a block from a different epoch is refused");

    // A new epoch retires every record already handed out. This is what makes
    // a renderer switch or a resolution change invalidate capture wholesale.
    Recorded fresh;
    const melonDS::u64 before = fresh.State.GetEpoch();
    fresh.State.BeginNewEpoch();
    Check(fresh.State.GetEpoch() == before + 1u, "a new epoch advances by one");
    Check(!fresh.Accepts(), "the previous epoch's blocks stop being readable");
    Check(fresh.State.MirrorNeedsFullCopy(), "a new epoch needs a full copy");
    Check(fresh.State.PeekNextSubmissionSerial() == 1u,
        "a new epoch restarts the submission serial");
}

void TestSemanticEpochMustMatch()
{
    // Both epoch tests matter. Here the block belongs to the current epoch,
    // but this state has recorded nothing native *in* that epoch, so there is
    // no mirror behind the record.
    CaptureProvenanceState state{1000u};
    CaptureBlockProvenance block;
    block.Owner = CaptureOwner::NativeDX12;
    block.Epoch = state.GetEpoch();
    block.SemanticFrame = 1u;
    block.CaptureGeneration = 1u;
    block.CompletionValue = 1u;
    Check(!state.AcceptsBlock(block, CaptureOwner::NativeDX12),
        "a current-epoch block with no recorded frame is refused");
}

void TestOneSidedComparisons()
{
    // Older is fine; the mirror has advanced past it. Newer is not: it
    // describes work that has not happened, which means the record and this
    // state disagree about time.
    {
        Recorded r;
        r.Block.SemanticFrame = 41u;
        Check(r.Accepts(), "an older semantic frame is still readable");
        r.Block.SemanticFrame = 43u;
        Check(!r.Accepts(), "a future semantic frame is refused");
        r.Block.SemanticFrame = 0u;
        Check(!r.Accepts(), "an unset semantic frame is refused");
    }
    {
        Recorded r;
        r.Block.CaptureGeneration = 6u;
        Check(r.Accepts(), "an older capture generation is still readable");
        r.Block.CaptureGeneration = 8u;
        Check(!r.Accepts(), "a future capture generation is refused");
        r.Block.CaptureGeneration = 0u;
        Check(!r.Accepts(), "an unset capture generation is refused");
    }
    {
        Recorded r;
        r.Block.CompletionValue = 4u;
        Check(r.Accepts(), "an older completion value is still readable");
        r.Block.CompletionValue = 6u;
        Check(!r.Accepts(), "a completion value ahead of the GPU is refused");
        r.Block.CompletionValue = 0u;
        Check(!r.Accepts(), "an unset completion value is refused");
    }
}

void TestResetAndInvalidate()
{
    {
        Recorded r;
        const melonDS::u64 epoch = r.State.GetEpoch();
        r.State.ResetSemanticState();
        Check(r.State.GetEpoch() == epoch,
            "a semantic reset keeps the renderer's identity");
        Check(!r.Accepts(), "a semantic reset stops serving recorded blocks");
        Check(r.State.MirrorNeedsFullCopy(), "a semantic reset needs a full copy");
    }
    {
        // Replacing the mirror's backing resources invalidates what was
        // recorded without changing identity or renumbering submissions.
        Recorded r;
        const melonDS::u64 epoch = r.State.GetEpoch();
        r.State.InvalidateMirror();
        Check(r.State.GetEpoch() == epoch, "invalidating keeps the epoch");
        Check(!r.Accepts(), "an invalidated mirror serves nothing");
        Check(r.State.MirrorNeedsFullCopy(), "an invalidated mirror needs a full copy");
        Check(r.State.PeekNextSubmissionSerial() == 6u,
            "invalidating does not rewind the submission serial");
    }
}

void TestSubmissionSerialSequence()
{
    CaptureProvenanceState state{1000u};
    Check(state.PeekNextSubmissionSerial() == 1u, "the first serial is 1");
    Check(state.PeekNextSubmissionSerial() == 1u, "peeking does not consume");

    // The producer needs the value while recording and commits it afterwards,
    // so a dropped frame must not advance the sequence.
    state.CommitSubmissionSerial(1u);
    Check(state.PeekNextSubmissionSerial() == 2u, "committing advances the sequence");
    (void)state.PeekNextSubmissionSerial();
    Check(state.PeekNextSubmissionSerial() == 2u,
        "a frame that peeked but never committed leaves the sequence alone");
}

} // namespace

int main()
{
    TestFreshState();
    TestRecordedStateAccepts();
    TestOwnerIsolation();
    TestEpochIsolation();
    TestSemanticEpochMustMatch();
    TestOneSidedComparisons();
    TestResetAndInvalidate();
    TestSubmissionSerialSequence();

    if (gFailures != 0)
    {
        std::printf("capture-provenance-tests: FAIL (%d)\n", gFailures);
        return 1;
    }
    std::printf("capture-provenance-tests: PASS\n");
    return 0;
}
