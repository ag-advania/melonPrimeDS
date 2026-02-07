#ifndef MELONPRIME_INTERNAL_H
#define MELONPRIME_INTERNAL_H

#include <cstdint>
#include <array>
#include <string_view>
#include <QPoint>

#include "types.h"
#include "MelonPrime.h"

namespace MelonPrime {

    namespace Consts {
        constexpr int32_t PLAYER_ADDR_INC = 0xF30;
        constexpr uint8_t AIM_ADDR_INC = 0x48;
        constexpr uint32_t VISOR_OFFSET = 0xABB;
        constexpr uint32_t RAM_MASK = 0x3FFFFF;

        namespace UI {
            constexpr QPoint SCAN_VISOR_BUTTON{ 128, 173 };
            constexpr QPoint OK{ 128, 142 };
            constexpr QPoint LEFT{ 71, 141 };
            constexpr QPoint RIGHT{ 185, 141 };
            constexpr QPoint YES{ 96, 142 };
            constexpr QPoint NO{ 160, 142 };
            constexpr QPoint CENTER_RESET{ 128, 88 };
            constexpr QPoint WEAPON_CHECK_START{ 236, 30 };
            constexpr QPoint MORPH_START{ 231, 167 };
        }
    }

    enum InputBit : uint16_t {
        INPUT_A = 0, INPUT_B = 1, INPUT_SELECT = 2, INPUT_START = 3,
        INPUT_RIGHT = 4, INPUT_LEFT = 5, INPUT_UP = 6, INPUT_DOWN = 7,
        INPUT_R = 8, INPUT_L = 9, INPUT_X = 10, INPUT_Y = 11,
    };

    FORCE_INLINE uint8_t Read8(const melonDS::u8* ram, melonDS::u32 addr) {
        return ram[addr & Consts::RAM_MASK];
    }
    FORCE_INLINE uint16_t Read16(const melonDS::u8* ram, melonDS::u32 addr) {
        return *reinterpret_cast<const uint16_t*>(&ram[addr & Consts::RAM_MASK]);
    }
    FORCE_INLINE uint32_t Read32(const melonDS::u8* ram, melonDS::u32 addr) {
        return *reinterpret_cast<const uint32_t*>(&ram[addr & Consts::RAM_MASK]);
    }
    FORCE_INLINE void Write8(melonDS::u8* ram, melonDS::u32 addr, uint8_t val) {
        ram[addr & Consts::RAM_MASK] = val;
    }
    FORCE_INLINE void Write16(melonDS::u8* ram, melonDS::u32 addr, uint16_t val) {
        *reinterpret_cast<uint16_t*>(&ram[addr & Consts::RAM_MASK]) = val;
    }

} // namespace MelonPrime

#endif // MELONPRIME_INTERNAL_H
