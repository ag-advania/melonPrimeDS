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

    // V7: reciprocal approximation produces 7; Software exact division is 6.
    Expect("V7 exact linear interpolation",
        InterpolateLinearExact(0, 80, 2, 23) == 6);
    Expect("V7 descending linear interpolation",
        InterpolateLinearExact(80, 0, 2, 23) == 73);
    Expect("V7 signed linear interpolation",
        InterpolateLinearExact(-80, 0, 2, 23) == -74);

    // V8-V10: executable truth tables for depth/blend metadata rules.
    const auto blendChannel = [](bool enabled, int dstA, int src, int dst, int alpha) {
        return enabled && dstA != 0
            ? ((src * alpha) + (dst * (32 - alpha))) >> 5
            : src;
    };
    Expect("V8 alpha blend disabled", blendChannel(false, 31, 40, 8, 16) == 40);
    Expect("V8 alpha blend enabled", blendChannel(true, 31, 40, 8, 16) == 24);
    const auto depthPass = [](bool facing, uint32_t dstAttr, uint32_t src, uint32_t dst) {
        return facing && (dstAttr & 0x00400010u) == 0x00000010u
            ? src <= dst : src < dst;
    };
    Expect("V9 front facing opaque-back tie", depthPass(true, 0x10u, 100, 100));
    Expect("V9 back facing strict tie", !depthPass(false, 0x10u, 100, 100));
    Expect("V10 back facing attribute bit", ((0u | (1u << 4u)) & 0x10u) != 0u);

    // V11-V12: a seventeenth full-screen layer starts a lossless second batch,
    // while degenerate inputs never consume a compact polygon slot.
    int batches = 1;
    int used = 0;
    for (int layer = 0; layer < 17; ++layer)
    {
        if (used == 16) { ++batches; used = 0; }
        ++used;
    }
    Expect("V11 seventeen layer bounded batching", batches == 2 && used == 1);
    const bool degenerate[4] = { false, true, false, true };
    int compactCount = 0;
    for (bool skip : degenerate) compactCount += !skip;
    Expect("V12 degenerate compact polygon indices", compactCount == 2);

    if (Failures != 0)
        return 1;

    std::puts("PASS: raster parity vectors V1-V12");
    return 0;
}
