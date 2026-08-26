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

#ifndef GPU2D_FRAME_POLICY_H
#define GPU2D_FRAME_POLICY_H

#include "GPU3D.h"

namespace melonDS::GPU2DFramePolicy
{

// Backend-neutral publication policy for one emulated frame's 2D output.
//
// VulkanRenderer::VBlank() and DX12Renderer::VBlank() used to carry the same
// ~90-line decision tree, differing only in log strings, perf counters and
// which Renderer3D they called. That is a policy, not a graphics API: nothing
// below knows what a VkImage, an ID3D12Resource, a command buffer or a
// descriptor is, and nothing below may ever learn.
//
// Everything here is a pure function of facts the caller already has, so the
// policy is checkable without a GPU.

// A compose attempt whose result means "this frame is legitimately not on
// screen yet". A full presenter ring or a still-compiling pipeline is not a
// correctness failure and must never be promoted into a Software hybrid
// frame -- the whole reason these three are grouped.
[[nodiscard]] constexpr bool IsRetainedFrameResult(GPU2DComposeResult result) noexcept
{
    return result == GPU2DComposeResult::Backpressure
        || result == GPU2DComposeResult::SemanticOnly
        || result == GPU2DComposeResult::Unavailable;
}

// Physical capture ownership is published after semantic submission, even
// when presentation backpressure retained the previously visible frame: the
// next frame may read capture before its own recorder is finalized and must
// still select this native mirror.
[[nodiscard]] constexpr bool ShouldPublishCaptureProvenance(
    GPU2DComposeResult result) noexcept
{
    return result == GPU2DComposeResult::Success
        || result == GPU2DComposeResult::SemanticOnly;
}

// What the renderer wrapper does after Evaluate().
enum class Outcome : int
{
    // The native compositor published this frame. Nothing further to do.
    NativePublished,
    // Keep the last published frame. Not a fallback and not a failure.
    RetainLastFrame,
    // Refuse to publish. The caller reports Decision::FailureReason through
    // its backend's FailNativeGPU2DExact().
    FailNativeExact,
    // The backend already latched a runtime failure; report it and stop.
    ReportRuntimeFailure,
    // Native did not publish and may legitimately be replaced by the CPU
    // structured compositor for this frame.
    TryStructuredFallback,
};

// Why a Software fallback is being announced. The caller renders this into
// its own backend-tagged log line; the distinction is worth keeping because
// "the native path could not dispatch" and "no native frame existed" have
// different causes.
enum class FallbackReason : int
{
    None,
    NativeDispatchUnavailable,
    NativeFrameUnavailable,
};

[[nodiscard]] constexpr const char* FallbackReasonText(FallbackReason reason) noexcept
{
    switch (reason)
    {
    case FallbackReason::NativeDispatchUnavailable: return "native dispatch unavailable";
    case FallbackReason::NativeFrameUnavailable: return "native frame unavailable";
    case FallbackReason::None: break;
    }
    return "";
}

// Post-compose facts. Every field is something the caller observed this
// frame; none of them is a GPU handle.
struct FrameFacts
{
    // A native Renderer3D exists at all. False means the whole native branch
    // is inert for this frame.
    bool HasNativeRenderer = false;
    // A native GPU2D frame was recorded for the *current* emulated frame.
    bool HasNativeFrameForCurrentEmulatedFrame = false;
    // A native GPU2D frame exists but may belong to an earlier generation.
    bool HasNativeFrame = false;
    // The compose attempt actually published.
    bool NativeComposed = false;
    GPU2DComposeResult ComposeResult = GPU2DComposeResult::Unavailable;
    // Native ownership was latched for this frame, so no CPU structured frame
    // was produced for this generation.
    bool NativeProducer = false;
    bool ExactValidationEnabled = false;
    bool RendererHasRuntimeFailure = false;
    bool CaptureEnabled = false;
    // The caller has already emitted its one-shot Software fallback line.
    bool FallbackAlreadyAnnounced = false;
};

// The caller applies these in declaration order, which is the order the
// pre-refactor inline branches produced them in.
struct Decision
{
    Outcome Result = Outcome::TryStructuredFallback;
    // Non-null only for Outcome::FailNativeExact.
    const char* FailureReason = nullptr;
    bool AnnounceNativeSuccess = false;
    bool RecordStaleGenerationReject = false;
    bool RecordRuntimeNativeUnavailableFallback = false;
    bool RecordCaptureSoftwareFallback = false;
    bool CountNativeFallbackFrame = false;
    FallbackReason AnnounceFallback = FallbackReason::None;
};

// True when the caller should ask its compositor for a native compose. Kept
// separate from Evaluate() because the compose result is one of Evaluate()'s
// inputs.
[[nodiscard]] constexpr bool ShouldAttemptNativeCompose(
    bool hasNativeRenderer, bool hasNativeFrameForCurrentEmulatedFrame) noexcept
{
    return hasNativeRenderer && hasNativeFrameForCurrentEmulatedFrame;
}

[[nodiscard]] constexpr Decision Evaluate(const FrameFacts& facts) noexcept
{
    Decision decision;

    // --- Stage 1: classify what the native attempt produced ----------------
    bool fallbackAnnounced = facts.FallbackAlreadyAnnounced;
    if (facts.HasNativeRenderer && facts.HasNativeFrameForCurrentEmulatedFrame)
    {
        if (facts.NativeComposed)
        {
            decision.AnnounceNativeSuccess = true;
        }
        else if (!facts.NativeProducer
            || facts.ComposeResult == GPU2DComposeResult::Fatal)
        {
            decision.RecordRuntimeNativeUnavailableFallback = true;
            decision.CountNativeFallbackFrame = true;
            if (!fallbackAnnounced)
            {
                decision.AnnounceFallback = FallbackReason::NativeDispatchUnavailable;
                fallbackAnnounced = true;
            }
        }
    }
    else if (facts.HasNativeRenderer && facts.HasNativeFrame)
    {
        // A native frame exists but belongs to an earlier generation. Never
        // compose it: a stale image is worse than the retained one.
        decision.RecordStaleGenerationReject = true;
    }

    // --- Stage 2: native ownership is exclusive ----------------------------
    if (facts.NativeProducer && !facts.NativeComposed)
    {
        if (IsRetainedFrameResult(facts.ComposeResult))
        {
            decision.Result = Outcome::RetainLastFrame;
            return decision;
        }
        // Native ownership means no CPU structured frame was produced for this
        // generation. Publishing anything here would be a stale or
        // mixed-generation output. A backend that already latched a failure is
        // not told again, and with no native renderer there is nothing to tell.
        if (facts.HasNativeRenderer && !facts.RendererHasRuntimeFailure)
        {
            decision.Result = Outcome::FailNativeExact;
            decision.FailureReason =
                "native GPU2D producer could not publish its owned frame";
        }
        else
        {
            decision.Result = Outcome::RetainLastFrame;
        }
        return decision;
    }

    // --- Stage 3: an already-latched runtime failure wins -------------------
    if (facts.HasNativeRenderer && !facts.NativeComposed
        && facts.RendererHasRuntimeFailure)
    {
        decision.Result = Outcome::ReportRuntimeFailure;
        return decision;
    }

    // --- Stage 4: the exact gate refuses any non-native frame ---------------
    if (facts.ExactValidationEnabled && facts.HasNativeRenderer
        && !facts.NativeComposed
        && !IsRetainedFrameResult(facts.ComposeResult))
    {
        decision.Result = Outcome::FailNativeExact;
        decision.FailureReason =
            "native GPU2D exact gate rejected a fallback or unavailable frame";
        return decision;
    }

    // --- Stage 5: announce the Software fallback once -----------------------
    if (facts.HasNativeRenderer && !facts.NativeComposed && !fallbackAnnounced)
    {
        decision.RecordRuntimeNativeUnavailableFallback = true;
        if (facts.CaptureEnabled && !facts.NativeProducer)
            decision.RecordCaptureSoftwareFallback = true;
        decision.CountNativeFallbackFrame = true;
        decision.AnnounceFallback = FallbackReason::NativeFrameUnavailable;
    }

    // --- Stage 6: retained results never reach the structured compositor ----
    if (facts.ComposeResult == GPU2DComposeResult::Backpressure
        || facts.ComposeResult == GPU2DComposeResult::SemanticOnly)
    {
        decision.Result = Outcome::RetainLastFrame;
        return decision;
    }

    decision.Result = facts.NativeComposed
        ? Outcome::NativePublished
        : Outcome::TryStructuredFallback;
    return decision;
}

// The CPU structured compositor may only run when the recorded frame covers
// the whole display. A savestate resume is the one documented exception, and
// only while the drop-discontinuous gate is off.
[[nodiscard]] constexpr bool ShouldComposeStructuredFrame(
    bool viewValid,
    bool completeCoverage,
    bool resumeFrameDiscontinuous,
    bool dropDiscontinuousSavestateFrame) noexcept
{
    return viewValid
        && (completeCoverage
            || (!dropDiscontinuousSavestateFrame && resumeFrameDiscontinuous));
}

} // namespace melonDS::GPU2DFramePolicy

#endif // GPU2D_FRAME_POLICY_H
