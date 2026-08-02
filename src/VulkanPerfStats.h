#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "types.h"

namespace MelonDSAndroid
{

using melonDS::u64;

inline u64 PerfNowNs()
{
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline double PerfNsToMs(u64 valueNs)
{
    return static_cast<double>(valueNs) / 1000000.0;
}

template <size_t SampleCount = 120>
class PerfSampleWindow
{
public:
    struct Summary
    {
        size_t Count = 0;
        u64 MeanNs = 0;
        u64 P95Ns = 0;
        u64 MaxNs = 0;
    };

    void Add(u64 sampleNs)
    {
        if (Count >= SampleCount)
            return;

        Samples[Count++] = sampleNs;
    }

    [[nodiscard]] bool Ready() const
    {
        return Count >= SampleCount;
    }

    [[nodiscard]] bool Empty() const
    {
        return Count == 0;
    }

    Summary SummarizeAndReset()
    {
        Summary summary{};
        if (Count == 0)
            return summary;

        summary.Count = Count;

        u64 totalNs = 0;
        summary.MaxNs = 0;
        std::vector<u64> orderedSamples;
        orderedSamples.reserve(Count);
        for (size_t i = 0; i < Count; i++)
        {
            const u64 sample = Samples[i];
            totalNs += sample;
            summary.MaxNs = std::max(summary.MaxNs, sample);
            orderedSamples.push_back(sample);
        }

        std::sort(orderedSamples.begin(), orderedSamples.end());
        const size_t p95Index = std::min(
            orderedSamples.size() - 1,
            (orderedSamples.size() * 95u) / 100u);

        summary.MeanNs = totalNs / static_cast<u64>(Count);
        summary.P95Ns = orderedSamples[p95Index];
        Count = 0;
        return summary;
    }

private:
    std::array<u64, SampleCount> Samples{};
    size_t Count = 0;
};

template <typename Callback>
class ScopeExit
{
public:
    explicit ScopeExit(Callback callback)
        : CallbackFn(std::move(callback))
    {
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    ScopeExit(ScopeExit&& other) noexcept
        : CallbackFn(std::move(other.CallbackFn))
        , Active(other.Active)
    {
        other.Active = false;
    }

    ~ScopeExit()
    {
        if (Active)
            CallbackFn();
    }

private:
    Callback CallbackFn;
    bool Active = true;
};

template <typename Callback>
ScopeExit<Callback> MakeScopeExit(Callback callback)
{
    return ScopeExit<Callback>(std::move(callback));
}

}

namespace melonDS
{
using MelonDSAndroid::MakeScopeExit;
using MelonDSAndroid::PerfNowNs;
using MelonDSAndroid::PerfNsToMs;
using MelonDSAndroid::PerfSampleWindow;
}

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
