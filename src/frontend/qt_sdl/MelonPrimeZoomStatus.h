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
    inline constexpr int kCrosshairZoomTransitionStyleCount = 16;

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

    [[nodiscard]] FORCE_INLINE float QuantizeSteps(float t, int steps) noexcept
    {
        if (steps <= 1)
            return t;
        t = std::clamp(t, 0.0f, 1.0f);
        return std::floor(t * static_cast<float>(steps) + 0.001f) / static_cast<float>(steps);
    }

    struct CrosshairZoomBlend {
        float geom = 0.0f;
        float baseAlpha = 1.0f;
        float scope = 0.0f;
        float pulse = 0.0f;
        int fxStyle = 0; // mirrors transition style for optional draw FX
    };

    [[nodiscard]] FORCE_INLINE CrosshairZoomBlend ComputeCrosshairZoomBlendStaged(
        float t, float baseZoomOpacity, float pulseStrength) noexcept
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

    [[nodiscard]] FORCE_INLINE int RemapCrosshairZoomTransitionStyle(int style) noexcept
    {
        // Only remap legacy IDs from the old 0..18 list. Current schema is 0..15;
        // do not remap in-range values (Glitch2 is 3 in the new list, not old Snap).
        if (style <= 15)
            return style;
        if (style == 16)
            return 15; // Beam Charge
        if (style == 17)
            return 14; // Wireframe -> Drone LIDAR
        return 12;     // Data Link / unknown -> Tactical Lock
    }

    [[nodiscard]] FORCE_INLINE CrosshairZoomBlend ComputeCrosshairZoomBlend(
        float t, float baseZoomOpacity, float pulseStrength, int style) noexcept
    {
        style = RemapCrosshairZoomTransitionStyle(style);
        style = std::clamp(style, 0, kCrosshairZoomTransitionStyleCount - 1);
        t = std::clamp(t, 0.0f, 1.0f);
        pulseStrength = std::clamp(pulseStrength, 0.0f, 1.0f);

        CrosshairZoomBlend b{};
        b.fxStyle = style;

        switch (style) {
        default:
        case 0: // Staged
            return ComputeCrosshairZoomBlendStaged(t, baseZoomOpacity, pulseStrength);

        case 1: { // Fade — opacity crossfade only
            const float fade = SmoothStep(t);
            b.geom = fade;
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * fade) * (1.0f - fade);
            b.scope = fade;
            break;
        }
        case 2: { // Glitch — RGB break / slice bursts during mid-transition
            const float staged = SmoothStep(t);
            const float slice = 0.88f + 0.12f * std::sin(t * 26.0f);
            b.geom = staged;
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * staged)
                        * (1.0f - SmoothStep(std::clamp((t - 0.06f) / 0.70f, 0.0f, 1.0f)))
                        * slice;
            b.scope = SmoothStep(std::clamp((t - 0.14f) / 0.80f, 0.0f, 1.0f));
            b.pulse = (t > 0.04f && t < 0.96f)
                ? std::abs(std::sin(t * 22.0f)) * pulseStrength * 0.42f
                : 0.0f;
            break;
        }
        case 3: { // Glitch2 — scan overload flicker (jam affects alpha only, not geom)
            const float staged = SmoothStep(t);
            const float jam = 0.84f + 0.16f * std::abs(std::sin(t * 30.0f));
            b.geom = staged;
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * staged)
                        * (1.0f - SmoothStep(std::clamp((t - 0.04f) / 0.68f, 0.0f, 1.0f)))
                        * jam;
            b.scope = SmoothStep(std::clamp((t - 0.12f) / 0.78f, 0.0f, 1.0f));
            b.pulse = (t > 0.02f && t < 0.98f)
                ? std::abs(std::sin(t * 24.0f)) * pulseStrength * 0.50f
                : 0.0f;
            break;
        }
        case 4: { // Snap
            const float qt = QuantizeSteps(t, 5);
            b.geom = qt;
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * qt) * (1.0f - qt);
            b.scope = qt;
            b.pulse = (qt > 0.01f && qt < 0.99f) ? pulseStrength * 0.5f : 0.0f;
            break;
        }
        case 5: { // Expand
            b.scope = SmoothStep(std::clamp(t / 0.72f, 0.0f, 1.0f));
            b.geom = b.scope;
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * t) * (1.0f - b.scope);
            break;
        }
        case 6: { // Contract — geom must be 0 when t=0 (not zoomed)
            const float shrink = SmoothStep(std::clamp(t / 0.55f, 0.0f, 1.0f));
            b.geom = shrink * (1.0f - 0.35f * shrink);
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * shrink) * (1.0f - shrink);
            b.scope = SmoothStep(std::clamp((t - 0.40f) / 0.60f, 0.0f, 1.0f));
            break;
        }
        case 7: { // Digital
            const float qt = QuantizeSteps(t, 8);
            b.geom = qt;
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * qt) * (1.0f - qt);
            b.scope = qt;
            break;
        }
        case 8: { // Pulse Wave
            const float wave = 0.5f + 0.5f * std::sin(t * 6.2831853f * 2.0f);
            b.geom = SmoothStep(t);
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * b.geom) * (1.0f - t) * wave;
            b.scope = SmoothStep(t);
            b.pulse = wave * pulseStrength;
            break;
        }
        case 9: { // Crossfade — both layers overlap by opacity
            const float fade = SmoothStep(t);
            b.geom = fade;
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * fade) * (1.0f - fade);
            b.scope = fade;
            break;
        }
        case 10: { // Magic Circle
            const float staged = SmoothStep(t);
            b.geom = staged;
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * staged)
                        * (1.0f - SmoothStep(std::clamp((t - 0.12f) / 0.58f, 0.0f, 1.0f)));
            b.scope = SmoothStep(std::clamp((t - 0.22f) / 0.78f, 0.0f, 1.0f));
            b.pulse = std::sin(t * 3.14159265f) * pulseStrength * 0.45f;
            break;
        }
        case 11: { // SF Movie — scope geometry follows zoomT; glow is FX-only
            const float staged = SmoothStep(t);
            const float glow = 0.55f + 0.45f * std::sin(t * 6.2831853f);
            b.geom = staged;
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * staged)
                        * (1.0f - SmoothStep(std::clamp((t - 0.05f) / 0.65f, 0.0f, 1.0f)));
            b.scope = SmoothStep(std::clamp((t - 0.12f) / 0.88f, 0.0f, 1.0f));
            b.pulse = glow * pulseStrength * 0.75f;
            break;
        }
        case 12: { // Tactical Lock
            const float staged = SmoothStep(t);
            b.geom = staged;
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * staged)
                        * (1.0f - SmoothStep(std::clamp((t - 0.10f) / 0.62f, 0.0f, 1.0f)));
            b.scope = SmoothStep(std::clamp((t - 0.16f) / 0.84f, 0.0f, 1.0f));
            b.pulse = std::sin(t * 3.14159265f) * pulseStrength * 0.5f;
            break;
        }
        case 13: { // Sniper Optics
            const float staged = SmoothStep(t);
            b.geom = staged;
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * staged)
                        * (1.0f - SmoothStep(std::clamp((t - 0.08f) / 0.70f, 0.0f, 1.0f)));
            b.scope = SmoothStep(std::clamp((t - 0.20f) / 0.80f, 0.0f, 1.0f));
            break;
        }
        case 14: { // Drone LIDAR
            const float staged = SmoothStep(t);
            b.geom = staged;
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * staged)
                        * (1.0f - SmoothStep(std::clamp((t - 0.14f) / 0.66f, 0.0f, 1.0f)));
            b.scope = SmoothStep(std::clamp((t - 0.24f) / 0.76f, 0.0f, 1.0f));
            b.pulse = pulseStrength * 0.4f;
            break;
        }
        case 15: { // Beam Charge
            const float staged = SmoothStep(t);
            b.geom = staged;
            b.baseAlpha = (1.0f + (baseZoomOpacity - 1.0f) * staged)
                        * (1.0f - SmoothStep(std::clamp((t - 0.18f) / 0.72f, 0.0f, 1.0f)));
            b.scope = SmoothStep(std::clamp((t - 0.30f) / 0.70f, 0.0f, 1.0f));
            b.pulse = std::sin(t * 3.14159265f) * pulseStrength * 0.85f;
            break;
        }
        }

        return b;
    }

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
