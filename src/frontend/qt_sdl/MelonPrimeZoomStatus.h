#ifndef MELON_PRIME_ZOOM_STATUS_H
#define MELON_PRIME_ZOOM_STATUS_H

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

    [[nodiscard]] FORCE_INLINE float ComputeReticleAmount(bool scoped,
                                                          uint16_t currentFrame,
                                                          bool animActive) noexcept
    {
        if (!animActive)
            return scoped ? 1.0f : 0.0f;
        if (currentFrame <= 2)
            return static_cast<float>(currentFrame) * 0.5f;
        if (currentFrame >= 0x10 && currentFrame <= 0x12)
            return 1.0f - static_cast<float>(currentFrame - 0x10) * 0.5f;
        return scoped ? 1.0f : 0.0f;
    }

} // namespace MelonPrime::ZoomStatus

#endif // MELON_PRIME_ZOOM_STATUS_H
