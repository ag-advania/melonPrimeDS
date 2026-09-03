/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#include "MelonPrimeTopScreenTouch.h"
#include "ScreenLayout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

struct Result
{
    bool valid = false;
    int x = 0;
    int y = 0;
};

Result ReferenceMap(const float* matrix, bool isTop, int x, int y, bool clamp)
{
    Result result{false, x, y};
    if (!matrix || !isTop)
        return result;

    const float determinant =
        matrix[0] * matrix[3] - matrix[1] * matrix[2];
    if (std::abs(determinant)
        < MelonPrime::kTopScreenTouchDeterminantEpsilon)
        return result;

    const float dx = static_cast<float>(x) - matrix[4];
    const float dy = static_cast<float>(y) - matrix[5];
    const float sx = (matrix[3] * dx - matrix[2] * dy) / determinant;
    const float sy = (-matrix[1] * dx + matrix[0] * dy) / determinant;
    if (!clamp
        && (sx < 0.0f || sx >= 256.0f || sy < 0.0f || sy >= 192.0f))
        return result;

    result.valid = true;
    result.x = clamp ? std::clamp(static_cast<int>(sx), 0, 255)
                     : static_cast<int>(sx);
    result.y = clamp ? std::clamp(static_cast<int>(sy), 0, 191)
                     : static_cast<int>(sy);
    return result;
}

Result ProductionMap(const float* matrix, bool isTop, int x, int y, bool clamp)
{
    Result result{false, x, y};
    const auto transform =
        MelonPrime::MakeTopScreenTouchTransform(matrix, isTop);
    result.valid = MelonPrime::MapTopScreenTouch(
        transform, result.x, result.y, clamp);
    return result;
}

[[noreturn]] void Fail(
    const char* label,
    const float* matrix,
    int x,
    int y,
    bool clamp,
    Result expected,
    Result actual)
{
    std::fprintf(stderr,
        "FAIL %s host=(%d,%d) clamp=%d "
        "expected=(%d,%d,%d) actual=(%d,%d,%d) "
        "matrix=[%.9g %.9g %.9g %.9g %.9g %.9g]\n",
        label, x, y, clamp ? 1 : 0,
        expected.valid ? 1 : 0, expected.x, expected.y,
        actual.valid ? 1 : 0, actual.x, actual.y,
        matrix ? matrix[0] : 0.0f, matrix ? matrix[1] : 0.0f,
        matrix ? matrix[2] : 0.0f, matrix ? matrix[3] : 0.0f,
        matrix ? matrix[4] : 0.0f, matrix ? matrix[5] : 0.0f);
    std::exit(1);
}

void CheckPoint(
    const char* label,
    const float* matrix,
    bool isTop,
    int x,
    int y)
{
    for (const bool clamp : {false, true}) {
        const Result expected = ReferenceMap(matrix, isTop, x, y, clamp);
        const Result actual = ProductionMap(matrix, isTop, x, y, clamp);
        if (expected.valid != actual.valid
            || expected.x != actual.x
            || expected.y != actual.y)
            Fail(label, matrix, x, y, clamp, expected, actual);
    }
}

void CheckMatrix(const char* label, const float* matrix, bool isTop)
{
    // Exact host-space edges and their nearest integer neighbours exercise
    // every cast, bounds and clamp boundary after rotation and translation.
    constexpr std::array<std::array<float, 2>, 13> dsPoints{{
        {{0.0f, 0.0f}}, {{255.0f, 0.0f}}, {{256.0f, 0.0f}},
        {{0.0f, 191.0f}}, {{0.0f, 192.0f}}, {{255.0f, 191.0f}},
        {{256.0f, 192.0f}}, {{128.0f, 96.0f}}, {{-1.0f, -1.0f}},
        {{257.0f, 193.0f}}, {{63.0f, 47.0f}}, {{127.0f, 95.0f}},
        {{191.0f, 143.0f}},
    }};

    for (const auto& point : dsPoints) {
        const float hostX = point[0] * matrix[0] + point[1] * matrix[2]
            + matrix[4];
        const float hostY = point[0] * matrix[1] + point[1] * matrix[3]
            + matrix[5];
        const int baseX = static_cast<int>(std::lround(hostX));
        const int baseY = static_cast<int>(std::lround(hostY));
        for (int oy = -2; oy <= 2; ++oy)
            for (int ox = -2; ox <= 2; ++ox)
                CheckPoint(label, matrix, isTop, baseX + ox, baseY + oy);
    }
}

void CheckLayouts()
{
    constexpr std::array<std::array<int, 2>, 5> sizes{{
        {{256, 384}}, {{512, 768}}, {{1024, 1536}},
        {{4096, 6144}}, {{997, 613}},
    }};
    constexpr std::array<int, 4> gaps{{0, 1, 8, 37}};
    constexpr std::array<float, 3> aspects{{0.75f, 1.0f, 1.3333334f}};

    std::uint64_t matrices = 0;
    for (const auto& size : sizes)
    for (int layoutValue = screenLayout_Natural;
         layoutValue < screenLayout_MAX; ++layoutValue)
    for (int rotationValue = screenRot_0Deg;
         rotationValue < screenRot_MAX; ++rotationValue)
    for (int sizingValue = screenSizing_Even;
         sizingValue < screenSizing_MAX; ++sizingValue)
    for (const bool swap : {false, true})
    for (const bool integerScale : {false, true})
    for (const int gap : gaps)
    for (const float topAspect : aspects)
    for (const float bottomAspect : aspects) {
        ScreenLayout layout;
        layout.Setup(
            size[0], size[1],
            static_cast<ScreenLayoutType>(layoutValue),
            static_cast<ScreenRotation>(rotationValue),
            static_cast<ScreenSizing>(sizingValue),
            gap, integerScale, swap, topAspect, bottomAspect);

        float matrix[kMaxScreenTransforms][6]{};
        int kind[kMaxScreenTransforms]{};
        const int count = layout.GetScreenTransforms(matrix[0], kind);
        for (int i = 0; i < count; ++i) {
            CheckMatrix("layout", matrix[i], kind[i] == 0);
            ++matrices;
        }
    }

    std::printf("layout matrices checked: %llu\n",
        static_cast<unsigned long long>(matrices));
}

void CheckDegenerateAndNegativeTranslation()
{
    const float negativeTranslation[6] =
        {4.0f, 0.0f, 0.0f, 4.0f, -513.25f, -385.75f};
    CheckMatrix("negative-translation", negativeTranslation, true);

    const float nearDegenerate[6] =
        {0.0000005f, 0.0f, 0.0f, 1.0f, -10.0f, -20.0f};
    CheckMatrix("near-degenerate", nearDegenerate, true);

    const float identity[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    CheckMatrix("bottom-kind", identity, false);
}

} // namespace

int main()
{
    CheckLayouts();
    CheckDegenerateAndNegativeTranslation();
    std::puts("PASS: top-screen touch precomputed inverse matches the old path");
    return 0;
}
