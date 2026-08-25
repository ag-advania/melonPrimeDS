/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// Equivalence harness for the backend-neutral GPU2D frame publication policy.
//
// GPU2DFramePolicy::Evaluate() replaced a decision tree that VulkanRenderer and
// DX12Renderer each carried inline. The risk of that extraction is not that it
// fails to compile; it is that one branch of a nine-input tree changes meaning
// and only shows up as a wrong frame on one machine, months later.
//
// So this file carries a second implementation: a direct transcription of the
// pre-refactor inline branches, written to read like the original rather than
// like the extraction. Both are then run over the complete cross-product of
// inputs -- every boolean combination against every compose result -- and every
// field of the decision must match. A divergence in any single one of the 1280
// cases fails the build.

#include <cstdio>
#include <cstring>

#include "GPU2DFramePolicy.h"

using melonDS::GPU2DComposeResult;
namespace Policy = melonDS::GPU2DFramePolicy;

namespace
{

int gFailures = 0;

// --- The oracle -------------------------------------------------------------
//
// Transcribed from VulkanRenderer::VBlank() / DX12Renderer::VBlank() as they
// stood before the extraction. Deliberately keeps the original's shape,
// including the redundant `!nativeComposed` tests and the stage ordering.

Policy::Decision LegacyEvaluate(const Policy::FrameFacts& f)
{
    Policy::Decision d;

    const bool retained =
        f.ComposeResult == GPU2DComposeResult::Backpressure
        || f.ComposeResult == GPU2DComposeResult::SemanticOnly
        || f.ComposeResult == GPU2DComposeResult::Unavailable;

    bool fallbackAnnounced = f.FallbackAlreadyAnnounced;

    if (f.HasNativeRenderer && f.HasNativeFrameForCurrentEmulatedFrame)
    {
        if (f.NativeComposed)
        {
            d.AnnounceNativeSuccess = true;
        }
        else if (!f.NativeProducer
            || f.ComposeResult == GPU2DComposeResult::Fatal)
        {
            d.RecordRuntimeNativeUnavailableFallback = true;
            d.CountNativeFallbackFrame = true;
            if (!fallbackAnnounced)
            {
                d.AnnounceFallback = Policy::FallbackReason::NativeDispatchUnavailable;
                fallbackAnnounced = true;
            }
        }
    }
    else if (f.HasNativeRenderer && f.HasNativeFrame)
    {
        d.RecordStaleGenerationReject = true;
    }

    if (f.NativeProducer && !f.NativeComposed)
    {
        if (retained)
        {
            d.Result = Policy::Outcome::RetainLastFrame;
            return d;
        }
        if (f.HasNativeRenderer && !f.RendererHasRuntimeFailure)
        {
            d.Result = Policy::Outcome::FailNativeExact;
            d.FailureReason =
                "native GPU2D producer could not publish its owned frame";
            return d;
        }
        d.Result = Policy::Outcome::RetainLastFrame;
        return d;
    }

    if (f.HasNativeRenderer && !f.NativeComposed && f.RendererHasRuntimeFailure)
    {
        d.Result = Policy::Outcome::ReportRuntimeFailure;
        return d;
    }

    if (f.ExactValidationEnabled && f.HasNativeRenderer && !f.NativeComposed
        && !retained)
    {
        d.Result = Policy::Outcome::FailNativeExact;
        d.FailureReason =
            "native GPU2D exact gate rejected a fallback or unavailable frame";
        return d;
    }

    if (f.HasNativeRenderer && !f.NativeComposed && !fallbackAnnounced)
    {
        d.RecordRuntimeNativeUnavailableFallback = true;
        if (f.CaptureEnabled && !f.NativeProducer)
            d.RecordCaptureSoftwareFallback = true;
        d.CountNativeFallbackFrame = true;
        d.AnnounceFallback = Policy::FallbackReason::NativeFrameUnavailable;
    }

    if (f.ComposeResult == GPU2DComposeResult::Backpressure
        || f.ComposeResult == GPU2DComposeResult::SemanticOnly)
    {
        d.Result = Policy::Outcome::RetainLastFrame;
        return d;
    }

    d.Result = f.NativeComposed
        ? Policy::Outcome::NativePublished
        : Policy::Outcome::TryStructuredFallback;
    return d;
}

bool SameReason(const char* a, const char* b)
{
    if (a == nullptr || b == nullptr)
        return a == b;
    return std::strcmp(a, b) == 0;
}

bool Same(const Policy::Decision& a, const Policy::Decision& b)
{
    return a.Result == b.Result
        && SameReason(a.FailureReason, b.FailureReason)
        && a.AnnounceNativeSuccess == b.AnnounceNativeSuccess
        && a.RecordStaleGenerationReject == b.RecordStaleGenerationReject
        && a.RecordRuntimeNativeUnavailableFallback
            == b.RecordRuntimeNativeUnavailableFallback
        && a.RecordCaptureSoftwareFallback == b.RecordCaptureSoftwareFallback
        && a.CountNativeFallbackFrame == b.CountNativeFallbackFrame
        && a.AnnounceFallback == b.AnnounceFallback;
}

const char* OutcomeName(Policy::Outcome outcome)
{
    switch (outcome)
    {
    case Policy::Outcome::NativePublished: return "NativePublished";
    case Policy::Outcome::RetainLastFrame: return "RetainLastFrame";
    case Policy::Outcome::FailNativeExact: return "FailNativeExact";
    case Policy::Outcome::ReportRuntimeFailure: return "ReportRuntimeFailure";
    case Policy::Outcome::TryStructuredFallback: return "TryStructuredFallback";
    }
    return "?";
}

void Check(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    ++gFailures;
}

void RunCrossProduct()
{
    static const GPU2DComposeResult kResults[] = {
        GPU2DComposeResult::Success,
        GPU2DComposeResult::SemanticOnly,
        GPU2DComposeResult::Backpressure,
        GPU2DComposeResult::Unavailable,
        GPU2DComposeResult::Fatal,
    };

    int cases = 0;
    int diverged = 0;
    for (unsigned bits = 0; bits < 256u; ++bits)
    {
        for (const GPU2DComposeResult result : kResults)
        {
            Policy::FrameFacts facts;
            facts.HasNativeRenderer = (bits & 0x01u) != 0;
            facts.HasNativeFrameForCurrentEmulatedFrame = (bits & 0x02u) != 0;
            facts.HasNativeFrame = (bits & 0x04u) != 0;
            facts.NativeProducer = (bits & 0x08u) != 0;
            facts.ExactValidationEnabled = (bits & 0x10u) != 0;
            facts.RendererHasRuntimeFailure = (bits & 0x20u) != 0;
            facts.CaptureEnabled = (bits & 0x40u) != 0;
            facts.FallbackAlreadyAnnounced = (bits & 0x80u) != 0;
            facts.ComposeResult = result;

            // NativeComposed is not free: it can only be true where a compose
            // was actually attempted, and only for a result that published.
            for (int composedBit = 0; composedBit < 2; ++composedBit)
            {
                const bool composed = composedBit != 0;
                if (composed
                    && !(facts.HasNativeRenderer
                        && facts.HasNativeFrameForCurrentEmulatedFrame))
                {
                    continue;
                }
                facts.NativeComposed = composed;

                ++cases;
                const Policy::Decision actual = Policy::Evaluate(facts);
                const Policy::Decision expected = LegacyEvaluate(facts);
                if (Same(actual, expected))
                    continue;

                ++diverged;
                if (diverged <= 8)
                {
                    std::printf(
                        "FAIL: divergence bits=0x%02X result=%d composed=%d "
                        "actual=%s expected=%s\n",
                        bits,
                        static_cast<int>(result),
                        composed ? 1 : 0,
                        OutcomeName(actual.Result),
                        OutcomeName(expected.Result));
                }
            }
        }
    }

    if (diverged != 0)
        gFailures += diverged;
    std::printf(
        "gpu2d-frame-policy: %d cases, %d divergences from the pre-refactor tree\n",
        cases, diverged);
}

// Properties the renderers rely on, stated directly rather than inferred from
// the cross-product.
void RunInvariants()
{
    Check(Policy::IsRetainedFrameResult(GPU2DComposeResult::Backpressure)
        && Policy::IsRetainedFrameResult(GPU2DComposeResult::SemanticOnly)
        && Policy::IsRetainedFrameResult(GPU2DComposeResult::Unavailable),
        "backpressure/semantic-only/unavailable retain the last frame");
    Check(!Policy::IsRetainedFrameResult(GPU2DComposeResult::Fatal)
        && !Policy::IsRetainedFrameResult(GPU2DComposeResult::Success),
        "fatal and success are not retained results");

    Check(Policy::ShouldPublishCaptureProvenance(GPU2DComposeResult::Success)
        && Policy::ShouldPublishCaptureProvenance(GPU2DComposeResult::SemanticOnly),
        "capture provenance is published after semantic submission");
    Check(!Policy::ShouldPublishCaptureProvenance(GPU2DComposeResult::Backpressure)
        && !Policy::ShouldPublishCaptureProvenance(GPU2DComposeResult::Unavailable)
        && !Policy::ShouldPublishCaptureProvenance(GPU2DComposeResult::Fatal),
        "capture provenance is not published without a semantic submission");

    Check(!Policy::ShouldAttemptNativeCompose(false, true)
        && !Policy::ShouldAttemptNativeCompose(true, false)
        && Policy::ShouldAttemptNativeCompose(true, true),
        "native compose needs both a renderer and a current-generation frame");

    // Backpressure must never become a Software hybrid frame: that is the
    // exact regression the retained-result grouping exists to prevent.
    {
        Policy::FrameFacts facts;
        facts.HasNativeRenderer = true;
        facts.HasNativeFrameForCurrentEmulatedFrame = true;
        facts.NativeProducer = true;
        facts.ComposeResult = GPU2DComposeResult::Backpressure;
        const Policy::Decision d = Policy::Evaluate(facts);
        Check(d.Result == Policy::Outcome::RetainLastFrame,
            "native-owned backpressure retains the last frame");
        Check(d.AnnounceFallback == Policy::FallbackReason::None
            && !d.RecordRuntimeNativeUnavailableFallback,
            "native-owned backpressure records no fallback");
    }

    // A native producer that could not publish anything is a refusal, not a
    // silent switch to a frame nobody rendered.
    {
        Policy::FrameFacts facts;
        facts.HasNativeRenderer = true;
        facts.HasNativeFrameForCurrentEmulatedFrame = true;
        facts.NativeProducer = true;
        facts.ComposeResult = GPU2DComposeResult::Fatal;
        const Policy::Decision d = Policy::Evaluate(facts);
        Check(d.Result == Policy::Outcome::FailNativeExact,
            "a fatal native-owned frame fails the exact gate");
        Check(d.FailureReason != nullptr, "the refusal carries a reason");
    }

    // The exact-validation gate refuses a Software fallback frame outright.
    {
        Policy::FrameFacts facts;
        facts.HasNativeRenderer = true;
        facts.ExactValidationEnabled = true;
        facts.ComposeResult = GPU2DComposeResult::Success;
        const Policy::Decision d = Policy::Evaluate(facts);
        Check(d.Result == Policy::Outcome::FailNativeExact,
            "exact validation rejects an unpublished native frame");
    }

    // Structured composition needs full coverage, with the documented
    // savestate-resume exception.
    Check(Policy::ShouldComposeStructuredFrame(true, true, false, false),
        "a complete structured frame composes");
    Check(!Policy::ShouldComposeStructuredFrame(false, true, false, false),
        "an invalid structured view never composes");
    Check(Policy::ShouldComposeStructuredFrame(true, false, true, false),
        "a discontinuous resume frame composes while the drop gate is off");
    Check(!Policy::ShouldComposeStructuredFrame(true, false, true, true),
        "the drop gate suppresses the discontinuous resume frame");
    Check(!Policy::ShouldComposeStructuredFrame(true, false, false, false),
        "an incomplete non-resume frame never composes");
}

} // namespace

int main()
{
    RunCrossProduct();
    RunInvariants();

    if (gFailures != 0)
    {
        std::printf("gpu2d-frame-policy-tests: FAIL (%d)\n", gFailures);
        return 1;
    }
    std::printf("gpu2d-frame-policy-tests: PASS\n");
    return 0;
}
