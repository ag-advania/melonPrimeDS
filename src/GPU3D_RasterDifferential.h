/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    This diagnostic is dormant unless MELONPRIME_RASTER_DIFFERENTIAL=1 is in
    the process environment. It feeds one GPU3D frame to the normal accelerated
    renderer and to a retained SoftRenderer3D, then compares every native 3D
    output word. No state, allocation or logging is added to normal frames.
*/

#ifndef GPU3D_RASTER_DIFFERENTIAL_H
#define GPU3D_RASTER_DIFFERENTIAL_H

#include <atomic>
#include <cstdlib>
#include <cstring>

#include "GPU3D.h"
#include "Platform.h"

namespace melonDS::RasterDifferential
{

inline bool Enabled() noexcept
{
    static const bool enabled = [] {
        const char* value = std::getenv("MELONPRIME_RASTER_DIFFERENTIAL");
        return value && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

inline bool WaitForDiagnosticSavestate() noexcept
{
    static const bool wait = [] {
        const char* value = std::getenv("MELONPRIME_TEST_SAVESTATE");
        return value && value[0] != '\0';
    }();
    return wait;
}

inline std::atomic<bool> DiagnosticSavestateReady { false };

inline void NotifyDiagnosticSavestateLoaded() noexcept
{
    DiagnosticSavestateReady.store(true, std::memory_order_release);
}

class State
{
public:
    void Reset() noexcept
    {
        FramesCompared = 0;
        MismatchedFrames = 0;
        MismatchedPixels = 0;
    }

    bool CompareFrame(Renderer3D& candidate, Renderer3D& reference, const char* backend)
    {
        if (WaitForDiagnosticSavestate())
        {
            if (!DiagnosticSavestateReady.load(std::memory_order_acquire))
                return true;
            if (!SavestateTransitionDiscarded)
            {
                SavestateTransitionDiscarded = true;
                Reset();
                Platform::Log(
                    Platform::LogLevel::Info,
                    "[RasterDiffTransition] discarded=1 reason=savestate-load\n");
                return true;
            }
        }

        constexpr u64 FnvOffset = 1469598103934665603ull;
        constexpr u64 FnvPrime = 1099511628211ull;

        u64 candidateHash = FnvOffset;
        u64 referenceHash = FnvOffset;
        u32 mismatchedPixels = 0;
        u32 nonZeroPixels = 0;
        int firstX = -1;
        int firstY = -1;
        u32 firstCandidate = 0;
        u32 firstReference = 0;
        struct Sample
        {
            u16 X;
            u16 Y;
            u32 Candidate;
            u32 Reference;
        };
        Sample samples[32] = {};
        u32 sampleCount = 0;

        for (int y = 0; y < 192; ++y)
        {
            const u32* candidateLine = candidate.GetLine(y);
            const u32* referenceLine = reference.GetLine(y);
            for (int x = 0; x < 256; ++x)
            {
                const u32 actual = candidateLine[x];
                const u32 expected = referenceLine[x];
                candidateHash = (candidateHash ^ actual) * FnvPrime;
                referenceHash = (referenceHash ^ expected) * FnvPrime;
                nonZeroPixels += actual != 0;
                if (actual == expected)
                    continue;

                if (mismatchedPixels == 0)
                {
                    firstX = x;
                    firstY = y;
                    firstCandidate = actual;
                    firstReference = expected;
                }
                if (sampleCount < 32)
                {
                    samples[sampleCount++] = {
                        static_cast<u16>(x), static_cast<u16>(y), actual, expected};
                }
                ++mismatchedPixels;
            }
        }

        ++FramesCompared;
        MismatchedFrames += mismatchedPixels != 0;
        MismatchedPixels += mismatchedPixels;

        Platform::Log(
            mismatchedPixels == 0 ? Platform::LogLevel::Info : Platform::LogLevel::Error,
            "[RasterDiff] backend=%s frame=%llu pixels=49152 nonZeroPixels=%u "
            "mismatchedPixels=%u candidateHash=%016llX referenceHash=%016llX "
            "first=(%d,%d,%08X,%08X) totals=(%llu,%llu,%llu)\n",
            backend,
            static_cast<unsigned long long>(FramesCompared),
            nonZeroPixels,
            mismatchedPixels,
            static_cast<unsigned long long>(candidateHash),
            static_cast<unsigned long long>(referenceHash),
            firstX,
            firstY,
            firstCandidate,
            firstReference,
            static_cast<unsigned long long>(FramesCompared),
            static_cast<unsigned long long>(MismatchedFrames),
            static_cast<unsigned long long>(MismatchedPixels));
        // Keep samples for the first two stable failures without flooding a
        // long-running diagnostic. The load-transition frame is discarded
        // before reaching this comparison.
        if (mismatchedPixels != 0 && MismatchedFrames <= 2)
        {
            for (u32 i = 0; i < sampleCount; ++i)
            {
                Platform::Log(
                    Platform::LogLevel::Error,
                    "[RasterDiffSample] backend=%s index=%u xy=(%u,%u) candidate=%08X reference=%08X\n",
                    backend, i, samples[i].X, samples[i].Y,
                    samples[i].Candidate, samples[i].Reference);
            }
        }
        return mismatchedPixels == 0;
    }

private:
    u64 FramesCompared = 0;
    u64 MismatchedFrames = 0;
    u64 MismatchedPixels = 0;
    bool SavestateTransitionDiscarded = false;
};

} // namespace melonDS::RasterDifferential

#endif // GPU3D_RASTER_DIFFERENTIAL_H
