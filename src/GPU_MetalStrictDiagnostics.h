// MelonPrimeDS - Metal full-Metal-ification Phase M0 diagnostics.
//
// Shared, header-only helpers for the Metal renderer/presenter translation
// units. Production behavior is unchanged unless MELONPRIME_METAL_ASSERT_GPU_ONLY
// is set: then a small set of GPU-only contract violations (CPU fallback
// reached on a frame the caller had already marked as GPU-resident) log
// loudly to stderr instead of silently degrading.
//
// The exact conditions under which each call site is a genuine regression
// versus an already-known, still-unfixed architectural gap (see Phase M4's
// display-capture notes) are still being refined, so this logs by default and
// only aborts the process when explicitly asked to
// (MELONPRIME_METAL_ASSERT_GPU_ONLY=abort) -- a false-positive abort during
// real exploratory testing is worse than a log line reviewed afterward.
//
// See docs/plans/melonPrimeDS_develop_完全Metal化_詳細修正指示書.md Phase M0.

#ifndef GPU_METAL_STRICT_DIAGNOSTICS_H
#define GPU_METAL_STRICT_DIAGNOSTICS_H

#if defined(MELONPRIME_ENABLE_METAL)

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace melonDS
{

inline bool MetalAssertGpuOnlyEnabled() noexcept
{
    static const bool enabled = []() {
        const char* env = std::getenv("MELONPRIME_METAL_ASSERT_GPU_ONLY");
        return env && env[0] != '\0';
    }();
    return enabled;
}

inline bool MetalAssertGpuOnlyShouldAbort() noexcept
{
    static const bool shouldAbort = []() {
        const char* env = std::getenv("MELONPRIME_METAL_ASSERT_GPU_ONLY");
        return env && std::strcmp(env, "abort") == 0;
    }();
    return shouldAbort;
}

// Call at a site where a CPU/software fallback was just reached even though
// the caller had already committed to a GPU-only frame. Logs whenever
// MELONPRIME_METAL_ASSERT_GPU_ONLY is set to any value; only aborts the
// process when it is set to exactly "abort".
inline void MetalStrictGpuOnlyViolation(const char* subsystem, const char* detail) noexcept
{
    if (!MetalAssertGpuOnlyEnabled())
        return;

    std::fprintf(stderr,
        "[MelonPrime] METAL STRICT GPU-ONLY VIOLATION: %s: %s\n",
        subsystem, detail);

    if (MetalAssertGpuOnlyShouldAbort())
        std::abort();
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU_METAL_STRICT_DIAGNOSTICS_H
