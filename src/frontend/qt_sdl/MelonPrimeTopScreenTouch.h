#ifndef MELONPRIME_TOP_SCREEN_TOUCH_H
#define MELONPRIME_TOP_SCREEN_TOUCH_H

// Top-screen touch hit-test: layout-derived state and the per-event mapping.
//
// SCR-PERF-002. `screenMatrix` only changes in setupScreenLayout(), so the
// determinant and the validity of each candidate transform are layout state,
// not per-event work. They are resolved once at the layout boundary and the
// input path is left with multiply-adds plus reciprocal multiplies, the bounds
// test and the optional clamp. The executable parity matrix in
// tools/testing/top-screen-touch-tests.cpp
// protects the integer touch result at every supported layout boundary.
//
// Header-only and free of Qt so the state machine is unit-testable on every
// build host.

#include <algorithm>
#include <cmath>

namespace MelonPrime {

// One resolved top-screen transform. `valid` folds in the screen-kind test and
// the determinant epsilon test, so the input path does neither.
struct TopScreenTouchTransform
{
    // Numerator coefficients plus the inverse determinant and original
    // translation. The numerator is deliberately rounded as float before the
    // reciprocal multiply: that preserves the old division expression's
    // integer boundary behaviour while still removing both hot-path divides.
    float numerator0 = 0.0f;
    float numerator1 = 0.0f;
    float numerator2 = 0.0f;
    float numerator3 = 0.0f;
    double inverseDeterminant = 0.0;
    float translateX = 0.0f;
    float translateY = 0.0f;
    bool valid = false;
};

// Matches the epsilon the pre-refactor per-event code used.
inline constexpr float kTopScreenTouchDeterminantEpsilon = 0.000001f;

// Cold path: called once per layout from setupScreenLayout(). `matrix` is the
// row-major 2x3 block ScreenLayout::GetScreenTransforms() filled for this
// transform; `isTopScreen` is its kind test.
[[nodiscard]] inline TopScreenTouchTransform MakeTopScreenTouchTransform(
    const float* matrix, bool isTopScreen) noexcept
{
    TopScreenTouchTransform out{};
    if (!matrix || !isTopScreen)
        return out;

    const float determinant = matrix[0] * matrix[3] - matrix[1] * matrix[2];
    if (std::abs(determinant) < kTopScreenTouchDeterminantEpsilon)
        return out;

    out.numerator0 = matrix[3];
    out.numerator1 = -matrix[2];
    out.numerator2 = -matrix[1];
    out.numerator3 = matrix[0];
    out.inverseDeterminant = 1.0 / static_cast<double>(determinant);
    out.translateX = matrix[4];
    out.translateY = matrix[5];
    out.valid = true;
    return out;
}

// Hot path: maps a panel-local pixel onto the DS top screen. Returns false and
// leaves px/py untouched when the transform is unusable, or when the point
// falls outside the screen and clamping was not requested.
[[nodiscard]] inline bool MapTopScreenTouch(
    const TopScreenTouchTransform& transform,
    int& px,
    int& py,
    bool clampCoords) noexcept
{
    if (!transform.valid)
        return false;

    const float dx = static_cast<float>(px) - transform.translateX;
    const float dy = static_cast<float>(py) - transform.translateY;
    const float numeratorX =
        transform.numerator0 * dx + transform.numerator1 * dy;
    const float numeratorY =
        transform.numerator2 * dx + transform.numerator3 * dy;
    const float sx = static_cast<float>(
        static_cast<double>(numeratorX) * transform.inverseDeterminant);
    const float sy = static_cast<float>(
        static_cast<double>(numeratorY) * transform.inverseDeterminant);

    if (!clampCoords && (sx < 0.0f || sx >= 256.0f || sy < 0.0f || sy >= 192.0f))
        return false;

    px = clampCoords ? std::clamp(static_cast<int>(sx), 0, 255) : static_cast<int>(sx);
    py = clampCoords ? std::clamp(static_cast<int>(sy), 0, 191) : static_cast<int>(sy);
    return true;
}

} // namespace MelonPrime

#endif // MELONPRIME_TOP_SCREEN_TOUCH_H
