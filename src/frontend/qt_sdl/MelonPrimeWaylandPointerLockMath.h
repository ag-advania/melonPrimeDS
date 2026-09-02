#ifndef MELONPRIME_WAYLAND_POINTER_LOCK_MATH_H
#define MELONPRIME_WAYLAND_POINTER_LOCK_MATH_H

#include <cstdint>

namespace MelonPrime {

// Wayland fixed-point motion uses signed 24.8 values. Keep the residual in
// fixed-point units so the event path performs integer arithmetic only; the
// quotient is the same truncation-toward-zero policy used by the former
// double-based implementation.
inline std::int32_t TakeWlFixedIntegral(
    std::int64_t& residual256, std::int32_t value) noexcept
{
    residual256 += static_cast<std::int64_t>(value);
    const std::int64_t whole = residual256 / 256;
    residual256 -= whole * 256;
    return static_cast<std::int32_t>(whole);
}

} // namespace MelonPrime

#endif // MELONPRIME_WAYLAND_POINTER_LOCK_MATH_H
