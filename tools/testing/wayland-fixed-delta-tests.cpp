#include "MelonPrimeWaylandPointerLockMath.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

struct FixedMotion {
    std::int32_t acceleratedX;
    std::int32_t acceleratedY;
    std::int32_t unacceleratedX;
    std::int32_t unacceleratedY;
};

std::int32_t OldDoubleModel(double& residual, std::int32_t value)
{
    residual += static_cast<double>(value) / 256.0;
    const double integral = std::trunc(residual);
    residual -= integral;
    const double lo = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    const double hi = static_cast<double>(std::numeric_limits<std::int32_t>::max());
    return static_cast<std::int32_t>(integral < lo ? lo : integral > hi ? hi : integral);
}

bool CheckSequence(const std::vector<FixedMotion>& sequence)
{
    double oldResidualX = 0.0;
    double oldResidualY = 0.0;
    std::int64_t newResidualX = 0;
    std::int64_t newResidualY = 0;

    for (const FixedMotion& motion : sequence) {
        const bool haveUnaccelerated =
            motion.unacceleratedX != 0 || motion.unacceleratedY != 0;
        const std::int32_t sourceX = haveUnaccelerated
            ? motion.unacceleratedX : motion.acceleratedX;
        const std::int32_t sourceY = haveUnaccelerated
            ? motion.unacceleratedY : motion.acceleratedY;
        const std::int32_t oldX = OldDoubleModel(oldResidualX, sourceX);
        const std::int32_t oldY = OldDoubleModel(oldResidualY, sourceY);
        const std::int32_t newX = MelonPrime::TakeWlFixedIntegral(
            newResidualX, sourceX);
        const std::int32_t newY = MelonPrime::TakeWlFixedIntegral(
            newResidualY, sourceY);
        if (oldX != newX || oldY != newY)
            return false;
    }
    return oldResidualX == static_cast<double>(newResidualX) / 256.0
        && oldResidualY == static_cast<double>(newResidualY) / 256.0;
}

} // namespace

int main()
{
    if (!CheckSequence({
            // +0.25 x4 and -0.25 x4: residuals must carry exactly.
            {64, 0, 0, 0}, {64, 0, 0, 0},
            {64, 0, 0, 0}, {64, 0, 0, 0},
            {-64, 0, 0, 0}, {-64, 0, 0, 0},
            {-64, 0, 0, 0}, {-64, 0, 0, 0},
            // +0.5 pairs and -0.5 pairs.
            {128, 128, 0, 0}, {128, 128, 0, 0},
            {-128, -128, 0, 0}, {-128, -128, 0, 0},
            // Prefer the nonzero unaccelerated source, then cover zero.
            {256, 256, 64, 64},
            {0, 0, 0, 0},
            // Large signed fixed-point inputs must match the old model.
            {std::numeric_limits<std::int32_t>::max(),
             std::numeric_limits<std::int32_t>::max(), 0, 0},
            {std::numeric_limits<std::int32_t>::min(),
             std::numeric_limits<std::int32_t>::min(), 0, 0},
        }))
    {
        std::fprintf(stderr, "wayland-fixed-delta-tests: FAIL\n");
        return 1;
    }

    std::puts("wayland-fixed-delta-tests: PASS");
    return 0;
}
