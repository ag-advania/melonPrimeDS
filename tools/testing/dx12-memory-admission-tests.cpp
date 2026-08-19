#include "DX12MemoryAdmission.h"

#include <cstdio>

namespace
{

bool Require(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "dx12-memory-admission-tests: FAIL: %s\n", message);
    return condition;
}

} // namespace

int main()
{
    bool ok = true;
    for (const int scale : {1, 4, 8, 16})
    {
        const melonDS::DX12::ScaleFootprint footprint =
            melonDS::DX12::ComputeScaleFootprint(scale);
        ok &= Require(footprint.DefaultBytes != 0,
            "scale footprint must include default-heap resources");
        ok &= Require(footprint.UploadBytes != 0,
            "scale footprint must include upload resources");
        ok &= Require(footprint.LargestAllocation <= footprint.DefaultBytes,
            "largest allocation must fit in total footprint");
        ok &= Require(footprint.ResourceCount != 0,
            "scale footprint must include resource count");
    }

    melonDS::DX12::MemoryAdmissionSnapshot snapshot{};
    snapshot.HasLiveBudget = true;
    snapshot.LocalBudget = 8ull * melonDS::DX12::MemoryMiB * 1024ull;
    snapshot.LocalAvailableForReservation = 7ull * melonDS::DX12::MemoryMiB * 1024ull;
    const auto footprint = melonDS::DX12::ComputeScaleFootprint(1);
    ok &= Require(
        melonDS::DX12::EvaluateMemoryAdmission(snapshot, footprint).Accepted,
        "synthetic live budget should accept the 1x footprint");

    snapshot.LocalAvailableForReservation = footprint.DefaultBytes;
    ok &= Require(
        !melonDS::DX12::EvaluateMemoryAdmission(snapshot, footprint).Accepted,
        "safety reserve boundary must refuse without clamping");

    snapshot.HasLiveBudget = false;
    snapshot.IsUMA = true;
    ok &= Require(
        melonDS::DX12::EvaluateMemoryAdmission(snapshot, footprint).Accepted,
        "UMA without QueryVideoMemoryInfo must fail soft");

    if (!ok)
        return 1;
    std::printf("dx12-memory-admission-tests: PASS\n");
    return 0;
}
