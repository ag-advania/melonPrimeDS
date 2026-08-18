/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
*/

#ifndef DX12_MEMORY_ADMISSION_H
#define DX12_MEMORY_ADMISSION_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <algorithm>
#include <limits>
#include <string>

#include "DX12Common.h"

namespace melonDS::DX12
{

inline constexpr u64 MemoryMiB = 1024ull * 1024ull;
inline constexpr u64 LiveBudgetFixedSafetyReserve = 128ull * MemoryMiB;

struct MemoryAdmissionSnapshot
{
    bool HasLiveBudget = false;
    bool IsUMA = false;
    u64 DedicatedVideoMemory = 0; // diagnostic only; never the live authority
    u64 LocalBudget = 0;
    u64 LocalCurrentUsage = 0;
    u64 LocalAvailableForReservation = 0;
    u64 LocalCurrentReservation = 0;
};

struct ScaleFootprint
{
    u64 DefaultBytes = 0;
    u64 UploadBytes = 0;
    u64 LargestAllocation = 0;
    u32 ResourceCount = 0;
};

struct MemoryAdmissionResult
{
    bool Accepted = false;
    u64 AvailableBytes = 0;
    u64 SafetyReserve = 0;
    std::string Reason;
};

[[nodiscard]] inline bool AddWouldOverflow(u64 left, u64 right) noexcept
{
    return right > std::numeric_limits<u64>::max() - left;
}

// The pure model mirrors GPU3D_DX12::CreateScaleDependentResources. It is
// deliberately conservative about optional direct textures and compositor
// slots, while leaving actual D3D12 alignment to GetResourceAllocationInfo at
// resource creation time.
[[nodiscard]] inline ScaleFootprint ComputeScaleFootprint(
    int scaleFactor, u32 requestedWorkTiles = 0) noexcept
{
    ScaleFootprint footprint;
    if (scaleFactor < 1)
        scaleFactor = 1;

    const u64 scale = static_cast<u64>(scaleFactor);
    const u32 range = (scaleFactor >= 5 ? 1u : 0u) + (scaleFactor >= 9 ? 1u : 0u);
    const u32 tileSize = 8u << range;
    const u32 width = 256u * static_cast<u32>(scale);
    const u32 height = 192u * static_cast<u32>(scale);
    const u32 tileShift = 3u + range;
    const u32 tilesPerLine = width >> tileShift;
    const u32 tileLines = height >> tileShift;
    const u64 workTiles = requestedWorkTiles != 0
        ? requestedWorkTiles
        : static_cast<u64>(tilesPerLine) * tileLines * 16ull;
    const u64 pixels = static_cast<u64>(width) * height;
    const u64 tileBytes = 4ull * tileSize * tileSize * workTiles;
    const u64 inputBytes = static_cast<u64>(
        14u * (256u * 192u) + (2u * 192u) + (192u * 4u)) * sizeof(u32);
    const u64 composedBytes = pixels * 4ull * 2ull;
    const u64 directTextureBytes = pixels * 4ull * 2ull;
    const u64 ySpanIndices = static_cast<u64>(height) * 2048ull;
    const u64 xSpanBytes = 24ull * 4ull * ySpanIndices;
    const u64 setupBytes = 8ull * ySpanIndices;
    const u64 binHeaderBytes = (2048ull * 4ull + 2048ull + 2048ull + 4ull) * 4ull;
    const u64 binBytes = binHeaderBytes
        + static_cast<u64>(tilesPerLine) * tileLines * (64ull + 64ull + 64ull) * 4ull;

    const u64 defaultBytes =
        tileBytes * 3ull
        + pixels * 3ull * 2ull * 4ull             // result buffer
        + (scaleFactor == 1 ? pixels * 2ull * 4ull : 4ull) // result winner
        + pixels * 4ull                             // final framebuffer
        + 8ull * 256ull * 256ull * scale * scale * 4ull // capture sidecar
        + inputBytes * 3ull
        + composedBytes * 3ull
        + directTextureBytes * 3ull
        + workTiles * 2ull * 4ull * 2ull
        + pixels * 4ull                             // blend state
        + binBytes
        + xSpanBytes
        + setupBytes;
    const u64 uploadBytes = inputBytes * 3ull + setupBytes;

    footprint.DefaultBytes = defaultBytes;
    footprint.UploadBytes = uploadBytes;
    footprint.LargestAllocation = std::max({
        tileBytes,
        pixels * 3ull * 2ull * 4ull,
        xSpanBytes,
        8ull * 256ull * 256ull * scale * scale * 4ull,
        composedBytes,
        directTextureBytes,
    });
    footprint.ResourceCount = 25;
    return footprint;
}

[[nodiscard]] inline MemoryAdmissionResult EvaluateMemoryAdmission(
    const MemoryAdmissionSnapshot& snapshot,
    const ScaleFootprint& footprint)
{
    MemoryAdmissionResult result;
    if (!snapshot.HasLiveBudget)
    {
        result.Accepted = true;
        result.Reason = snapshot.IsUMA
            ? "no live DXGI budget; UMA left to CreateCommittedResource"
            : "no live DXGI budget authority";
        return result;
    }

    result.AvailableBytes = snapshot.LocalAvailableForReservation;
    result.SafetyReserve = std::max(
        LiveBudgetFixedSafetyReserve,
        snapshot.LocalBudget / 20ull);
    if (result.AvailableBytes < result.SafetyReserve)
    {
        result.Reason = "live DXGI local budget has no room after the safety reserve";
        return result;
    }
    if (footprint.DefaultBytes > result.AvailableBytes - result.SafetyReserve)
    {
        result.Reason = "scale footprint exceeds live DXGI local budget after the safety reserve";
        return result;
    }

    result.Accepted = true;
    result.Reason = "live DXGI local budget admission";
    return result;
}

} // namespace melonDS::DX12

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_MEMORY_ADMISSION_H
