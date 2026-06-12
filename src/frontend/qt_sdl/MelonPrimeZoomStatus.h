#ifndef MELON_PRIME_ZOOM_STATUS_H
#define MELON_PRIME_ZOOM_STATUS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "MelonPrimeInternal.h"

namespace MelonPrime::ZoomStatus {

    inline constexpr uint32_t kPlayerScopeFlagsOffset = 0x850;
    inline constexpr uint8_t  kScopeZoomEnabledBit = 0x01;
    inline constexpr uint32_t kCrosshairAnimActiveOffset = 0x04;
    inline constexpr uint32_t kCrosshairCurrentFrameOffset = 0x06;
    inline constexpr uint32_t kQ14One = 1u << 14;

    [[nodiscard]] FORCE_INLINE bool IsMainRamRange(uint32_t addr, uint32_t size) noexcept
    {
        return addr < 0x02400000u && size <= (0x02400000u - addr);
    }

    struct ScopeState {
        uint32_t player = 0;
        uint8_t scopeFlags = 0;
        bool valid = false;
        bool scoped = false;
    };

    [[nodiscard]] FORCE_INLINE uint32_t ReadLocalPlayer(const melonDS::u8* ram,
                                                        uint32_t localPlayerPtrGlobal) noexcept
    {
        if (!ram || !IsMainRamRange(localPlayerPtrGlobal, sizeof(uint32_t)))
            return 0;
        const uint32_t player = Read32(ram, localPlayerPtrGlobal);
        return IsMainRamRange(player, kPlayerScopeFlagsOffset + sizeof(uint8_t)) ? player : 0;
    }

    [[nodiscard]] FORCE_INLINE ScopeState ReadScopeState(const melonDS::u8* ram,
                                                         uint32_t localPlayerPtrGlobal) noexcept
    {
        ScopeState state;
        state.player = ReadLocalPlayer(ram, localPlayerPtrGlobal);
        if (!state.player)
            return state;

        state.scopeFlags = Read8(ram, state.player + kPlayerScopeFlagsOffset);
        state.valid = true;
        state.scoped = (state.scopeFlags & kScopeZoomEnabledBit) != 0;
        return state;
    }

    [[nodiscard]] FORCE_INLINE int64_t ApplyQ14Scale(int64_t value, uint32_t scaleQ14) noexcept
    {
        return (value * static_cast<int64_t>(scaleQ14) + static_cast<int64_t>(kQ14One / 2))
             >> 14;
    }

    [[nodiscard]] FORCE_INLINE float SmoothStep(float t) noexcept
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    struct CrosshairZoomBlend {
        float geom = 0.0f;      // eased geometry scale driver
        float baseAlpha = 1.0f; // base crosshair opacity multiplier
        float scope = 0.0f;     // scope reticle reveal 0..1
        float pulse = 0.0f;     // transition ring FX strength
    };

    // Staged crosshair↔scope blend: base fades first, scope expands in after.
    [[nodiscard]] FORCE_INLINE CrosshairZoomBlend ComputeCrosshairZoomBlend(float t,
                                                                            float baseZoomOpacity,
                                                                            float pulseStrength = 1.0f) noexcept
    {
        CrosshairZoomBlend b{};
        t = std::clamp(t, 0.0f, 1.0f);
        pulseStrength = std::clamp(pulseStrength, 0.0f, 1.0f);
        b.geom = SmoothStep(t);

        const float baseFade =
            1.0f - SmoothStep(std::clamp((t - 0.10f) / 0.55f, 0.0f, 1.0f));
        const float opacityTarget = 1.0f + (baseZoomOpacity - 1.0f) * b.geom;
        b.baseAlpha = opacityTarget * baseFade;

        b.scope = SmoothStep(std::clamp((t - 0.28f) / 0.72f, 0.0f, 1.0f));
        b.pulse = (t > 0.03f && t < 0.97f)
            ? std::sin(t * 3.14159265f) * pulseStrength
            : 0.0f;
        return b;
    }

    // Host-side temporal smoothing so instant scoped-bit flips still animate.
    [[nodiscard]] FORCE_INLINE float AdvanceDisplayZoom(float display,
                                                          float target,
                                                          float maxStep) noexcept
    {
        maxStep = std::max(0.001f, maxStep);
        const float diff = target - display;
        if (std::abs(diff) <= 0.001f)
            return target;
        if (diff > 0.0f)
            return std::min(target, display + maxStep);
        return std::max(target, display - maxStep);
    }

    [[nodiscard]] FORCE_INLINE float ComputeReticleAmount(bool scoped,
                                                          uint16_t currentFrame,
                                                          bool animActive) noexcept
    {
        if (!animActive)
            return scoped ? 1.0f : 0.0f;

        constexpr uint16_t kZoomInEnd = 4;
        if (currentFrame <= kZoomInEnd)
            return SmoothStep(static_cast<float>(currentFrame)
                            / static_cast<float>(kZoomInEnd));

        constexpr uint16_t kZoomOutStart = 0x10;
        constexpr uint16_t kZoomOutEnd = 0x14;
        if (currentFrame >= kZoomOutStart && currentFrame <= kZoomOutEnd) {
            const float t = static_cast<float>(currentFrame - kZoomOutStart)
                          / static_cast<float>(kZoomOutEnd - kZoomOutStart);
            return 1.0f - SmoothStep(t);
        }

        return scoped ? 1.0f : 0.0f;
    }

} // namespace MelonPrime::ZoomStatus

#endif // MELON_PRIME_ZOOM_STATUS_H
