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

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <limits>

namespace MelonPrime
{

namespace
{

// Keep this aligned with VulkanSync's one-second frame-fence watchdog. It is
// long enough not to affect normal frame intervals while a stuck Linux WSI
// cannot hold native-surface retirement forever.
constexpr std::uint64_t kLinuxPresentationWaitTimeoutNanoseconds =
    1000ull * 1000ull * 1000ull;

// This is intentionally a finite starting point rather than an unconditional
// zero. The low-latency A/B can select zero or another small budget when the
// display/driver evidence supports it.
constexpr std::uint64_t kDefaultLowLatencyAcquireTimeoutNanoseconds =
    500ull * 1000ull;

struct TimeoutCache
{
    std::atomic_bool Initialized{false};
    std::atomic<std::uint64_t> Value{0};
};

TimeoutCache NormalTimeoutCache;
TimeoutCache LowLatencyTimeoutCache;

constexpr std::uint64_t DefaultLinuxAwareTimeoutNanoseconds() noexcept
{
#if defined(__linux__)  // scatter-budget-exempt: Linux Vulkan WSI timeout policy, not input/runtime dispatch
    return kLinuxPresentationWaitTimeoutNanoseconds;
#else
    return std::numeric_limits<std::uint64_t>::max();
#endif
}

std::uint64_t ParseTimeoutEnvironment(
    const char* variable, std::uint64_t fallback) noexcept
{
    const char* value = std::getenv(variable);
    if (value == nullptr || *value == '\0')
        return fallback;
    if (*value == '-')
        return fallback;

    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0')
        return fallback;
    return static_cast<std::uint64_t>(parsed);
}

std::uint64_t CachedTimeout(
    TimeoutCache& cache, const char* variable, std::uint64_t fallback) noexcept
{
    if (!cache.Initialized.load(std::memory_order_acquire))
    {
        // Environment variables are process-start configuration. Two first
        // callers racing here may parse the same immutable value, but no
        // caller ever performs a frame-by-frame environment lookup.
        cache.Value.store(
            ParseTimeoutEnvironment(variable, fallback),
            std::memory_order_release);
        cache.Initialized.store(true, std::memory_order_release);
    }
    return cache.Value.load(std::memory_order_acquire);
}

} // namespace


std::uint64_t PresenterAcquireTimeoutNanoseconds() noexcept
{
    return CachedTimeout(
        NormalTimeoutCache,
        "MELONPRIME_VULKAN_ACQUIRE_TIMEOUT_NS",
        DefaultLinuxAwareTimeoutNanoseconds());
}


std::uint64_t PresenterLowLatencyAcquireTimeoutNanoseconds() noexcept
{
    return CachedTimeout(
        LowLatencyTimeoutCache,
        "MELONPRIME_VULKAN_LOW_LATENCY_ACQUIRE_TIMEOUT_NS",
        kDefaultLowLatencyAcquireTimeoutNanoseconds);
}

#if defined(MELONPRIME_VULKAN_PRESENTER_TIMEOUT_TESTING)
void ResetPresenterAcquireTimeoutCachesForTesting() noexcept
{
    NormalTimeoutCache.Initialized.store(false, std::memory_order_release);
    LowLatencyTimeoutCache.Initialized.store(false, std::memory_order_release);
}
#endif

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
