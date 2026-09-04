#ifndef MELON_PRIME_HUD_RETAINED_STATE_H
#define MELON_PRIME_HUD_RETAINED_STATE_H

#ifdef MELONPRIME_CUSTOM_HUD

// =========================================================================
//  Custom HUD retained-composition state.
//
//  The overlay QImage is retained across presented frames.  Historically the
//  only thing remembered about it was "the bounding box of everything drawn
//  last frame", which forced a full clear + full redraw + bounding-box upload
//  on every new emulated frame, whether or not a single HUD pixel changed.
//
//  What is remembered instead is, per HUD element:
//
//    revision  a content stamp over exactly the values that decide the
//              element's pixels (HP value, ammo, resolved scoreboard cells,
//              crosshair centre, ...).  Equal revision => identical pixels.
//    bounds    where those pixels landed in overlay space.
//    present   whether the element drew at all.
//
//  From that the composer derives which elements have to be re-rasterised,
//  which overlapping neighbours have to be re-applied for correct z-order,
//  which overlay regions to clear, and which regions the backend must upload.
//
//  Revisions describe *visual* content, never raw guest state: the match
//  clock stamps the displayed second, not the 60 Hz tick, so 59 of every 60
//  frames are correctly seen as unchanged.
//
//  Thread/lifetime: owned by CustomHudConfigState, so per emulator instance,
//  and only touched under ScopedHudConfigState on the presenting thread.
// =========================================================================

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <QRect>

#include "MelonPrimeHudDirtyRegions.h"

namespace MelonPrime {

// Draw order is z-order: earlier entries are drawn first and therefore sit
// underneath later ones. The composer relies on this ordering to decide which
// neighbours a re-rasterised element has to be re-applied over.
enum class HudElementId : std::uint8_t {
    Scoreboard = 0,
    EnemyTarget,
    Crosshair,
    Hp,
    Bomb,
    MatchStatus,
    RankTime,
    Radar,
    WeaponAmmo,
    WeaponInventory,
    Count
};

inline constexpr std::size_t kHudElementIdCount =
    static_cast<std::size_t>(HudElementId::Count);

inline constexpr std::size_t HudElementIndex(HudElementId id) noexcept
{
    return static_cast<std::size_t>(id);
}

inline const char* HudElementIdName(HudElementId id) noexcept
{
    switch (id) {
    case HudElementId::Scoreboard:      return "scoreboard";
    case HudElementId::EnemyTarget:     return "enemy_target";
    case HudElementId::Crosshair:       return "crosshair";
    case HudElementId::Hp:              return "hp";
    case HudElementId::Bomb:            return "bomb";
    case HudElementId::MatchStatus:     return "match_status";
    case HudElementId::RankTime:        return "rank_time";
    case HudElementId::Radar:           return "radar";
    case HudElementId::WeaponAmmo:      return "weapon_ammo";
    case HudElementId::WeaponInventory: return "weapon_inventory";
    case HudElementId::Count:           break;
    }
    return "unknown";
}

// One element's contribution to the retained overlay.
struct HudElementVisualStamp {
    std::uint64_t revision = 0;
    QRect bounds;
    bool present = false;
};

// What this frame wants each element to look like, before anything is drawn.
struct HudElementPass {
    std::uint64_t revision = 0;
    bool present = false;
    // Set for elements whose pixels cannot be predicted from a cheap stamp
    // (the radar crop mirrors live bottom-screen pixels). They re-rasterise
    // whenever they are present.
    bool alwaysDirty = false;
    // Set when the element paints onto a target other than the retained
    // overlay -- the native Software panel repaints the scoreboard straight
    // onto the widget painter. A detached element is drawn on every presented
    // frame and never occupies retained overlay pixels, so it contributes
    // neither bounds nor dirty regions.
    bool detached = false;
    // Set when the element overwrites every pixel it occupies (source
    // composition, not source-over) and can therefore be re-applied without a
    // clear first. That is what lets the scoreboard repaint one changed cell
    // instead of its whole panel: clearing the panel would oblige it to
    // recomposite all of it. Only claim this when the element's occupied area
    // cannot have shrunk since the last draw.
    bool selfClearing = false;
};

// Identity of the surface the retained pixels belong to. Any mismatch means
// the retained pixels describe a different surface (resize, DPI change,
// renderer switch, another panel) and the composer must start from a full
// clear rather than trusting per-element bounds.
struct HudRetainedSurfaceKey {
    const void* overlayBits = nullptr;
    int width = 0;
    int height = 0;
    std::uint32_t configEpoch = 0;
    std::uint32_t visualGeneration = 0;
    int fontPixelSize = 0;
    float hudScale = 0.0f;
    float stretchX = 0.0f;
    float originXds = 0.0f;
    float originYds = 0.0f;
    bool editMode = false;

    bool operator==(const HudRetainedSurfaceKey& other) const noexcept
    {
        return overlayBits == other.overlayBits
            && width == other.width
            && height == other.height
            && configEpoch == other.configEpoch
            && visualGeneration == other.visualGeneration
            && fontPixelSize == other.fontPixelSize
            && hudScale == other.hudScale
            && stretchX == other.stretchX
            && originXds == other.originXds
            && originYds == other.originYds
            && editMode == other.editMode;
    }

    bool operator!=(const HudRetainedSurfaceKey& other) const noexcept
    {
        return !(*this == other);
    }
};

struct HudRetainedComposition {
    std::array<HudElementVisualStamp, kHudElementIdCount> elements{};
    HudRetainedSurfaceKey surface{};
    bool valid = false;

    void Invalidate() noexcept
    {
        valid = false;
        for (auto& element : elements)
            element = HudElementVisualStamp{};
        surface = HudRetainedSurfaceKey{};
    }

    // Union of every element that currently has pixels on the overlay. This is
    // what a presenter has to composite, as opposed to the dirty set, which is
    // only what changed.
    void CollectContent(HudDirtyRegionSet& out) const noexcept
    {
        out.Reset();
        for (const auto& element : elements) {
            if (element.present)
                out.Add(element.bounds);
        }
    }
};

// FNV-1a over the values that decide an element's pixels. Small, branch-free,
// and callable from the hot path; it hashes POD only, never a QString buffer.
class HudVisualRevision {
public:
    HudVisualRevision& Mix(std::uint64_t value) noexcept
    {
        m_hash ^= value;
        m_hash *= 1099511628211ull;
        return *this;
    }

    HudVisualRevision& Mix(std::int64_t value) noexcept
    {
        return Mix(static_cast<std::uint64_t>(value));
    }

    HudVisualRevision& Mix(std::uint32_t value) noexcept
    {
        return Mix(static_cast<std::uint64_t>(value));
    }

    HudVisualRevision& Mix(std::int32_t value) noexcept
    {
        return Mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(value)));
    }

    HudVisualRevision& Mix(bool value) noexcept
    {
        return Mix(static_cast<std::uint64_t>(value ? 1u : 0u));
    }

    HudVisualRevision& Mix(float value) noexcept
    {
        // Bit pattern, not the value: identical encodings rasterise
        // identically, and none of these inputs is ever NaN.
        static_assert(sizeof(std::uint32_t) == sizeof(float), "float is not 32-bit");
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return Mix(bits);
    }

    // A revision of 0 is reserved for "no content", so an element that hashes
    // to zero is nudged rather than being mistaken for absent.
    std::uint64_t Value() const noexcept { return m_hash ? m_hash : 1ull; }

private:
    std::uint64_t m_hash = 1469598103934665603ull; // FNV-1a offset basis
};

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
#endif // MELON_PRIME_HUD_RETAINED_STATE_H
