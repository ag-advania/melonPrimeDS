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

#ifndef CAPTURE_PROVENANCE_STATE_H
#define CAPTURE_PROVENANCE_STATE_H

#include "GPU.h"

namespace melonDS
{

// Semantic owner of native Display Capture provenance.
//
// The audit's ownership rule for capture reads:
//
//     semantic owner:  backend-neutral capture provenance
//     physical owner:  VulkanCaptureBridge / DX12CaptureBridge
//
// This is that semantic owner. Both renderers carried the same six fields and
// the same acceptance predicate, character for character; the only thing that
// differed was which CaptureOwner enumerator they compared against.
//
// What it answers is: given a capture block whose provenance was recorded some
// frames ago, is the native mirror still the authority for it? Getting that
// wrong does not crash -- it hands the emulated program VRAM contents from the
// wrong frame, or from a renderer that no longer exists.
//
// It performs no GPU work and holds no GPU handle. The copy, the fence and the
// mapping stay with the backend that can issue them, and the high-resolution
// sidecar tracker stays with the renderer that owns the sidecar -- this is the
// semantic mirror only.
class CaptureProvenanceState
{
public:
    // Epochs are allocated process-wide so two live renderers cannot claim the
    // same one. Where they come from is not this class's decision, so the
    // owner supplies the first one.
    explicit CaptureProvenanceState(u64 initialEpoch) noexcept
        : Epoch(initialEpoch)
    {
    }

    // The renderer's identity for the lifetime of its current resource set. A
    // fresh epoch is what makes every previously recorded provenance block
    // stop matching, which is how a renderer switch or a scale change
    // invalidates capture without walking any tables.
    [[nodiscard]] u64 GetEpoch() const noexcept { return Epoch; }

    // Retires every recorded semantic frame but keeps the epoch. Used where
    // the renderer drops derived state without changing identity.
    void ResetSemanticState() noexcept;

    // Retires everything and takes a new epoch. Used on renderer reset and on
    // resolution change, where the resources behind the mirror are replaced.
    void BeginNewEpoch() noexcept;

    // A native semantic submission completed recording for `frame`.
    void RecordSemanticSubmission(u64 frame, u64 captureGeneration) noexcept;

    // The submission serial the next semantic frame will carry. The producer
    // needs the value while recording and commits it afterwards.
    [[nodiscard]] u64 PeekNextSubmissionSerial() const noexcept
    {
        return SubmissionSerial + 1u;
    }
    void CommitSubmissionSerial(u64 serial) noexcept;

    // The completion value a reader must not have run ahead of.
    void SetCompletionValue(u64 value) noexcept { CompletionValue = value; }
    [[nodiscard]] u64 GetCompletionValue() const noexcept { return CompletionValue; }

    // The resources backing the semantic mirror were replaced, so nothing
    // recorded is readable any more -- but the epoch and the serial sequence
    // are unchanged, because the renderer's identity did not change. The next
    // producer will refresh the whole mirror.
    void InvalidateMirror() noexcept { Initialized = false; }

    // True when nothing native has been recorded for the current epoch, so a
    // producer must refresh the whole mirror rather than a delta.
    [[nodiscard]] bool MirrorNeedsFullCopy() const noexcept
    {
        return !Initialized || SemanticEpoch != Epoch;
    }

    [[nodiscard]] bool IsInitialized() const noexcept { return Initialized; }
    [[nodiscard]] u64 GetSemanticFrame() const noexcept { return SemanticFrame; }
    [[nodiscard]] u64 GetSemanticCaptureGeneration() const noexcept
    {
        return SemanticCaptureGeneration;
    }
    [[nodiscard]] u64 GetSemanticEpoch() const noexcept { return SemanticEpoch; }

    // What the frontend publishes alongside a composed frame.
    [[nodiscard]] NativeCaptureStateIdentity GetIdentity(
        CaptureOwner owner) const noexcept;

    // Whether a recorded provenance block may still be served from the native
    // mirror. `owner` is the backend's own enumerator: a block owned by the
    // other backend is refused rather than reinterpreted.
    //
    // Every comparison is deliberately one-sided. A block from a *newer*
    // semantic frame, capture generation or completion value than this state
    // has reached describes work that has not happened yet, which means the
    // provenance record and this state disagree about time.
    [[nodiscard]] bool AcceptsBlock(
        const CaptureBlockProvenance& expected, CaptureOwner owner) const noexcept;

private:
    u64 Epoch = 0;
    u64 SemanticFrame = 0;
    u64 SemanticCaptureGeneration = 0;
    u64 SemanticEpoch = 0;
    u64 SubmissionSerial = 0;
    u64 CompletionValue = 0;
    bool Initialized = false;
};

} // namespace melonDS

#endif // CAPTURE_PROVENANCE_STATE_H
