#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <cstddef>
#include <cstdint>
#include <memory>

namespace MelonPrime::SapphireFramebufferGuard
{

constexpr std::uint32_t kCanaryValue = 0xDEADBEEFu;
constexpr std::size_t kCanaryWords = 4;

class PlaneAllocation
{
public:
    void reset(std::size_t pixelCount);
    void release() noexcept;
    explicit operator bool() const noexcept { return payload_ != nullptr; }
    operator std::uint32_t*() const noexcept { return payload_; }
    std::uint32_t* data() const noexcept { return payload_; }
    std::uint32_t* get() const noexcept { return payload_; }
    void check(const char* site, int bufferIndex, int planeIndex) const noexcept;

private:
    std::unique_ptr<std::uint32_t[]> backing_;
    std::uint32_t* payload_ = nullptr;
    std::size_t payloadCount_ = 0;

    void fillCanaries() const noexcept;
};

} // namespace MelonPrime::SapphireFramebufferGuard

#endif
