#include "MelonPrimeSapphireFramebufferGuard.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace MelonPrime::SapphireFramebufferGuard
{

namespace
{

void abortOnCanaryFailure(
    const char* site,
    int bufferIndex,
    int planeIndex,
    const char* region,
    std::size_t offset,
    std::uint32_t expected,
    std::uint32_t actual) noexcept
{
    std::fprintf(
        stderr,
        "[SapphireFramebufferGuard] canary failure site=%s buffer=%d plane=%d region=%s "
        "offset=%zu expected=0x%08X actual=0x%08X\n",
        site != nullptr ? site : "unknown",
        bufferIndex,
        planeIndex,
        region != nullptr ? region : "unknown",
        offset,
        expected,
        actual);
    std::fflush(stderr);
    std::abort();
}

} // namespace

void PlaneAllocation::reset(std::size_t pixelCount)
{
    payloadCount_ = pixelCount;
    const std::size_t totalWords = kCanaryWords + pixelCount + kCanaryWords;
    backing_ = std::make_unique<std::uint32_t[]>(totalWords);
    payload_ = backing_.get() + kCanaryWords;
    std::memset(payload_, 0, payloadCount_ * sizeof(std::uint32_t));
    fillCanaries();
}

void PlaneAllocation::release() noexcept
{
    backing_.reset();
    payload_ = nullptr;
    payloadCount_ = 0;
}

void PlaneAllocation::fillCanaries() const noexcept
{
    if (backing_ == nullptr || payload_ == nullptr)
        return;

    for (std::size_t i = 0; i < kCanaryWords; ++i)
        backing_[i] = kCanaryValue;
    for (std::size_t i = 0; i < kCanaryWords; ++i)
        payload_[payloadCount_ + i] = kCanaryValue;
}

void PlaneAllocation::check(const char* site, int bufferIndex, int planeIndex) const noexcept
{
    if (backing_ == nullptr || payload_ == nullptr)
        return;

    for (std::size_t i = 0; i < kCanaryWords; ++i)
    {
        if (backing_[i] != kCanaryValue)
        {
            abortOnCanaryFailure(
                site, bufferIndex, planeIndex, "prefix", i, kCanaryValue, backing_[i]);
        }
    }

    for (std::size_t i = 0; i < kCanaryWords; ++i)
    {
        const std::uint32_t actual = payload_[payloadCount_ + i];
        if (actual != kCanaryValue)
        {
            abortOnCanaryFailure(
                site,
                bufferIndex,
                planeIndex,
                "suffix",
                payloadCount_ + i,
                kCanaryValue,
                actual);
        }
    }
}

} // namespace MelonPrime::SapphireFramebufferGuard

#endif
