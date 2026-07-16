#pragma once

namespace MelonPrime
{

#if defined(MELONPRIME_SAPPHIRE_REBUILD)

inline constexpr bool sapphireRebuildActive() noexcept
{
    return true;
}

inline constexpr bool sapphireRebuildUsesLegacyGpu2DPath() noexcept
{
    return false;
}

#else

inline constexpr bool sapphireRebuildActive() noexcept
{
    return false;
}

inline constexpr bool sapphireRebuildUsesLegacyGpu2DPath() noexcept
{
    return true;
}

#endif

#if defined(MELONPRIME_SAPPHIRE_REBUILD) && defined(MELONPRIME_SAPPHIRE_REBUILD_SOLID_COLOR)

inline constexpr bool sapphireRebuildSolidColorPresentOnly() noexcept
{
    return true;
}

#else

inline constexpr bool sapphireRebuildSolidColorPresentOnly() noexcept
{
    return false;
}

#endif

#if defined(MELONPRIME_SAPPHIRE_REBUILD) && defined(MELONPRIME_SAPPHIRE_REBUILD_FEATURES)

inline constexpr bool sapphireRebuildDesktopFeaturesEnabled() noexcept
{
    return true;
}

#else

inline constexpr bool sapphireRebuildDesktopFeaturesEnabled() noexcept
{
    return false;
}

#endif

} // namespace MelonPrime
