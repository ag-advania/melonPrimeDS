#ifndef MELON_PRIME_ROM_ADDR_TABLE_H
#define MELON_PRIME_ROM_ADDR_TABLE_H

#include <cstdint>
#include <array>
#include <cassert>

namespace MelonPrime {

    enum class RomGroup : int {
        JP1_0 = 0, JP1_1, US1_0, US1_1, EU1_0, EU1_1, KR1_0, COUNT
    };

    namespace Consts {
        constexpr uint32_t PLAYER_STRUCT_SIZE = 0xF30;
    }

    // ==========================================================
    //  1. データ定義 (横並びで見やすく管理)
    // ==========================================================

    // --- Player 1 Base Addresses ---
    static constexpr uint32_t LIST_PlayerStructStart[] = { 0x020DC5D4, 0x020DC594, 0x020DA714, 0x020DAF94, 0x020DAFB4, 0x020DB034, 0x020D3DE0 };
    static constexpr uint32_t LIST_PlayerPos[] = { 0x020DBB78, 0x020DBB38, 0x020D9CB8, 0x020DA538, 0x020DA558, 0x020DA5D8, 0x020D33A9 };
    static constexpr uint32_t LIST_IsInVisorOrMap[] = { 0x020DB0BD, 0x020DB07D, 0x020D91FD, 0x020D9A7D, 0x020D9A9D, 0x020D9B1D, 0x020D28EE };
    static constexpr uint32_t LIST_PlayerHP[] = { 0x020DC6AE, 0x020DC66E, 0x020DA7EE, 0x020DB06E, 0x020DB08E, 0x020DB10E, 0x020D3EBA };
    static constexpr uint32_t LIST_BaseIsAltForm[] = { 0x020DC6D8, 0x020DC698, 0x020DA818, 0x020DB098, 0x020DB0B8, 0x020DB138, 0x020D3EE4 };
    static constexpr uint32_t LIST_BoostGauge[] = { 0x020DC71C, 0x020DC6DC, 0x020DA85C, 0x020DB0DC, 0x020DB0FC, 0x020DB17C, 0x020D3F28 };
    static constexpr uint32_t LIST_IsBoosting[] = { 0x020DC71E, 0x020DC6DE, 0x020DA85E, 0x020DB0DE, 0x020DB0FE, 0x020DB17E, 0x020D3F2A };
    static constexpr uint32_t LIST_BaseLoadedSpecialWeapon[] = { 0x020DC72E, 0x020DC6EE, 0x020DA86E, 0x020DB0EE, 0x020DB10E, 0x020DB18E, 0x020D3F3A };
    static constexpr uint32_t LIST_BaseJumpFlag[] = { 0x020DCA99, 0x020DCA59, 0x020DABD9, 0x020DB459, 0x020DB479, 0x020DB4F9, 0x020D42A5 };
    static constexpr uint32_t LIST_BaseWeaponChange[] = { 0x020DCA9B, 0x020DCA5B, 0x020DABDB, 0x020DB45B, 0x020DB47B, 0x020DB4FB, 0x020D42A7 };
    static constexpr uint32_t LIST_BaseSelectedWeapon[] = { 0x020DCAA3, 0x020DCA63, 0x020DABE3, 0x020DB463, 0x020DB483, 0x020DB503, 0x020D42AF };

    // ★追加: 武器関連アドレス (計算済み)
    static constexpr uint32_t LIST_BaseCurrentWeapon[] = { 0x020DCAA2, 0x020DCA62, 0x020DABE2, 0x020DB462, 0x020DB482, 0x020DB502, 0x020D42AE };
    static constexpr uint32_t LIST_BaseHavingWeapons[] = { 0x020DCAA6, 0x020DCA66, 0x020DABE6, 0x020DB466, 0x020DB486, 0x020DB506, 0x020D42B2 };
    static constexpr uint32_t LIST_BaseWeaponAmmo[] = { 0x020DC720, 0x020DC6E0, 0x020DA860, 0x020DB0E0, 0x020DB100, 0x020DB180, 0x020D3F2C };

    static constexpr uint32_t LIST_WeaponDataCurrent[] = { 0x020DCE2C, 0x020DCDEC, 0x020DAF6C, 0x020DB7EC, 0x020DB80C, 0x020DB88C, 0x020D4638 };
    static constexpr uint32_t LIST_BaseAimX[] = { 0x020E03E6, 0x020E03A6, 0x020DE526, 0x020DEDA6, 0x020DEDC6, 0x020DEE46, 0x020D7C0E };
    static constexpr uint32_t LIST_BaseAimY[] = { 0x020E03EE, 0x020E03AE, 0x020DE52E, 0x020DEDAE, 0x020DEDCE, 0x020DEE4E, 0x020D7C16 };

    // --- System / Global Addresses ---
    static constexpr uint32_t LIST_BaseChosenHunter[] = { 0x020CD358, 0x020CD318, 0x020CB51C, 0x020CBDA4, 0x020CBDC4, 0x020CBE44, 0x020C4B88 };
    static constexpr uint32_t LIST_InGame[] = { 0x020F0BB0, 0x020F0B70, 0x020EEA70, 0x020EF530, 0x020EF550, 0x020EF5D0, 0x020E81B4 };
    static constexpr uint32_t LIST_IsInAdventure[] = { 0x020E9A3C, 0x020E99FC, 0x020E78FC, 0x020E83BC, 0x020E83DC, 0x020E845C, 0x020E11F8 };
    static constexpr uint32_t LIST_IsMapOrUserActionPaused[] = { 0x020FD598, 0x020FD558, 0x020FB458, 0x020FBF18, 0x020FBF38, 0x020FBFB8, 0x020F4CF8 };
    static constexpr uint32_t LIST_OperationAndSound[] = { 0x020E9998, 0x020E9958, 0x020E7858, 0x020E8318, 0x020E8338, 0x020E83B8, 0x020E1154 };
    static constexpr uint32_t LIST_UnlockMapsHunters[] = { 0x020E9999, 0x020E9959, 0x020E7859, 0x020E8319, 0x020E8339, 0x020E83B9, 0x020E1155 };
    static constexpr uint32_t LIST_VolSfx8Bit[] = { 0x020E9999, 0x020E9959, 0x020E7859, 0x020E8319, 0x020E8339, 0x020E83B9, 0x020E1155 };
    static constexpr uint32_t LIST_VolMusic8Bit[] = { 0x020E999A, 0x020E995A, 0x020E785A, 0x020E831A, 0x020E833A, 0x020E83BA, 0x020E1156 };
    static constexpr uint32_t LIST_UnlockMapsHunters2[] = { 0x020E999C, 0x020E995C, 0x020E785C, 0x020E831C, 0x020E833C, 0x020E83BC, 0x020E1158 };
    static constexpr uint32_t LIST_UnlockMapsHunters3[] = { 0x020E99A0, 0x020E9960, 0x020E7860, 0x020E8320, 0x020E8340, 0x020E83C0, 0x020E115C };
    static constexpr uint32_t LIST_UnlockMapsHunters4[] = { 0x020E99A4, 0x020E9964, 0x020E7864, 0x020E8324, 0x020E8344, 0x020E83C4, 0x020E1160 };
    static constexpr uint32_t LIST_UnlockMapsHunters5[] = { 0x020E99A8, 0x020E9968, 0x020E7868, 0x020E8328, 0x020E8348, 0x020E83C8, 0x020E1164 };
    static constexpr uint32_t LIST_DsNameFlagAndMicVolume[] = { 0x020E99A9, 0x020E9969, 0x020E7869, 0x020E8329, 0x020E8349, 0x020E83C9, 0x020E1165 };
    static constexpr uint32_t LIST_Sensitivity[] = { 0x020E99AC, 0x020E996C, 0x020E786C, 0x020E832C, 0x020E834C, 0x020E83CC, 0x020E1168 };
    static constexpr uint32_t LIST_BaseInGameSensi[] = { 0x020E9A60, 0x020E9A20, 0x020E7920, 0x020E83E0, 0x020E8400, 0x020E8480, 0x020E121C };
    static constexpr uint32_t LIST_MainHunter[] = { 0x020ECF40, 0x020ECF00, 0x020EAE00, 0x020EB8C0, 0x020EB8E0, 0x020EB960, 0x020E44BC };
    static constexpr uint32_t LIST_RankColor[] = { 0x020ECF43, 0x020ECF03, 0x020EAE03, 0x020EB8C3, 0x020EB8E3, 0x020EB963, 0x020E44BF };


    // ==========================================================
    //  2. 構造体定義 (ポインタアクセス用)
    // ==========================================================
    struct RomAddresses {
        // Player 1
        uint32_t playerStructStart;
        uint32_t playerPos;
        uint32_t isInVisorOrMap;
        uint32_t playerHP;
        uint32_t baseIsAltForm;
        uint32_t boostGauge;
        uint32_t isBoosting;
        uint32_t baseLoadedSpecialWeapon;
        uint32_t baseJumpFlag;
        uint32_t baseWeaponChange;
        uint32_t baseSelectedWeapon;

        // ★追加
        uint32_t baseCurrentWeapon;
        uint32_t baseHavingWeapons;
        uint32_t baseWeaponAmmo;

        uint32_t weaponDataCurrent;
        uint32_t baseAimX;
        uint32_t baseAimY;

        // System
        uint32_t baseChosenHunter;
        uint32_t inGame;
        uint32_t isInAdventure;
        uint32_t isMapOrUserActionPaused;
        uint32_t operationAndSound;
        uint32_t unlockMapsHunters;
        uint32_t volSfx8Bit;
        uint32_t volMusic8Bit;
        uint32_t unlockMapsHunters2;
        uint32_t unlockMapsHunters3;
        uint32_t unlockMapsHunters4;
        uint32_t unlockMapsHunters5;
        uint32_t dsNameFlagAndMicVolume;
        uint32_t sensitivity;
        uint32_t baseInGameSensi;
        uint32_t mainHunter;
        uint32_t rankColor;
    };


    // ==========================================================
    //  3. 変換処理 (リストから構造体の配列をコンパイル時に生成)
    // ==========================================================

    constexpr RomAddresses CreateRomAddress(size_t i) {
        return {
            LIST_PlayerStructStart[i],
            LIST_PlayerPos[i],
            LIST_IsInVisorOrMap[i],
            LIST_PlayerHP[i],
            LIST_BaseIsAltForm[i],
            LIST_BoostGauge[i],
            LIST_IsBoosting[i],
            LIST_BaseLoadedSpecialWeapon[i],
            LIST_BaseJumpFlag[i],
            LIST_BaseWeaponChange[i],
            LIST_BaseSelectedWeapon[i],

            // ★追加
            LIST_BaseCurrentWeapon[i],
            LIST_BaseHavingWeapons[i],
            LIST_BaseWeaponAmmo[i],

            LIST_WeaponDataCurrent[i],
            LIST_BaseAimX[i],
            LIST_BaseAimY[i],

            LIST_BaseChosenHunter[i],
            LIST_InGame[i],
            LIST_IsInAdventure[i],
            LIST_IsMapOrUserActionPaused[i],
            LIST_OperationAndSound[i],
            LIST_UnlockMapsHunters[i],
            LIST_VolSfx8Bit[i],
            LIST_VolMusic8Bit[i],
            LIST_UnlockMapsHunters2[i],
            LIST_UnlockMapsHunters3[i],
            LIST_UnlockMapsHunters4[i],
            LIST_UnlockMapsHunters5[i],
            LIST_DsNameFlagAndMicVolume[i],
            LIST_Sensitivity[i],
            LIST_BaseInGameSensi[i],
            LIST_MainHunter[i],
            LIST_RankColor[i]
        };
    }

    static constexpr std::array<RomAddresses, static_cast<size_t>(RomGroup::COUNT)> ROM_INSTANCES = {
        CreateRomAddress(0), // JP1.0
        CreateRomAddress(1), // JP1.1
        CreateRomAddress(2), // US1.0
        CreateRomAddress(3), // US1.1
        CreateRomAddress(4), // EU1.0
        CreateRomAddress(5), // EU1.1
        CreateRomAddress(6)  // KR1.0
    };

    [[nodiscard]] static inline const RomAddresses* getRomAddrsPtr(RomGroup group) noexcept
    {
        size_t idx = static_cast<size_t>(group);
        assert(idx < ROM_INSTANCES.size());
        return &ROM_INSTANCES[idx];
    }

} // namespace MelonPrime

#endif // MELON_PRIME_ROM_ADDR_TABLE_H