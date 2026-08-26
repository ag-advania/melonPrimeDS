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

#ifndef STRUCTURED_UPLOAD_PLAN_H
#define STRUCTURED_UPLOAD_PLAN_H

#include <array>
#include <cstddef>

#include "MelonPrimeStructuredComposition.h"

namespace melonDS::StructuredComposition
{

// Which byte ranges of a compositor's structured input buffer have to be
// re-uploaded this frame.
//
// The structured input is one buffer holding, in order: fourteen 2D planes,
// two line-metadata blocks, and the capture command stream. Each of those
// seventeen logical units carries its own content generation, so a frame that
// changed only one plane should re-upload only that plane -- and adjacent
// dirty units are coalesced so the result is a handful of copies rather than
// seventeen.
//
// Both native backends computed this identically, down to the coalescing loop;
// only `VkDeviceSize` versus `u64` differed. It is not a graphics question:
// getting it wrong leaves stale 2D content on screen, silently, for exactly
// the units whose generation comparison was wrong.
//
// Nothing here touches a GPU handle -- it produces offsets and sizes, and the
// backend issues the copies.

inline constexpr u32 UploadUnitCount =
    kStructuredInputPlaneCount + kStructuredInputLineMetaCount + 1u;
static_assert(UploadUnitCount == 17u,
    "the upload unit layout is 14 planes, 2 line-metadata blocks and the "
    "capture command stream");

// Index of the capture command stream, the one unit whose dirtiness does not
// follow a single generation field.
inline constexpr u32 CaptureCommandUnit = UploadUnitCount - 1u;

struct StructuredUploadRange
{
    u64 Offset = 0;
    u64 Size = 0;
};

struct StructuredUploadPlan
{
    // The buffer layout: where each unit starts and how long it is. The
    // caller packs into the staging buffer at these offsets, so it must not
    // recompute them -- a layout that disagrees with the copy ranges writes
    // one unit and uploads another.
    std::array<u64, UploadUnitCount> UnitOffsets{};
    std::array<u64, UploadUnitCount> UnitSizes{};
    // Per-unit dirtiness, kept alongside the coalesced ranges: the caller
    // packs unit by unit but copies run by run, and deriving one from the
    // other after the fact is how the two can disagree.
    std::array<bool, UploadUnitCount> Dirty{};
    std::array<StructuredUploadRange, UploadUnitCount> Ranges{};
    std::size_t RangeCount = 0;

    [[nodiscard]] bool Required() const noexcept { return RangeCount != 0; }
};

// `uploadInitialized` false means this slot has never been written, so every
// unit is dirty regardless of generations.
[[nodiscard]] inline StructuredUploadPlan BuildStructuredUploadPlan(
    const GenerationState& current,
    const GenerationState& uploaded,
    bool uploadInitialized,
    u64 planeBytes,
    u64 lineMetaBytes,
    u64 captureCommandBytes) noexcept
{
    StructuredUploadPlan plan;
    std::array<u64, UploadUnitCount>& unitOffsets = plan.UnitOffsets;
    std::array<u64, UploadUnitCount>& unitSizes = plan.UnitSizes;
    for (u32 unit = 0; unit < kStructuredInputPlaneCount; ++unit)
    {
        unitOffsets[unit] = static_cast<u64>(unit) * planeBytes;
        unitSizes[unit] = planeBytes;
    }
    unitOffsets[14u] = static_cast<u64>(kStructuredInputPlaneCount) * planeBytes;
    unitOffsets[15u] = unitOffsets[14u] + lineMetaBytes;
    unitOffsets[16u] = unitOffsets[15u] + lineMetaBytes;
    unitSizes[14u] = lineMetaBytes;
    unitSizes[15u] = lineMetaBytes;
    unitSizes[16u] = captureCommandBytes;

    const bool fullUpload = !uploadInitialized;
    std::array<bool, UploadUnitCount>& dirty = plan.Dirty;
    for (u32 plane = 0; plane < kStructuredInputPlaneCount; ++plane)
        dirty[plane] = fullUpload || current.Plane[plane] != uploaded.Plane[plane];
    dirty[14u] = fullUpload || current.LineMeta[0] != uploaded.LineMeta[0];
    dirty[15u] = fullUpload || current.LineMeta[1] != uploaded.LineMeta[1];

    // The capture command stream is classified from the capture-source planes
    // as well as its own generation, so a source plane changing underneath an
    // unchanged command stream still re-uploads it.
    dirty[CaptureCommandUnit] = fullUpload
        || current.CaptureCommands != uploaded.CaptureCommands
        || current.Plane[3u] != uploaded.Plane[3u]
        || current.Plane[7u] != uploaded.Plane[7u]
        || current.Plane[13u] != uploaded.Plane[13u];

    for (u32 unit = 0; unit < UploadUnitCount; ++unit)
    {
        if (!dirty[unit])
            continue;
        const u64 offset = unitOffsets[unit];
        const u64 size = unitSizes[unit];
        if (plan.RangeCount != 0
            && plan.Ranges[plan.RangeCount - 1].Offset
                    + plan.Ranges[plan.RangeCount - 1].Size == offset)
        {
            plan.Ranges[plan.RangeCount - 1].Size += size;
        }
        else
        {
            plan.Ranges[plan.RangeCount++] = {offset, size};
        }
    }
    return plan;
}

} // namespace melonDS::StructuredComposition

#endif // STRUCTURED_UPLOAD_PLAN_H
