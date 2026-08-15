/*
    GPU-independent tests for the conservative CaptureSidecar dependency
    classifier. The classifier is shared by Vulkan and DX12, so these vectors
    cover the safety decision that both backends must make before recording a
    batched dispatch.
*/

#include <array>
#include <cstdio>

#include "MelonPrimeStructuredComposition.h"

namespace
{

using namespace melonDS;
using namespace melonDS::StructuredComposition;

constexpr u32 kCommandCount = kScreenHeight * kCaptureCommandWords;

struct Fixture
{
    std::array<std::array<u32, kScreenPixelCount>, kStructuredInputPlaneCount> Planes{};
    std::array<const u32*, kStructuredInputPlaneCount> PlanePointers{};
    std::array<u32, kCommandCount> Commands{};

    Fixture()
    {
        for (u32 plane = 0; plane < kStructuredInputPlaneCount; ++plane)
            PlanePointers[plane] = Planes[plane].data();
    }

    void SetCommand(
        u32 line,
        u32 captureCount,
        u32 bank,
        u32 version,
        u32 sourceScreen,
        u32 address,
        u32 width)
    {
        const u32 base = line * kCaptureCommandWords;
        Commands[base] = captureCount;
        Commands[base + 1u] = kCaptureCommandValid
            | (bank & 3u)
            | ((version & 1u) << kCaptureCommandDestinationVersionShift)
            | ((sourceScreen & 1u) << kCaptureCommandSourceScreenShift);
        Commands[base + 2u] = address;
        Commands[base + 3u] = width;
    }

    void SetSourceAReference(u32 line, u32 x, u32 bank, u32 version, u32 address)
    {
        Planes[3u][line * kScreenWidth + x] = PackCaptureReference(bank, version, address);
    }
};

bool Require(bool condition, const char* message)
{
    if (condition)
        return true;
    std::fprintf(stderr, "FAIL: %s\n", message);
    return false;
}

bool RunVectorsForScale(u32 scale)
{
    (void)scale;
    bool passed = true;

    // A: no valid capture line.
    {
        static Fixture fixture;
        const CaptureLineAnalysis analysis =
            AnalyzeCaptureDependencies(fixture.PlanePointers, fixture.Commands.data());
        passed = Require(
                     analysis.ValidLineCount == 0
                         && analysis.IndependentLineCount == 0
                         && analysis.LegacyOrderedLineCount == 0,
                     "pattern A classified an invalid frame as capture work")
            && passed;
    }

    // B: one line with a 3D source and no sidecar read.
    {
        static Fixture fixture;
        fixture.SetCommand(12u, 1u << 24u, 0u, 0u, 0u, 0u, 8u);
        const CaptureLineAnalysis analysis =
            AnalyzeCaptureDependencies(fixture.PlanePointers, fixture.Commands.data());
        passed = Require(
                     analysis.ValidLineCount == 1
                         && analysis.IndependentLineCount == 1
                         && analysis.LegacyOrderedLineCount == 0
                         && analysis.Independent[12u] != 0u,
                     "pattern B did not classify a sidecar-independent line")
            && passed;
    }

    // C: consecutive independent lines with disjoint destination ranges.
    {
        static Fixture fixture;
        fixture.SetCommand(3u, 1u << 24u, 0u, 0u, 0u, 0u, 8u);
        fixture.SetCommand(4u, 1u << 24u, 0u, 0u, 0u, 16u, 8u);
        fixture.SetCommand(5u, 1u << 24u, 0u, 0u, 0u, 32u, 8u);
        const CaptureLineAnalysis analysis =
            AnalyzeCaptureDependencies(fixture.PlanePointers, fixture.Commands.data());
        passed = Require(
                     analysis.ValidLineCount == 3
                         && analysis.IndependentLineCount == 3
                         && analysis.LegacyOrderedLineCount == 0
                         && analysis.IndependentLines[0] == 3u
                         && analysis.IndependentLines[1] == 4u
                         && analysis.IndependentLines[2] == 5u,
                     "pattern C did not preserve a consecutive independent run")
            && passed;
    }

    // D: a line reading the previous line's sidecar output must stay ordered.
    {
        static Fixture fixture;
        fixture.SetCommand(20u, 1u << 24u, 0u, 0u, 0u, 64u, 8u);
        fixture.SetCommand(21u, 0u, 0u, 0u, 0u, 128u, 8u);
        fixture.SetSourceAReference(21u, 0u, 0u, 0u, 64u);
        const CaptureLineAnalysis analysis =
            AnalyzeCaptureDependencies(fixture.PlanePointers, fixture.Commands.data());
        passed = Require(
                     analysis.ValidLineCount == 2
                         && analysis.IndependentLineCount == 1
                         && analysis.LegacyOrderedLineCount == 1
                         && analysis.Independent[20u] != 0u
                         && analysis.Independent[21u] == 0u,
                     "pattern D batched a line with a sidecar read dependency")
            && passed;
    }

    // A same-bank/version overlap is also ordered even without an explicit
    // source reference: two workgroups must not race their final stored cell.
    {
        static Fixture fixture;
        fixture.SetCommand(30u, 1u << 24u, 1u, 1u, 0u, 0xFFF8u, 16u);
        fixture.SetCommand(31u, 1u << 24u, 1u, 1u, 0u, 0x0004u, 16u);
        const CaptureLineAnalysis analysis =
            AnalyzeCaptureDependencies(fixture.PlanePointers, fixture.Commands.data());
        passed = Require(
                     analysis.IndependentLineCount == 0
                         && analysis.LegacyOrderedLineCount == 2,
                     "wrapped same-bank writes were incorrectly batched")
            && passed;
    }

    return passed;
}

} // namespace

int main()
{
    bool passed = true;
    for (const u32 scale : {1u, 4u, 8u, 16u})
        passed = RunVectorsForScale(scale) && passed;
    if (!passed)
        return 1;

    std::puts("CaptureSidecar dependency vectors PASS (1x/4x/8x/16x; Vulkan/DX12 shared classifier)");
    return 0;
}
