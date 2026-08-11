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

#ifndef GPU3D_RASTER_EDGE_H
#define GPU3D_RASTER_EDGE_H

#include "types.h"

namespace melonDS::RasterEdge
{

// Canonical DS slope setup mirrored from the upstream Software renderer and
// used by MelonPrime's compute-renderer CPU setup stages. Keeping Software
// itself untouched reduces conflicts when pulling upstream renderer updates.
// The xlen != 1 exception is observable for a one-scanline vertical edge: it
// is vertical (Increment == 0), not a 45-degree edge (Increment == 0x40000).
constexpr s32 CalculateSlopeIncrement(
    s32 x0, s32 x1, s32 xMin, s32 xMax, s32 y0, s32 y1) noexcept
{
    const s32 xlen = xMax + 1 - xMin;
    const s32 ylen = y1 - y0;
    if (ylen == 0)
        return 0;
    if (ylen == xlen && xlen != 1)
        return 0x40000;

    const s32 yrecip = (1 << 18) / ylen;
    s32 increment = (x1 - x0) * yrecip;
    return increment < 0 ? -increment : increment;
}

// GPU3D_Soft.cpp::RenderPolygonScanline() applies this after both current X
// values have been calculated and before testing whether the edges swapped.
// Compute shaders mirror this helper at the same point in their pipeline.
constexpr bool ShouldDecrementRightVertical(
    s32 leftIncrement, s32 rightIncrement, s32 leftX, s32 rightX) noexcept
{
    return rightIncrement == 0
        && (leftIncrement != 0 || leftX != rightX)
        && rightX != 0;
}

constexpr s32 AdjustRightVertical(
    s32 leftIncrement, s32 rightIncrement, s32 leftX, s32 rightX) noexcept
{
    return ShouldDecrementRightVertical(leftIncrement, rightIncrement, leftX, rightX)
        ? rightX - 1
        : rightX;
}

// Span setup does not yet have the opposite edge, so it cannot decide whether
// the conditional decrement applies. Its polygon bounds may conservatively
// include the possible pixel to the left; the actual span coordinate remains
// untouched until both edges meet in InterpSpans.
constexpr s32 ConservativeRightVerticalMin(s32 x, bool rightSide) noexcept
{
    return rightSide && x > 0 ? x - 1 : x;
}

// Software always interpolates edge attributes in scanline-Y space. It moves
// that interpolation origin back by one sample on the outside half of 45-degree
// and X-major edges.
constexpr s32 InterpolationOriginOffset(
    s32 increment, bool rightSide, bool negative) noexcept
{
    return increment >= 0x40000 && (rightSide != negative) ? 1 : 0;
}

constexpr s32 CalculateYMajorCoverage(
    s32 increment, s32 dx, bool negative, bool rightSide, bool swapped) noexcept
{
    if (increment == 0)
        return swapped ? 0 : 31;

    s32 coverage = ((dx >> 9) + (increment >> 10)) >> 4;
    if ((coverage >> 5) != (dx >> 18))
        coverage = 31;
    coverage &= 0x1F;
    if (swapped ? (rightSide != negative) : (rightSide == negative))
        coverage = 0x1F - coverage;
    return coverage;
}

constexpr bool IsBottomNonFlatEdge(
    s32 y, s32 polygonYBottom, s32 leftNextX, s32 rightNextX) noexcept
{
    return y == polygonYBottom - 1 && leftNextX != rightNextX;
}

} // namespace melonDS::RasterEdge

#endif // GPU3D_RASTER_EDGE_H
