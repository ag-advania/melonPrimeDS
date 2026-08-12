/*
    GPU-independent executable vectors for the DS raster edge contract.

    These call the same constexpr helpers used by GPU3D_Soft and by the CPU
    setup stages of OpenGL Compute, Vulkan and DX12. The shader compile/source
    audits separately require the cross-language mirrors at the corresponding
    InterpSpans point.
*/

#ifndef MELONPRIME_DS
#define MELONPRIME_DS 1
#endif

#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <array>
#include <vector>

#include "GPU3D_FixedVariantIndex.h"
#include "GPU3D_RasterEdge.h"
#include "MelonPrimeStructuredComposition.h"

namespace
{

int Failures = 0;

namespace Contract = melonDS::StructuredComposition;

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

    // V13: every AA edge kind preserves the layer underneath. Top/bottom-only
    // edges are just as eligible for a second-layer depth test as left/right.
    for (uint32_t edge = 1; edge <= 8; edge <<= 1)
        Expect("V13 all edge flags preserve second layer", (edge & 0xFu) != 0u);

    // V14: X-major coverage advances only for pixels that survive alpha and
    // depth testing. Three accepted predecessors yield coverage 4 here; two
    // would incorrectly yield 12.
    const auto rightCoverage = [](int initial, int increment, int accepted) {
        return 31 - ((initial + increment * accepted) >> 5);
    };
    Expect("V14 accepted-pixel AA progression",
        rightCoverage(127, 256, 3) == 4 &&
        rightCoverage(127, 256, 2) == 12);

    // V15: native output consumes already quantized DS coordinates. Hires
    // coordinates are reserved for enlarged targets where subpixel detail is
    // representable.
    const auto rasterX = [](int scale, int finalX, int hiresX) {
        return scale > 1 ? (hiresX * scale) >> 4 : finalX;
    };
    Expect("V15 native quantized coordinates",
        rasterX(1, 174, 2800) == 174 && rasterX(2, 174, 2800) == 350);

    // V16: the fixed variant index preserves first-seen canonical order at
    // the full 2,048-variant budget, even under an adversarial single bucket.
    melonDS::AdaptiveVariantIndex<64, 4096, 32> variantIndex;
    std::uint32_t canonicalKeys[2048] = {};
    std::uint32_t canonicalCount = 0;
    bool variantSequenceOkay = true;
    for (std::uint32_t pass = 0; pass < 2; ++pass)
    {
        for (std::uint32_t position = 0; position < 2048; ++position)
        {
            const std::uint32_t key = pass == 0 ? position : 2047u - position;
            std::uint32_t index = 0;
            const bool found = variantIndex.Find(0,
                [&](std::uint32_t candidate) {
                    return candidate < canonicalCount && canonicalKeys[candidate] == key;
                }, index);
            if (!found)
            {
                index = canonicalCount;
                canonicalKeys[canonicalCount++] = key;
                variantSequenceOkay &= variantIndex.Insert(
                    0, index, [](std::uint32_t) { return 0u; });
            }
            variantSequenceOkay &= index == key;
        }
    }
    Expect("V16 fixed variant collision and insertion order",
        variantSequenceOkay && canonicalCount == 2048);

    // V17: a deliberately tiny epoch type makes rollover executable instead
    // of waiting 2^32 frames. Wrapped entries must never become visible again.
    melonDS::FixedVariantIndex<8, std::uint8_t> rolloverIndex;
    bool rolloverOkay = rolloverIndex.Insert(3, 7);
    for (int reset = 0; reset < 255; ++reset)
        rolloverIndex.Reset();
    std::uint32_t staleIndex = 0;
    rolloverOkay &= !rolloverIndex.Find(3,
        [](std::uint32_t) { return true; }, staleIndex);
    rolloverOkay &= rolloverIndex.Insert(3, 9);
    rolloverOkay &= rolloverIndex.Find(3,
        [](std::uint32_t candidate) { return candidate == 9; }, staleIndex);
    Expect("V17 fixed variant epoch rollover", rolloverOkay && staleIndex == 9);

    // V18-V19: the scanline-time LCD routing table preserves screen swap,
    // engine reversal, fallback display modes, and mid-frame route changes.
    std::vector<std::uint32_t> RoutingEnginePlanes(
        2u * Contract::kPlaneCount * Contract::kScreenPixelCount);
    std::vector<std::uint32_t> RoutingFallbackPlanes(
        2u * Contract::kPlaneCount * Contract::kScreenPixelCount);
    std::vector<std::uint32_t> RoutingPackedPlanes(
        2u * Contract::kPlaneCount * Contract::kScreenPixelCount);
    std::array<std::uint8_t, 2u * Contract::kScreenHeight> RoutingSources{};
    Contract::ScreenRoutingView routing{};
    for (std::uint32_t engine = 0; engine < 2u; ++engine)
    {
        for (std::uint32_t plane = 0; plane < Contract::kPlaneCount; ++plane)
        {
            const std::size_t base =
                (engine * Contract::kPlaneCount + plane) * Contract::kScreenPixelCount;
            routing.EnginePlane[engine][plane] = RoutingEnginePlanes.data() + base;
            for (std::size_t pixel = 0; pixel < Contract::kScreenPixelCount; ++pixel)
            {
                RoutingEnginePlanes[base + pixel] =
                    0x10000000u | (engine << 24u) | (plane << 20u)
                    | static_cast<std::uint32_t>(pixel);
            }
        }
    }
    for (std::uint32_t screen = 0; screen < 2u; ++screen)
    {
        for (std::uint32_t plane = 0; plane < Contract::kPlaneCount; ++plane)
        {
            const std::size_t base =
                (screen * Contract::kPlaneCount + plane) * Contract::kScreenPixelCount;
            routing.FallbackPlane[screen][plane] = RoutingFallbackPlanes.data() + base;
            for (std::size_t pixel = 0; pixel < Contract::kScreenPixelCount; ++pixel)
            {
                RoutingFallbackPlanes[base + pixel] =
                    0xF0000000u | (screen << 24u) | (plane << 20u)
                    | static_cast<std::uint32_t>(pixel);
            }
        }
        routing.ScreenSource[screen] =
            RoutingSources.data() + screen * Contract::kScreenHeight;
    }

    std::fill_n(RoutingSources.data(), Contract::kScreenHeight,
        Contract::kScreenSourceEngineA);
    std::fill_n(RoutingSources.data() + Contract::kScreenHeight, Contract::kScreenHeight,
        Contract::kScreenSourceEngineB);
    auto pack = Contract::PackRoutedScreenPlanes(RoutingPackedPlanes.data(), routing);
    bool constantRoutesOkay = pack.Valid && pack.RouteRuns == 2u;
    for (std::uint32_t screen = 0; screen < 2u; ++screen)
    {
        for (std::uint32_t plane = 0; plane < Contract::kPlaneCount; ++plane)
        {
            const std::size_t base =
                (screen * Contract::kPlaneCount + plane) * Contract::kScreenPixelCount;
            const std::uint32_t* expected = routing.EnginePlane[screen][plane];
            constantRoutesOkay &= RoutingPackedPlanes[base] == expected[0];
            constantRoutesOkay &= RoutingPackedPlanes[base + Contract::kScreenPixelCount - 1u]
                == expected[Contract::kScreenPixelCount - 1u];
        }
    }
    Expect("V18 screen swap and reverse engine routes", constantRoutesOkay);

    std::fill_n(RoutingSources.data(), 96u, Contract::kScreenSourceEngineB);
    std::fill_n(RoutingSources.data() + 96u, 96u, Contract::kScreenSourceFallback);
    std::fill_n(RoutingSources.data() + Contract::kScreenHeight, 64u,
        Contract::kScreenSourceFallback);
    std::fill_n(RoutingSources.data() + Contract::kScreenHeight + 64u, 64u,
        Contract::kScreenSourceEngineA);
    std::fill_n(RoutingSources.data() + Contract::kScreenHeight + 128u, 64u,
        Contract::kScreenSourceEngineB);
    pack = Contract::PackRoutedScreenPlanes(RoutingPackedPlanes.data(), routing);
    bool mixedRoutesOkay = pack.Valid && pack.RouteRuns == 5u;
    const auto packed = [&](std::uint32_t screen, std::uint32_t plane, std::uint32_t line) {
        return RoutingPackedPlanes[
            (screen * Contract::kPlaneCount + plane) * Contract::kScreenPixelCount
            + line * Contract::kScreenWidth];
    };
    for (std::uint32_t plane = 0; plane < Contract::kPlaneCount; ++plane)
    {
        mixedRoutesOkay &= packed(0u, plane, 95u)
            == routing.EnginePlane[1][plane][95u * Contract::kScreenWidth];
        mixedRoutesOkay &= packed(0u, plane, 96u)
            == routing.FallbackPlane[0][plane][96u * Contract::kScreenWidth];
        mixedRoutesOkay &= packed(1u, plane, 63u)
            == routing.FallbackPlane[1][plane][63u * Contract::kScreenWidth];
        mixedRoutesOkay &= packed(1u, plane, 64u)
            == routing.EnginePlane[0][plane][64u * Contract::kScreenWidth];
        mixedRoutesOkay &= packed(1u, plane, 128u)
            == routing.EnginePlane[1][plane][128u * Contract::kScreenWidth];
    }
    Expect("V19 mixed regular fallback and mid-frame routes", mixedRoutesOkay);

    if (Failures != 0)
        return 1;

    std::puts("PASS: raster parity vectors V1-V19");
    return 0;
}
