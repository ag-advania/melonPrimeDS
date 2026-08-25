/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// Equivalence and property tests for the structured input upload plan.
//
// This decides which of the seventeen logical units of a compositor's
// structured input buffer are re-uploaded each frame, and coalesces the dirty
// ones into copy runs. A unit wrongly considered clean leaves last frame's 2D
// content on screen for that plane -- silently, with no error anywhere.
//
// As with the frame publication policy, the extraction is checked against a
// transcription of the pre-extraction inline computation rather than against
// its own restatement.

#include <array>
#include <cstdio>
#include <cstddef>
#include <vector>

#include "StructuredUploadPlan.h"

namespace Plan = melonDS::StructuredComposition;
using melonDS::u32;
using melonDS::u64;

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

// --- The oracle -------------------------------------------------------------
//
// Transcribed from ComposeStructuredOutput() as it stood before the
// extraction, keeping the original's shape.

struct LegacyRange
{
    u64 Offset = 0;
    u64 Size = 0;
};

struct LegacyResult
{
    std::array<bool, Plan::UploadUnitCount> Dirty{};
    std::array<u64, Plan::UploadUnitCount> UnitOffsets{};
    std::array<u64, Plan::UploadUnitCount> UnitSizes{};
    std::array<LegacyRange, Plan::UploadUnitCount> Ranges{};
    std::size_t RangeCount = 0;
};

LegacyResult LegacyBuild(
    const Plan::GenerationState& contentGeneration,
    const Plan::GenerationState& uploadedContentGeneration,
    bool structuredUploadInitialized,
    u64 planeBytes,
    u64 lineMetaBytes,
    u64 captureCommandBytes)
{
    constexpr u32 logicalUnitCount = Plan::UploadUnitCount;
    LegacyResult out;
    auto& unitOffsets = out.UnitOffsets;
    auto& unitSizes = out.UnitSizes;
    for (u32 unit = 0; unit < 14u; ++unit)
    {
        unitOffsets[unit] = static_cast<u64>(unit) * planeBytes;
        unitSizes[unit] = planeBytes;
    }
    unitOffsets[14u] = 14u * planeBytes;
    unitOffsets[15u] = unitOffsets[14u] + lineMetaBytes;
    unitOffsets[16u] = unitOffsets[15u] + lineMetaBytes;
    unitSizes[14u] = lineMetaBytes;
    unitSizes[15u] = lineMetaBytes;
    unitSizes[16u] = captureCommandBytes;

    auto& dirty = out.Dirty;
    const bool fullUpload = !structuredUploadInitialized;
    for (u32 plane = 0; plane < 14u; ++plane)
    {
        dirty[plane] = fullUpload
            || contentGeneration.Plane[plane]
                != uploadedContentGeneration.Plane[plane];
    }
    dirty[14u] = fullUpload
        || contentGeneration.LineMeta[0] != uploadedContentGeneration.LineMeta[0];
    dirty[15u] = fullUpload
        || contentGeneration.LineMeta[1] != uploadedContentGeneration.LineMeta[1];
    const bool captureClassificationDirty = fullUpload
        || contentGeneration.CaptureCommands
            != uploadedContentGeneration.CaptureCommands
        || contentGeneration.Plane[3u] != uploadedContentGeneration.Plane[3u]
        || contentGeneration.Plane[7u] != uploadedContentGeneration.Plane[7u]
        || contentGeneration.Plane[13u] != uploadedContentGeneration.Plane[13u];
    dirty[16u] = captureClassificationDirty;

    for (u32 unit = 0; unit < logicalUnitCount; ++unit)
    {
        if (!dirty[unit])
            continue;
        const u64 offset = unitOffsets[unit];
        const u64 size = unitSizes[unit];
        if (out.RangeCount != 0
            && out.Ranges[out.RangeCount - 1].Offset
                    + out.Ranges[out.RangeCount - 1].Size == offset)
        {
            out.Ranges[out.RangeCount - 1].Size += size;
        }
        else
        {
            out.Ranges[out.RangeCount++] = {offset, size};
        }
    }
    return out;
}

bool Same(const Plan::StructuredUploadPlan& a, const LegacyResult& b)
{
    if (a.RangeCount != b.RangeCount)
        return false;
    for (u32 unit = 0; unit < Plan::UploadUnitCount; ++unit)
    {
        if (a.Dirty[unit] != b.Dirty[unit])
            return false;
        if (a.UnitOffsets[unit] != b.UnitOffsets[unit])
            return false;
        if (a.UnitSizes[unit] != b.UnitSizes[unit])
            return false;
    }
    for (std::size_t i = 0; i < b.RangeCount; ++i)
    {
        if (a.Ranges[i].Offset != b.Ranges[i].Offset)
            return false;
        if (a.Ranges[i].Size != b.Ranges[i].Size)
            return false;
    }
    return true;
}

// The real byte sizes both backends pass.
constexpr u64 kPlaneBytes = 256ull * 192ull * sizeof(u32);
constexpr u64 kLineMetaBytes = 192ull * sizeof(u32);
constexpr u64 kCaptureCommandBytes =
    192ull * Plan::kCaptureCommandWords * sizeof(u32);

// A deterministic generator over the generation fields. Exhausting 17
// independent counters is not feasible, so this walks a wide spread of
// single-unit, adjacent-pair and scattered dirty patterns instead -- adjacency
// being what the coalescing loop is sensitive to.
void RunEquivalence()
{
    int cases = 0;
    int diverged = 0;

    for (unsigned pattern = 0; pattern < (1u << 17); pattern += 7u)
    {
        for (int initialized = 0; initialized < 2; ++initialized)
        {
            Plan::GenerationState uploaded{};
            Plan::GenerationState current{};
            for (u32 plane = 0; plane < 14u; ++plane)
            {
                uploaded.Plane[plane] = 100u + plane;
                current.Plane[plane] = uploaded.Plane[plane]
                    + ((pattern >> plane) & 1u);
            }
            uploaded.LineMeta[0] = 200u;
            uploaded.LineMeta[1] = 201u;
            current.LineMeta[0] = uploaded.LineMeta[0] + ((pattern >> 14u) & 1u);
            current.LineMeta[1] = uploaded.LineMeta[1] + ((pattern >> 15u) & 1u);
            uploaded.CaptureCommands = 300u;
            current.CaptureCommands =
                uploaded.CaptureCommands + ((pattern >> 16u) & 1u);

            ++cases;
            const Plan::StructuredUploadPlan actual = Plan::BuildStructuredUploadPlan(
                current, uploaded, initialized != 0,
                kPlaneBytes, kLineMetaBytes, kCaptureCommandBytes);
            const LegacyResult expected = LegacyBuild(
                current, uploaded, initialized != 0,
                kPlaneBytes, kLineMetaBytes, kCaptureCommandBytes);
            if (Same(actual, expected))
                continue;
            ++diverged;
            if (diverged <= 5)
            {
                std::printf("FAIL: divergence pattern=0x%05X initialized=%d\n",
                    pattern, initialized);
            }
        }
    }

    gFailures += diverged;
    std::printf(
        "structured-upload-plan: %d cases, %d divergences from the "
        "pre-refactor computation\n", cases, diverged);
}

void RunProperties()
{
    Plan::GenerationState uploaded{};
    for (u32 plane = 0; plane < 14u; ++plane)
        uploaded.Plane[plane] = 100u + plane;
    uploaded.LineMeta[0] = 200u;
    uploaded.LineMeta[1] = 201u;
    uploaded.CaptureCommands = 300u;

    // Nothing changed and the slot has been written before: no copies at all.
    {
        const auto plan = Plan::BuildStructuredUploadPlan(
            uploaded, uploaded, true,
            kPlaneBytes, kLineMetaBytes, kCaptureCommandBytes);
        Check(!plan.Required(), "an unchanged frame uploads nothing");
        Check(plan.RangeCount == 0, "an unchanged frame produces no ranges");
    }

    // A slot that has never been written uploads everything, and because the
    // units are laid out contiguously that collapses to a single copy.
    {
        const auto plan = Plan::BuildStructuredUploadPlan(
            uploaded, uploaded, false,
            kPlaneBytes, kLineMetaBytes, kCaptureCommandBytes);
        Check(plan.Required(), "an uninitialized slot uploads");
        Check(plan.RangeCount == 1,
            "a full upload coalesces into one contiguous copy");
        Check(plan.Ranges[0].Offset == 0, "the full copy starts at zero");
        const u64 total = 14u * kPlaneBytes + 2u * kLineMetaBytes
            + kCaptureCommandBytes;
        Check(plan.Ranges[0].Size == total, "the full copy covers every unit");
        for (u32 unit = 0; unit < Plan::UploadUnitCount; ++unit)
            Check(plan.Dirty[unit], "every unit is dirty on a full upload");
    }

    // One isolated plane changed: one copy, exactly that plane's extent.
    {
        Plan::GenerationState current = uploaded;
        current.Plane[5u] += 1u;
        const auto plan = Plan::BuildStructuredUploadPlan(
            current, uploaded, true,
            kPlaneBytes, kLineMetaBytes, kCaptureCommandBytes);
        Check(plan.RangeCount == 1, "one changed plane is one copy");
        Check(plan.Ranges[0].Offset == 5u * kPlaneBytes,
            "the copy starts at that plane");
        Check(plan.Ranges[0].Size == kPlaneBytes, "the copy is one plane long");
    }

    // Two adjacent planes changed: still one copy, because coalescing is the
    // whole point of the range list.
    {
        Plan::GenerationState current = uploaded;
        current.Plane[5u] += 1u;
        current.Plane[6u] += 1u;
        const auto plan = Plan::BuildStructuredUploadPlan(
            current, uploaded, true,
            kPlaneBytes, kLineMetaBytes, kCaptureCommandBytes);
        Check(plan.RangeCount == 1, "adjacent planes coalesce into one copy");
        Check(plan.Ranges[0].Size == 2u * kPlaneBytes,
            "the coalesced copy spans both planes");
    }

    // Two separated planes: two copies, not one spanning the clean plane
    // between them.
    {
        Plan::GenerationState current = uploaded;
        current.Plane[5u] += 1u;
        current.Plane[8u] += 1u;
        const auto plan = Plan::BuildStructuredUploadPlan(
            current, uploaded, true,
            kPlaneBytes, kLineMetaBytes, kCaptureCommandBytes);
        Check(plan.RangeCount == 2, "separated planes stay separate copies");
        Check(plan.Ranges[0].Offset == 5u * kPlaneBytes
            && plan.Ranges[1].Offset == 8u * kPlaneBytes,
            "each copy starts at its own plane");
    }

    // The capture command stream is re-uploaded when a capture *source* plane
    // changes, even though its own generation did not. This is the asymmetry
    // that is easiest to lose in a rewrite.
    for (const u32 sourcePlane : {3u, 7u, 13u})
    {
        Plan::GenerationState current = uploaded;
        current.Plane[sourcePlane] += 1u;
        const auto plan = Plan::BuildStructuredUploadPlan(
            current, uploaded, true,
            kPlaneBytes, kLineMetaBytes, kCaptureCommandBytes);
        Check(plan.Dirty[Plan::CaptureCommandUnit],
            "a capture source plane re-uploads the capture command stream");
    }

    // A non-source plane must not drag the command stream along with it.
    {
        Plan::GenerationState current = uploaded;
        current.Plane[4u] += 1u;
        const auto plan = Plan::BuildStructuredUploadPlan(
            current, uploaded, true,
            kPlaneBytes, kLineMetaBytes, kCaptureCommandBytes);
        Check(!plan.Dirty[Plan::CaptureCommandUnit],
            "an unrelated plane leaves the capture command stream alone");
    }

    // The unit layout must be contiguous and gap-free, because the packing
    // path writes at these offsets while the copy path uses the ranges.
    {
        const auto plan = Plan::BuildStructuredUploadPlan(
            uploaded, uploaded, false,
            kPlaneBytes, kLineMetaBytes, kCaptureCommandBytes);
        u64 expected = 0;
        bool contiguous = true;
        for (u32 unit = 0; unit < Plan::UploadUnitCount; ++unit)
        {
            contiguous = contiguous && plan.UnitOffsets[unit] == expected;
            expected += plan.UnitSizes[unit];
        }
        Check(contiguous, "the unit layout has no gaps or overlaps");
    }
}

} // namespace

int main()
{
    RunEquivalence();
    RunProperties();

    if (gFailures != 0)
    {
        std::printf("structured-upload-plan-tests: FAIL (%d)\n", gFailures);
        return 1;
    }
    std::printf("structured-upload-plan-tests: PASS\n");
    return 0;
}
