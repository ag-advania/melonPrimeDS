#ifndef MELONPRIME_INTERNAL_H
#define MELONPRIME_INTERNAL_H

#include <cstdint>
#include <cstring>
#include <array>
#include <string_view>
#include <QPoint>

#include "types.h"
#include "MelonPrime.h"

namespace MelonPrime {

    namespace Consts {
        constexpr int32_t  PLAYER_ADDR_INC = 0xF30;
        constexpr uint8_t  AIM_ADDR_INC    = 0x48;
        constexpr uint32_t RAM_MASK        = 0x3FFFFF;

        namespace UI {
            constexpr QPoint SCAN_VISOR_BUTTON{ 128, 173 };
            constexpr QPoint OK              { 128, 142 };
            constexpr QPoint LEFT            {  71, 141 };
            constexpr QPoint RIGHT           { 185, 141 };
            constexpr QPoint YES             {  96, 142 };
            constexpr QPoint NO              { 160, 142 };
            constexpr QPoint CENTER_RESET    { 128,  88 };
            constexpr QPoint WEAPON_CHECK_START{ 236, 30 };
            constexpr QPoint MORPH_START     { 231, 167 };
        }
    }

    enum InputBit : uint16_t {
        INPUT_A = 0,  INPUT_B = 1,  INPUT_SELECT = 2,  INPUT_START = 3,
        INPUT_RIGHT = 4, INPUT_LEFT = 5, INPUT_UP = 6, INPUT_DOWN = 7,
        INPUT_R = 8, INPUT_L = 9, INPUT_X = 10, INPUT_Y = 11,
    };

    // Strict-aliasing safe RAM accessors.
    // Compilers optimize memcpy to a single MOV on aligned accesses.

    [[nodiscard]] FORCE_INLINE uint8_t Read8(const melonDS::u8* ram, melonDS::u32 addr) {
        return ram[addr & Consts::RAM_MASK];
    }

    [[nodiscard]] FORCE_INLINE uint16_t Read16(const melonDS::u8* ram, melonDS::u32 addr) {
        uint16_t val;
        std::memcpy(&val, &ram[addr & Consts::RAM_MASK], sizeof(uint16_t));
        return val;
    }

    [[nodiscard]] FORCE_INLINE uint32_t Read32(const melonDS::u8* ram, melonDS::u32 addr) {
        uint32_t val;
        std::memcpy(&val, &ram[addr & Consts::RAM_MASK], sizeof(uint32_t));
        return val;
    }

    FORCE_INLINE void Write8(melonDS::u8* ram, melonDS::u32 addr, uint8_t val) {
        ram[addr & Consts::RAM_MASK] = val;
    }

    FORCE_INLINE void Write16(melonDS::u8* ram, melonDS::u32 addr, uint16_t val) {
        std::memcpy(&ram[addr & Consts::RAM_MASK], &val, sizeof(uint16_t));
    }

} // namespace MelonPrime

#endif // MELONPRIME_INTERNAL_H
