/* Standalone high-cardinality benchmark for GPU3D_FixedVariantIndex.h.

   Build on Windows MinGW:
     c++ -O3 -std=c++17 -Isrc tools/perf/variant-index-benchmark.cpp -o variant-index-benchmark.exe
*/

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

#include "GPU3D_FixedVariantIndex.h"

namespace
{

constexpr std::uint32_t MaxVariants = 2048;

struct Key
{
    std::uint32_t Words[7] {};

    bool operator==(const Key& other) const noexcept
    {
        for (int word = 0; word < 7; ++word)
            if (Words[word] != other.Words[word])
                return false;
        return true;
    }
};

std::uint32_t HashKey(const Key& key) noexcept
{
    std::uint32_t hash = 0x811C9DC5u;
    for (std::uint32_t word : key.Words)
        hash = melonDS::MixVariantHash(hash, word);
    return hash;
}

std::array<Key, MaxVariants> MakeKeys()
{
    std::array<Key, MaxVariants> keys {};
    std::uint32_t state = 0xC001D00Du;
    for (Key& key : keys)
    {
        for (std::uint32_t& word : key.Words)
        {
            state = state * 1664525u + 1013904223u;
            word = state;
        }
    }
    return keys;
}

std::uint64_t RunLegacy(
    const std::array<Key, MaxVariants>& keys, int frames)
{
    std::array<Key, MaxVariants> canonical {};
    std::uint64_t checksum = 0;
    for (int frame = 0; frame < frames; ++frame)
    {
        std::uint32_t count = 0;
        for (std::uint32_t pass = 0; pass < 2; ++pass)
        {
            for (std::uint32_t position = 0; position < MaxVariants; ++position)
            {
                const std::uint32_t keyIndex = pass == 0
                    ? position : MaxVariants - 1u - position;
                const Key& key = keys[keyIndex];
                std::uint32_t index = count;
                bool found = false;
                for (std::uint32_t candidate = count; candidate != 0; --candidate)
                {
                    if (canonical[candidate - 1] == key)
                    {
                        found = true;
                        index = candidate - 1;
                        break;
                    }
                }
                if (!found)
                    canonical[count++] = key;
                checksum += index;
            }
        }
    }
    return checksum;
}

std::uint64_t RunFixedIndex(
    const std::array<Key, MaxVariants>& keys, int frames)
{
    std::array<Key, MaxVariants> canonical {};
    melonDS::AdaptiveVariantIndex<64, 4096, 32> lookup;
    std::uint64_t checksum = 0;
    for (int frame = 0; frame < frames; ++frame)
    {
        lookup.Reset();
        std::uint32_t count = 0;
        for (std::uint32_t pass = 0; pass < 2; ++pass)
        {
            for (std::uint32_t position = 0; position < MaxVariants; ++position)
            {
                const std::uint32_t keyIndex = pass == 0
                    ? position : MaxVariants - 1u - position;
                const Key& key = keys[keyIndex];
                const std::uint32_t hash = HashKey(key);
                std::uint32_t index = 0;
                const bool found = lookup.Find(hash,
                    [&](std::uint32_t candidate) noexcept {
                        return candidate < count && canonical[candidate] == key;
                    }, index);
                if (!found)
                {
                    index = count;
                    canonical[count++] = key;
                    if (!lookup.Insert(hash, index,
                        [&](std::uint32_t candidate) noexcept {
                            return HashKey(canonical[candidate]);
                        }))
                        return 0;
                }
                checksum += index;
            }
        }
    }
    return checksum;
}

template<typename Function>
double TimeMilliseconds(Function&& function, std::uint64_t& checksum)
{
    const auto start = std::chrono::steady_clock::now();
    checksum = function();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

} // namespace

int main(int argc, char** argv)
{
    const int frames = argc > 1 ? std::max(1, std::atoi(argv[1])) : 50;
    const auto keys = MakeKeys();
    std::uint64_t legacyChecksum = 0;
    std::uint64_t fixedChecksum = 0;

    // One untimed pass faults in code/data before the reported samples.
    RunLegacy(keys, 1);
    RunFixedIndex(keys, 1);

    const double legacyMs = TimeMilliseconds(
        [&] { return RunLegacy(keys, frames); }, legacyChecksum);
    const double fixedMs = TimeMilliseconds(
        [&] { return RunFixedIndex(keys, frames); }, fixedChecksum);
    if (legacyChecksum == 0 || legacyChecksum != fixedChecksum)
    {
        std::fprintf(stderr, "FAIL: variant sequence checksum mismatch\n");
        return 1;
    }

    std::printf(
        "frames=%d variants=2048 sequence=unique+reverse legacy_ms=%.3f "
        "fixed_index_ms=%.3f speedup=%.2fx checksum=%llu\n",
        frames, legacyMs, fixedMs, legacyMs / fixedMs,
        static_cast<unsigned long long>(fixedChecksum));
    return 0;
}
