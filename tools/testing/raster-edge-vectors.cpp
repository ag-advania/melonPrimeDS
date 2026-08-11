/*
    GPU-independent executable vectors for the DS raster edge contract.

    These call the same constexpr helpers used by GPU3D_Soft and by the CPU
    setup stages of OpenGL Compute, Vulkan and DX12. The shader compile/source
    audits separately require the cross-language mirrors at the corresponding
    InterpSpans point.
*/

#include <cstdio>

#include "GPU3D_RasterEdge.h"

namespace
{

int Failures = 0;

void Expect(const char* name, bool condition)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++Failures;
    }
}

} // namespace

int main()
{
    using namespace melonDS::RasterEdge;

    // V1: an ordinary right vertical edge is moved one pixel left.
    Expect("V1 ordinary right vertical",
        AdjustRightVertical(0x20000, 0, 10, 5) == 4);

    // V2: the leftmost screen pixel must never become -1.
    Expect("V2 right vertical at x=0",
        AdjustRightVertical(0x20000, 0, 10, 0) == 0);

    // V3: two coincident vertical edges stay coincident.
    Expect("V3 coincident vertical edges",
        AdjustRightVertical(0, 0, 10, 10) == 10);

    // V4: a one-scanline vertical edge is not the 45-degree special case.
    Expect("V4 one-scanline vertical increment",
        CalculateSlopeIncrement(10, 10, 10, 10, 20, 21) == 0);
    Expect("V4 control 45-degree increment",
        CalculateSlopeIncrement(10, 11, 10, 10, 20, 21) == 0x40000);
    Expect("V4 right 45-degree interpolation origin",
        InterpolationOriginOffset(0x40000, true, false) == 1);
    Expect("V4 left negative 45-degree interpolation origin",
        InterpolationOriginOffset(0x40000, false, true) == 1);
    Expect("V4 inside 45-degree interpolation origin",
        InterpolationOriginOffset(0x40000, false, false) == 0);
    Expect("V4 right X-major interpolation origin",
        InterpolationOriginOffset(0x40001, true, false) == 1);

    // V5: Software inverts AA coverage for a swapped vertical edge.
    Expect("V5 swapped vertical AA coverage",
        CalculateYMajorCoverage(0, 0, false, true, true) == 0);
    Expect("V5 unswapped vertical AA coverage",
        CalculateYMajorCoverage(0, 0, false, true, false) == 31);

    // V6: the bottom X-major exception applies only beside a non-flat bottom.
    Expect("V6 bottom non-flat edge",
        IsBottomNonFlatEdge(9, 10, 4, 8));
    Expect("V6 flat bottom exclusion",
        !IsBottomNonFlatEdge(9, 10, 8, 8));
    Expect("V6 non-bottom exclusion",
        !IsBottomNonFlatEdge(8, 10, 4, 8));

    if (Failures != 0)
        return 1;

    std::puts("PASS: raster edge vectors V1-V6");
    return 0;
}
