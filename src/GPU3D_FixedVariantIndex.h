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

#ifndef GPU3D_FIXED_VARIANT_INDEX_H
#define GPU3D_FIXED_VARIANT_INDEX_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace melonDS
{

// Fixed-capacity open-addressing index for per-frame renderer variants. The
// caller owns the canonical keys and their insertion order; probe positions
// only map to that canonical index. Advancing the epoch resets the table
// without a per-frame clear or allocation.
template<std::size_t Capacity, typename Epoch = std::uint32_t>
class FixedVariantIndex
{
    static_assert(Capacity > 1 && (Capacity & (Capacity - 1)) == 0,
        "variant index capacity must be a power of two");
    static_assert(std::is_integral_v<Epoch> && std::is_unsigned_v<Epoch>,
        "variant index epoch must be an unsigned integer");

    struct Entry
    {
        std::uint32_t Index = 0;
        Epoch Generation = 0;
    };

public:
    void Reset() noexcept
    {
        ++CurrentGeneration;
        if (CurrentGeneration != 0)
            return;

        // A wrapped generation could otherwise revive entries from the first
        // epoch. This path is reached once per 2^N resets (N=32 in renderers).
        for (Entry& entry : Entries)
            entry.Generation = 0;
        CurrentGeneration = 1;
    }

    template<typename MatchesCanonicalKey>
    [[nodiscard]] bool Find(
        std::uint32_t hash,
        MatchesCanonicalKey&& matchesCanonicalKey,
        std::uint32_t& index) const noexcept
    {
        std::size_t slot = hash & (Capacity - 1);
        for (std::size_t probe = 0; probe < Capacity; ++probe)
        {
            const Entry& entry = Entries[slot];
            if (entry.Generation != CurrentGeneration)
                return false;
            if (matchesCanonicalKey(entry.Index))
            {
                index = entry.Index;
                return true;
            }
            slot = (slot + 1) & (Capacity - 1);
        }
        return false;
    }

    [[nodiscard]] bool Insert(std::uint32_t hash, std::uint32_t index) noexcept
    {
        std::size_t slot = hash & (Capacity - 1);
        for (std::size_t probe = 0; probe < Capacity; ++probe)
        {
            Entry& entry = Entries[slot];
            if (entry.Generation != CurrentGeneration)
            {
                entry = { index, CurrentGeneration };
                return true;
            }
            slot = (slot + 1) & (Capacity - 1);
        }
        return false;
    }

private:
    std::array<Entry, Capacity> Entries{};
    Epoch CurrentGeneration = 1;
};

// Keeps common low-cardinality frames in a tiny L1-resident table, then
// rebuilds the canonical prefix into the large fixed table exactly once if the
// frame exceeds SmallLimit. Neither tier allocates or changes canonical order.
template<std::size_t SmallCapacity, std::size_t LargeCapacity,
         std::uint32_t SmallLimit>
class AdaptiveVariantIndex
{
    static_assert(SmallLimit < SmallCapacity,
        "small variant tier needs an empty probe terminator");
    static_assert(SmallCapacity < LargeCapacity,
        "large variant tier must be larger than the small tier");

public:
    void Reset() noexcept
    {
        Small.Reset();
        LargeActive = false;
    }

    template<typename MatchesCanonicalKey>
    [[nodiscard]] bool Find(
        std::uint32_t hash,
        MatchesCanonicalKey&& matchesCanonicalKey,
        std::uint32_t& index) const noexcept
    {
        if (LargeActive)
            return Large.Find(hash, matchesCanonicalKey, index);
        return Small.Find(hash, matchesCanonicalKey, index);
    }

    template<typename HashCanonicalKey>
    [[nodiscard]] bool Insert(
        std::uint32_t hash,
        std::uint32_t index,
        HashCanonicalKey&& hashCanonicalKey) noexcept
    {
        if (!LargeActive && index >= SmallLimit)
        {
            Large.Reset();
            for (std::uint32_t canonical = 0; canonical < index; ++canonical)
            {
                if (!Large.Insert(hashCanonicalKey(canonical), canonical))
                    return false;
            }
            LargeActive = true;
        }
        return LargeActive ? Large.Insert(hash, index) : Small.Insert(hash, index);
    }

private:
    FixedVariantIndex<SmallCapacity> Small{};
    FixedVariantIndex<LargeCapacity> Large{};
    bool LargeActive = false;
};

// Call sites combine exactly the fields used by their equality operator.
constexpr std::uint32_t MixVariantHash(
    std::uint32_t hash, std::uint32_t value) noexcept
{
    return (hash ^ value) * 0x01000193u;
}

} // namespace melonDS

#endif // GPU3D_FIXED_VARIANT_INDEX_H
