#ifndef MELON_PRIME_HUD_DIRTY_REGIONS_H
#define MELON_PRIME_HUD_DIRTY_REGIONS_H

#ifdef MELONPRIME_CUSTOM_HUD

// =========================================================================
//  Bounded Custom HUD dirty-region set.
//
//  The Custom HUD used to describe one presented frame with a single bounding
//  QRect: the union of every element drawn that frame.  HP sits top-left, the
//  scoreboard centre, ammo bottom-right, so that union covers most of the
//  window even when the only pixels that actually moved belong to the
//  crosshair.  Every consumer downstream -- the CPU clear, the QPainter
//  recompose, the OpenGL/Vulkan/DX12/Metal upload, the final composite -- then
//  paid for the bounding box instead of for the change.
//
//  This type replaces that single rectangle with a small fixed-capacity set of
//  disjoint-ish rectangles.  It is deliberately:
//
//    - allocation free            (std::array, no heap, steady-state safe)
//    - bounded                    (kMaxRegions, so upload call count is capped)
//    - backend agnostic           (pixel areas only; no bytes-per-pixel, no
//                                  per-backend call cost is modelled here --
//                                  a backend that wants coarser regions
//                                  collapses them itself)
//    - value semantics            (trivially copyable POD-ish aggregate)
//
//  Merge policy: a rectangle being added is merged into an existing region
//  when the two overlap or touch, and merges cascade so the set never keeps
//  two regions that could be one.  Only when the set is full does it fall back
//  to merging the pair whose union wastes the fewest pixels.
//
//  Thread: whatever thread owns the overlay being described.  This type takes
//  no locks and has no shared state.
// =========================================================================

#include <array>
#include <cstdint>

#include <QRect>

struct HudDirtyRegionSet
{
    // Eight is the point where the per-region bookkeeping and the extra
    // backend upload calls stop paying for themselves against the pixels
    // they save; the HUD has ~10 elements and most frames touch one or two.
    static constexpr int kMaxRegions = 8;

    std::array<QRect, kMaxRegions> regions{};
    std::uint8_t count = 0;

    void Reset() noexcept { count = 0; }

    bool IsEmpty() const noexcept { return count == 0; }

    int Count() const noexcept { return static_cast<int>(count); }

    const QRect& Region(int index) const noexcept { return regions[static_cast<std::size_t>(index)]; }

    // Bounding box of the whole set. Empty when the set is empty.
    QRect Bounds() const noexcept
    {
        QRect bounds;
        for (std::uint8_t i = 0; i < count; ++i)
            bounds |= regions[i];
        return bounds;
    }

    // Sum of the region areas. Regions are kept non-overlapping by the merge
    // policy below, so this is the real pixel count rather than an estimate.
    std::uint64_t PixelCount() const noexcept
    {
        std::uint64_t pixels = 0;
        for (std::uint8_t i = 0; i < count; ++i)
            pixels += static_cast<std::uint64_t>(regions[i].width())
                    * static_cast<std::uint64_t>(regions[i].height());
        return pixels;
    }

    bool Intersects(const QRect& rect) const noexcept
    {
        if (rect.isEmpty())
            return false;
        for (std::uint8_t i = 0; i < count; ++i) {
            if (regions[i].intersects(rect))
                return true;
        }
        return false;
    }

    void Add(const QRect& rect) noexcept
    {
        const QRect candidate = rect.normalized();
        if (candidate.isEmpty())
            return;

        QRect merged = candidate;
        // Absorb every region the candidate overlaps or touches, and repeat:
        // absorbing one region can grow `merged` enough to reach another.
        bool absorbedAny = true;
        while (absorbedAny) {
            absorbedAny = false;
            for (std::uint8_t i = 0; i < count;) {
                if (Touches(merged, regions[i])) {
                    merged |= regions[i];
                    RemoveAt(i);
                    absorbedAny = true;
                    continue;
                }
                ++i;
            }
        }

        if (count < kMaxRegions) {
            regions[count++] = merged;
            return;
        }

        // Full. Either fold the candidate into whichever region it wastes the
        // fewest pixels against, or collapse the cheapest existing pair and
        // give the candidate the freed slot -- whichever wastes less.
        std::uint8_t bestHost = 0;
        std::int64_t hostCost = ExtraPixels(regions[0], merged);
        for (std::uint8_t i = 1; i < count; ++i) {
            const std::int64_t cost = ExtraPixels(regions[i], merged);
            if (cost < hostCost) {
                hostCost = cost;
                bestHost = i;
            }
        }

        std::uint8_t pairA = 0;
        std::uint8_t pairB = 0;
        const std::int64_t pairCost = CheapestPair(pairA, pairB);
        if (hostCost <= pairCost) {
            regions[bestHost] |= merged;
            return;
        }
        regions[pairA] |= regions[pairB];
        RemoveAt(pairB);
        regions[count++] = merged;
    }

    void Unite(const HudDirtyRegionSet& other) noexcept
    {
        for (std::uint8_t i = 0; i < other.count; ++i)
            Add(other.regions[i]);
    }

    // Clip every region to `clip`, dropping the ones that fall outside it.
    void IntersectWith(const QRect& clip) noexcept
    {
        for (std::uint8_t i = 0; i < count;) {
            const QRect clipped = regions[i] & clip;
            if (clipped.isEmpty()) {
                RemoveAt(i);
                continue;
            }
            regions[i] = clipped;
            ++i;
        }
    }

    void SetSingle(const QRect& rect) noexcept
    {
        Reset();
        Add(rect);
    }

    // Collapse the whole set into its own bounding box. Backends whose upload
    // or composite call is expensive enough to beat the saved pixels use this
    // rather than teaching the HUD core about their call cost.
    void CollapseToBounds() noexcept
    {
        if (count <= 1)
            return;
        const QRect bounds = Bounds();
        Reset();
        if (!bounds.isEmpty())
            regions[count++] = bounds;
    }

    // Collapse until at most `limit` regions remain, always merging the pair
    // whose union wastes the fewest pixels.
    void CollapseTo(int limit) noexcept
    {
        if (limit < 1)
            limit = 1;
        while (count > limit)
            CollapseCheapestPair();
    }

    bool operator==(const HudDirtyRegionSet& other) const noexcept
    {
        if (count != other.count)
            return false;
        for (std::uint8_t i = 0; i < count; ++i) {
            if (regions[i] != other.regions[i])
                return false;
        }
        return true;
    }

    bool operator!=(const HudDirtyRegionSet& other) const noexcept
    {
        return !(*this == other);
    }

private:
    // Overlapping *or* edge-adjacent. Adjacent rectangles are merged because
    // two touching uploads cost more than the single upload that covers both
    // and wastes nothing.
    static bool Touches(const QRect& a, const QRect& b) noexcept
    {
        if (a.isEmpty() || b.isEmpty())
            return false;
        return a.adjusted(-1, -1, 1, 1).intersects(b);
    }

    // Pixels the union covers that neither input needed. Zero for a perfect
    // tiling, large for two far-apart corners of the window.
    static std::int64_t ExtraPixels(const QRect& a, const QRect& b) noexcept
    {
        const std::int64_t unionArea = Area(a | b);
        const std::int64_t coveredArea = Area(a) + Area(b) - Area(a & b);
        return unionArea - coveredArea;
    }

    static std::int64_t Area(const QRect& r) noexcept
    {
        if (r.isEmpty())
            return 0;
        return static_cast<std::int64_t>(r.width()) * static_cast<std::int64_t>(r.height());
    }

    void RemoveAt(std::uint8_t index) noexcept
    {
        regions[index] = regions[count - 1];
        regions[count - 1] = QRect();
        --count;
    }

    // Index pair whose union wastes the fewest pixels, with that cost.
    std::int64_t CheapestPair(std::uint8_t& outA, std::uint8_t& outB) const noexcept
    {
        outA = 0;
        outB = 1;
        std::int64_t bestCost = ExtraPixels(regions[0], regions[1]);
        for (std::uint8_t a = 0; a < count; ++a) {
            for (std::uint8_t b = static_cast<std::uint8_t>(a + 1); b < count; ++b) {
                const std::int64_t cost = ExtraPixels(regions[a], regions[b]);
                if (cost < bestCost) {
                    bestCost = cost;
                    outA = a;
                    outB = b;
                }
            }
        }
        return bestCost;
    }

    void CollapseCheapestPair() noexcept
    {
        if (count < 2)
            return;
        std::uint8_t bestA = 0;
        std::uint8_t bestB = 1;
        CheapestPair(bestA, bestB);
        regions[bestA] |= regions[bestB];
        RemoveAt(bestB);
    }
};

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_DIRTY_REGIONS_H
