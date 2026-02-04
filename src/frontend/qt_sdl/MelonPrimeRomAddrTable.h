#ifndef MELON_PRIME_ROM_ADDR_TABLE_H
#define MELON_PRIME_ROM_ADDR_TABLE_H

#include <cstdint>
#include <array>
#include <cassert>

namespace MelonPrime {

    struct RomAddrs
    {
        uint32_t addrBaseChosenHunter;
        uint32_t addrInGame;
        uint32_t addrPlayerPos;
        uint32_t addrBaseIsAltForm;
        uint32_t addrBaseWeaponChange;
        uint32_t addrBaseSelectedWeapon;
        uint32_t addrBaseAimX;
        uint32_t addrBaseAimY;
        uint32_t addrIsInAdventure;
        uint32_t addrIsMapOrUserActionPaused;
        uint32_t addrUnlockMapsHunters;
        uint32_t addrSensitivity;
        uint32_t addrMainHunter;
        uint32_t addrBaseLoadedSpecialWeapon;
    };

    enum class RomGroup : int {
        US1_1 = 0, US1_0, EU1_1, EU1_0, JP1_0, JP1_1, KR1_0, COUNT
    };

    namespace Consts {
        constexpr uint32_t JP1_0_BASE_IS_ALT_FORM = 0x020DC6D8;
        constexpr uint32_t JP1_0_BASE_WEAPON_CHANGE = 0x020DCA9B;
        constexpr uint32_t JP1_0_BASE_SELECTED_WEAPON = 0x020DCAA3;
        constexpr uint32_t ABS_US11_IS_ALT_FORM = 0x020DB098;
        constexpr uint32_t ABS_US11_WEAPON_CHANGE = 0x020DB45B;
        constexpr uint32_t ABS_US11_SELECTED_WEAPON = 0x020DB463;
        constexpr uint32_t OFF_US10_FROM_JP10 = 0x1EC0;
        constexpr uint32_t OFF_EU11_FROM_JP10 = 0x15A0;
        constexpr uint32_t OFF_EU10_FROM_JP10 = 0x1620;
        constexpr uint32_t OFF_JP11_ALT_FROM_JP10 = 0x64;
        constexpr uint32_t OFF_JP11_WEAPON_FROM_JP10 = 0x40;
        constexpr uint32_t OFF_KR10_FROM_JP10 = 0x87F4;
    }

    static constexpr std::array<RomAddrs, static_cast<size_t>(RomGroup::COUNT)> kRomAddrTable = { {
            // US1.1
            {
                0x020CBDA4, 0x020EEC40u + 0x8F0u, 0x020DA538,
                Consts::ABS_US11_IS_ALT_FORM, Consts::ABS_US11_WEAPON_CHANGE, Consts::ABS_US11_SELECTED_WEAPON,
                0x020DEDA6, 0x020DEDAEu, 0x020E83BC, 0x020FBF18, 0x020E8319, 0x020E832C,
                0x020E83BC + 0x3504, Consts::ABS_US11_IS_ALT_FORM + 0x56
            },
        // US1.0
        {
            0x020CB51C, 0x020EE180u + 0x8F0u, 0x020D9CB8,
            Consts::JP1_0_BASE_IS_ALT_FORM - Consts::OFF_US10_FROM_JP10,
            Consts::JP1_0_BASE_WEAPON_CHANGE - Consts::OFF_US10_FROM_JP10,
            Consts::JP1_0_BASE_SELECTED_WEAPON - Consts::OFF_US10_FROM_JP10,
            0x020DE526, 0x020DE52Eu, 0x020E78FC, 0x020FB458, 0x020E7859, 0x020E786C,
            0x020E78FC + 0x3504, Consts::JP1_0_BASE_IS_ALT_FORM - Consts::OFF_US10_FROM_JP10 + 0x56
        },
        // EU1.1
        {
            0x020CBE44, 0x020EECE0u + 0x8F0u, 0x020DA5D8,
            Consts::JP1_0_BASE_IS_ALT_FORM - Consts::OFF_EU11_FROM_JP10,
            Consts::JP1_0_BASE_WEAPON_CHANGE - Consts::OFF_EU11_FROM_JP10,
            Consts::JP1_0_BASE_SELECTED_WEAPON - Consts::OFF_EU11_FROM_JP10,
            0x020DEE46, 0x020DEE4Eu, 0x020E845C, 0x020FBFB8, 0x020E83B9, 0x020E83CC,
            0x020E845C + 0x3504, Consts::JP1_0_BASE_IS_ALT_FORM - Consts::OFF_EU11_FROM_JP10 + 0x56
        },
        // EU1.0
        {
            0x020CBDC4, 0x020EEC60u + 0x8F0u, 0x020DA558,
            Consts::JP1_0_BASE_IS_ALT_FORM - Consts::OFF_EU10_FROM_JP10,
            Consts::JP1_0_BASE_WEAPON_CHANGE - Consts::OFF_EU10_FROM_JP10,
            Consts::JP1_0_BASE_SELECTED_WEAPON - Consts::OFF_EU10_FROM_JP10,
            0x020DEDC6, 0x020DEDCEu, 0x020E83DC, 0x020FBF38, 0x020E8339, 0x020E834C,
            0x020E83DC + 0x3504, Consts::JP1_0_BASE_IS_ALT_FORM - Consts::OFF_EU10_FROM_JP10 + 0x56
        },
        // JP1.0
        {
            0x020CD358, 0x020F0BB0, 0x020DBB78,
            Consts::JP1_0_BASE_IS_ALT_FORM, Consts::JP1_0_BASE_WEAPON_CHANGE, Consts::JP1_0_BASE_SELECTED_WEAPON,
            0x020E03E6, 0x020E03EE, 0x020E9A3C, 0x020FD598, 0x020E9999, 0x020E99AC,
            0x020ECF40, Consts::JP1_0_BASE_IS_ALT_FORM + 0x56
        },
        // JP1.1
        {
            0x020CD318, 0x020F0280u + 0x8F0u, 0x020DBB38,
            0x020DC698,
            Consts::JP1_0_BASE_WEAPON_CHANGE - Consts::OFF_JP11_WEAPON_FROM_JP10,
            Consts::JP1_0_BASE_SELECTED_WEAPON - Consts::OFF_JP11_WEAPON_FROM_JP10,
            0x020E03A6, 0x020E03AEu, 0x020E99FC, 0x020FD558, 0x020E9959, 0x020E996C,
            0x020E99FC + 0x3504, 0x020DC6EE
        },
        // KR1.0
        {
            0x020C4B88, 0x020E81B4, 0x020D33A9,
            Consts::JP1_0_BASE_IS_ALT_FORM - Consts::OFF_KR10_FROM_JP10,
            Consts::JP1_0_BASE_WEAPON_CHANGE - Consts::OFF_KR10_FROM_JP10,
            Consts::JP1_0_BASE_SELECTED_WEAPON - Consts::OFF_KR10_FROM_JP10,
            0x020D7C0E, 0x020D7C16u, 0x020E11F8, 0x020F4CF8, 0x020E1155, 0x020E1168,
            0x020E44BC, Consts::JP1_0_BASE_IS_ALT_FORM - Consts::OFF_KR10_FROM_JP10 + 0x56
        }
    } };

    static_assert(kRomAddrTable.size() == static_cast<size_t>(RomGroup::COUNT), "ROM address table size mismatch.");

    [[nodiscard]] static inline const RomAddrs& getRomAddrs(RomGroup group) noexcept
    {
        assert(static_cast<int>(group) >= 0 && static_cast<int>(group) < static_cast<int>(RomGroup::COUNT));
        return kRomAddrTable[static_cast<size_t>(group)];
    }

    // 互換性ラッパー
    static inline void detectRomAndSetAddresses_fast(
        RomGroup group,
        uint32_t& addrBaseChosenHunter_, uint32_t& addrInGame_, uint32_t& addrPlayerPos_,
        uint32_t& addrBaseIsAltForm_, uint32_t& addrBaseWeaponChange_, uint32_t& addrBaseSelectedWeapon_,
        uint32_t& addrBaseAimX_, uint32_t& addrBaseAimY_, uint32_t& addrIsInAdventure_,
        uint32_t& addrIsMapOrUserActionPaused_, uint32_t& addrUnlockMapsHunters,
        uint32_t& addrSensitivity, uint32_t& addrMainHunter, uint32_t& addrBaseLoadedSpecialWeapon
    ) noexcept
    {
        const RomAddrs& a = getRomAddrs(group);
        addrBaseChosenHunter_ = a.addrBaseChosenHunter;
        addrInGame_ = a.addrInGame;
        addrPlayerPos_ = a.addrPlayerPos;
        addrBaseIsAltForm_ = a.addrBaseIsAltForm;
        addrBaseWeaponChange_ = a.addrBaseWeaponChange;
        addrBaseSelectedWeapon_ = a.addrBaseSelectedWeapon;
        addrBaseAimX_ = a.addrBaseAimX;
        addrBaseAimY_ = a.addrBaseAimY;
        addrIsInAdventure_ = a.addrIsInAdventure;
        addrIsMapOrUserActionPaused_ = a.addrIsMapOrUserActionPaused;
        addrUnlockMapsHunters = a.addrUnlockMapsHunters;
        addrSensitivity = a.addrSensitivity;
        addrMainHunter = a.addrMainHunter;
        addrBaseLoadedSpecialWeapon = a.addrBaseLoadedSpecialWeapon;
    }

} // namespace MelonPrime

#endif // MELON_PRIME_ROM_ADDR_TABLE_H