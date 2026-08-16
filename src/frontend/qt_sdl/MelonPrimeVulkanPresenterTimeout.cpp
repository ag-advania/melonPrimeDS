/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#include "MelonPrimeVulkanPresenterTimeout.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <cerrno>
#include <cstdlib>
#include <limits>

namespace MelonPrime
{

namespace
{

// Keep this aligned with VulkanSync's one-second frame-fence watchdog. It is
// long enough not to affect normal frame intervals, while a stuck Linux WSI
// or image fence cannot hold native-surface retirement forever.
constexpr std::uint64_t kLinuxPresentationWaitTimeoutNanoseconds =
    1000ull * 1000ull * 1000ull;

constexpr std::uint64_t DefaultLinuxAwareTimeoutNanoseconds() noexcept
{
#if defined(__linux__)
    return kLinuxPresentationWaitTimeoutNanoseconds;
#else
    return std::numeric_limits<std::uint64_t>::max();
#endif
}

} // namespace


std::uint64_t PresenterAcquireTimeoutNanoseconds() noexcept
{
    const char* value = std::getenv("MELONPRIME_VULKAN_ACQUIRE_TIMEOUT_NS");
    if (value == nullptr || *value == '\0')
        return DefaultLinuxAwareTimeoutNanoseconds();

    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0')
        return DefaultLinuxAwareTimeoutNanoseconds();
    return static_cast<std::uint64_t>(parsed);
}


std::uint64_t PresenterImageFenceTimeoutNanoseconds() noexcept
{
    // Do not read MELONPRIME_VULKAN_ACQUIRE_TIMEOUT_NS here. That variable is
    // an acquire experiment and must not change the GPU/image-fence watchdog.
#if defined(__linux__)
    return kLinuxPresentationWaitTimeoutNanoseconds;
#else
    return std::numeric_limits<std::uint64_t>::max();
#endif
}

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
