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

    template<typename T>
    using RomTable = std::array<T, static_cast<size_t>(RomGroup::COUNT)>;

    // =========================================================================
    //  Single-source address definition (X-macro)
    //
    //  Row format:
    //    X(KIND, fieldName, ListName, JP1_0, JP1_1, US1_0, US1_1, EU1_0, EU1_1, KR1_0)
    //
    //  KIND:
    //    ADDR = main-RAM / code address; every value is static_assert'd into
    //           0x02000000-0x023FFFFF below
    //    DATA = raw 32-bit value (ARM instruction encoding etc.); no range check
    //
    //  Each row of the MP_ROM_FIELDS_* lists generates all three of:
    //    - inline constexpr RomTable<uint32_t> LIST_<ListName>
    //    - the RomAddresses::<fieldName> member (in row order)
    //    - the matching CreateRomAddress() initializer entry
    //  MP_ROM_LISTS_DS_OSD rows generate only the LIST_<ListName> array
    //  (consumed directly by MelonPrimePatchOsdColor.cpp; not in the struct).
    //
    //  Adding an address = adding ONE row to the right group below.
    //  Note: comments inside the macro bodies must be /* */ style; a //
    //  comment would swallow the line-continuation backslash.
    // =========================================================================

#define MP_ROM_FIELDS_CORE(X) \
    X(ADDR, playerStructStart,        PlayerStructStart,         0x020DC5D4u, 0x020DC594u, 0x020DA714u, 0x020DAF94u, 0x020DAFB4u, 0x020DB034u, 0x020D3DE0u) \
    X(ADDR, playerPos,                PlayerPos,                 0x020DBB78u, 0x020DBB38u, 0x020D9CB8u, 0x020DA538u, 0x020DA558u, 0x020DA5D8u, 0x020D33A9u) \
    X(ADDR, isInVisorOrMap,           IsInVisorOrMap,            0x020DB0BDu, 0x020DB07Du, 0x020D91FDu, 0x020D9A7Du, 0x020D9A9Du, 0x020D9B1Du, 0x020D28EEu) \
    X(ADDR, playerHP,                 PlayerHP,                  0x020DC6AEu, 0x020DC66Eu, 0x020DA7EEu, 0x020DB06Eu, 0x020DB08Eu, 0x020DB10Eu, 0x020D3EBAu) \
    X(ADDR, playerDoubleDamageTimer,  PlayerDoubleDamageTimer,   0x020DCA84u, 0x020DCA44u, 0x020DABC4u, 0x020DB444u, 0x020DB464u, 0x020DB4E4u, 0x020D4290u) /* Slot-0 Double Damage timer (CPlayer +0x4B0); Damage Notify Purple local-damage flash */ \
    X(ADDR, baseIsAltForm,            BaseIsAltForm,             0x020DC6D8u, 0x020DC698u, 0x020DA818u, 0x020DB098u, 0x020DB0B8u, 0x020DB138u, 0x020D3EE4u) \
    X(ADDR, boostGauge,               BoostGauge,                0x020DC71Cu, 0x020DC6DCu, 0x020DA85Cu, 0x020DB0DCu, 0x020DB0FCu, 0x020DB17Cu, 0x020D3F28u) \
    X(ADDR, isBoosting,               IsBoosting,                0x020DC71Eu, 0x020DC6DEu, 0x020DA85Eu, 0x020DB0DEu, 0x020DB0FEu, 0x020DB17Eu, 0x020D3F2Au) \
    X(ADDR, baseLoadedSpecialWeapon,  BaseLoadedSpecialWeapon,   0x020DC72Eu, 0x020DC6EEu, 0x020DA86Eu, 0x020DB0EEu, 0x020DB10Eu, 0x020DB18Eu, 0x020D3F3Au) \
    X(ADDR, baseJumpFlag,             BaseJumpFlag,              0x020DCA99u, 0x020DCA59u, 0x020DABD9u, 0x020DB459u, 0x020DB479u, 0x020DB4F9u, 0x020D42A5u) \
    X(ADDR, baseWeaponChange,         BaseWeaponChange,          0x020DCA9Bu, 0x020DCA5Bu, 0x020DABDBu, 0x020DB45Bu, 0x020DB47Bu, 0x020DB4FBu, 0x020D42A7u) \
    X(ADDR, baseSelectedWeapon,       BaseSelectedWeapon,        0x020DCAA3u, 0x020DCA63u, 0x020DABE3u, 0x020DB463u, 0x020DB483u, 0x020DB503u, 0x020D42AFu) \
    X(ADDR, baseCurrentWeapon,        BaseCurrentWeapon,         0x020DCAA2u, 0x020DCA62u, 0x020DABE2u, 0x020DB462u, 0x020DB482u, 0x020DB502u, 0x020D42AEu) \
    X(ADDR, baseHavingWeapons,        BaseHavingWeapons,         0x020DCAA6u, 0x020DCA66u, 0x020DABE6u, 0x020DB466u, 0x020DB486u, 0x020DB506u, 0x020D42B2u) \
    X(ADDR, baseWeaponAmmo,           BaseWeaponAmmo,            0x020DC720u, 0x020DC6E0u, 0x020DA860u, 0x020DB0E0u, 0x020DB100u, 0x020DB180u, 0x020D3F2Cu) \
    X(ADDR, weaponDataCurrent,        WeaponDataCurrent,         0x020DCE2Cu, 0x020DCDECu, 0x020DAF6Cu, 0x020DB7ECu, 0x020DB80Cu, 0x020DB88Cu, 0x020D4638u) \
    X(ADDR, baseAimX,                 BaseAimX,                  0x020E03E6u, 0x020E03A6u, 0x020DE526u, 0x020DEDA6u, 0x020DEDC6u, 0x020DEE46u, 0x020D7C0Eu) \
    X(ADDR, baseAimY,                 BaseAimY,                  0x020E03EEu, 0x020E03AEu, 0x020DE52Eu, 0x020DEDAEu, 0x020DEDCEu, 0x020DEE4Eu, 0x020D7C16u) \
    X(ADDR, baseChosenHunter,         BaseChosenHunter,          0x020CD358u, 0x020CD318u, 0x020CB51Cu, 0x020CBDA4u, 0x020CBDC4u, 0x020CBE44u, 0x020C4B88u) \
    X(ADDR, currentMode,              CurrentMode,               0x020E6B30u, 0x020E6AF0u, 0x020E4A04u, 0x020E54CCu, 0x020E54ECu, 0x020E556Cu, 0x020DE31Au) \
    X(ADDR, battleFlowState,          BattleFlowState,           0x020E6B48u, 0x020E6B08u, 0x020E4A1Cu, 0x020E54E4u, 0x020E5504u, 0x020E5584u, 0x020DE330u) \
    X(ADDR, inGame,                   InGame,                    0x020F0BB0u, 0x020F0B70u, 0x020EEA70u, 0x020EF530u, 0x020EF550u, 0x020EF5D0u, 0x020E81B4u) \
    X(ADDR, isInAdventure,            IsInAdventure,             0x020E9A3Cu, 0x020E99FCu, 0x020E78FCu, 0x020E83BCu, 0x020E83DCu, 0x020E845Cu, 0x020E11F8u) \
    X(ADDR, isMapOrUserActionPaused,  IsMapOrUserActionPaused,   0x020FD598u, 0x020FD558u, 0x020FB458u, 0x020FBF18u, 0x020FBF38u, 0x020FBFB8u, 0x020F4CF8u) \
    X(ADDR, operationAndSound,        OperationAndSound,         0x020E9998u, 0x020E9958u, 0x020E7858u, 0x020E8318u, 0x020E8338u, 0x020E83B8u, 0x020E1154u) \
    X(ADDR, unlockMapsHunters,        UnlockMapsHunters,         0x020E9999u, 0x020E9959u, 0x020E7859u, 0x020E8319u, 0x020E8339u, 0x020E83B9u, 0x020E1155u) \
    X(ADDR, volSfx8Bit,               VolSfx8Bit,                0x020E9999u, 0x020E9959u, 0x020E7859u, 0x020E8319u, 0x020E8339u, 0x020E83B9u, 0x020E1155u) \
    X(ADDR, volMusic8Bit,             VolMusic8Bit,              0x020E999Au, 0x020E995Au, 0x020E785Au, 0x020E831Au, 0x020E833Au, 0x020E83BAu, 0x020E1156u) \
    X(ADDR, unlockMapsHunters2,       UnlockMapsHunters2,        0x020E999Cu, 0x020E995Cu, 0x020E785Cu, 0x020E831Cu, 0x020E833Cu, 0x020E83BCu, 0x020E1158u) \
    X(ADDR, unlockMapsHunters3,       UnlockMapsHunters3,        0x020E99A0u, 0x020E9960u, 0x020E7860u, 0x020E8320u, 0x020E8340u, 0x020E83C0u, 0x020E115Cu) \
    X(ADDR, unlockMapsHunters4,       UnlockMapsHunters4,        0x020E99A4u, 0x020E9964u, 0x020E7864u, 0x020E8324u, 0x020E8344u, 0x020E83C4u, 0x020E1160u) \
    X(ADDR, unlockMapsHunters5,       UnlockMapsHunters5,        0x020E99A8u, 0x020E9968u, 0x020E7868u, 0x020E8328u, 0x020E8348u, 0x020E83C8u, 0x020E1164u) \
    X(ADDR, dsNameFlagAndMicVolume,   DsNameFlagAndMicVolume,    0x020E99A9u, 0x020E9969u, 0x020E7869u, 0x020E8329u, 0x020E8349u, 0x020E83C9u, 0x020E1165u) \
    X(ADDR, sensitivity,              Sensitivity,               0x020E99ACu, 0x020E996Cu, 0x020E786Cu, 0x020E832Cu, 0x020E834Cu, 0x020E83CCu, 0x020E1168u) \
    X(ADDR, baseInGameSensi,          BaseInGameSensi,           0x020E9A60u, 0x020E9A20u, 0x020E7920u, 0x020E83E0u, 0x020E8400u, 0x020E8480u, 0x020E121Cu) \
    X(ADDR, mainHunter,               MainHunter,                0x020ECF40u, 0x020ECF00u, 0x020EAE00u, 0x020EB8C0u, 0x020EB8E0u, 0x020EB960u, 0x020E44BCu) \
    X(ADDR, rankColor,                RankColor,                 0x020ECF43u, 0x020ECF03u, 0x020EAE03u, 0x020EB8C3u, 0x020EB8E3u, 0x020EB963u, 0x020E44BFu)

    // Custom HUD addresses (expanded under #ifdef MELONPRIME_CUSTOM_HUD)
#define MP_ROM_FIELDS_HUD(X) \
    X(ADDR, hudToggle,                HudToggle,                 0x020DB090u, 0x020DB050u, 0x020D91D0u, 0x020D9A50u, 0x020D9A70u, 0x020D9AF0u, 0x020D31C0u) \
    X(ADDR, currentAmmoSpecial,       CurrentAmmoSpecial,        0x020DC720u, 0x020DC6E0u, 0x020DA860u, 0x020DB0E0u, 0x020DB100u, 0x020DB180u, 0x020D3F2Cu) /* player struct relative (+0xF30) */ \
    X(ADDR, currentAmmoMissile,       CurrentAmmoMissile,        0x020DC722u, 0x020DC6E2u, 0x020DA862u, 0x020DB0E2u, 0x020DB102u, 0x020DB182u, 0x020D3F2Eu) /* player struct relative (+0xF30) */ \
    X(ADDR, startPressed,             StartPressed,              0x020E0538u, 0x020E04F8u, 0x020DE634u, 0x020DEEB4u, 0x020DEED4u, 0x020DEF54u, 0x020D7D29u) \
    X(ADDR, gameOver,                 GameOver,                  0x020E6B48u, 0x020E6B08u, 0x020E4A1Cu, 0x020E54E4u, 0x020E5504u, 0x020E5584u, 0x020DE330u) \
    X(ADDR, baseViewMode,             BaseViewMode,              0x020DCAAAu, 0x020DCA6Au, 0x020DABEAu, 0x020DB46Au, 0x020DB48Au, 0x020DB50Au, 0x020D42B6u) /* player struct relative (+0xF30) */ \
    X(ADDR, crosshairPosX,            CrosshairPosX,             0x020E05C0u, 0x020E0580u, 0x020DE7A4u, 0x020DF024u, 0x020DF044u, 0x020DF0C4u, 0x020D7D7Cu) \
    X(ADDR, crosshairPosY,            CrosshairPosY,             0x020E05C2u, 0x020E0582u, 0x020DE7A6u, 0x020DF026u, 0x020DF046u, 0x020DF0C6u, 0x020D7D7Eu) \
    X(ADDR, maxHP,                    MaxHP,                     0x020DC6B0u, 0x020DC670u, 0x020DA7F0u, 0x020DB070u, 0x020DB090u, 0x020DB110u, 0x020D3EBCu) /* player struct relative (+0xF30) */ \
    X(ADDR, maxAmmoSpecial,           MaxAmmoSpecial,            0x020DC724u, 0x020DC6E4u, 0x020DA864u, 0x020DB0E4u, 0x020DB104u, 0x020DB184u, 0x020D3F30u) /* player struct relative (+0xF30) */ \
    X(ADDR, maxAmmoMissile,           MaxAmmoMissile,            0x020DC726u, 0x020DC6E6u, 0x020DA866u, 0x020DB0E6u, 0x020DB106u, 0x020DB186u, 0x020D3F32u) /* player struct relative (+0xF30) */ \
    X(ADDR, baseBomb,                 BaseBomb,                  0x020DCB10u, 0x020DCAD0u, 0x020DAC50u, 0x020DB4D0u, 0x020DB4F0u, 0x020DB570u, 0x020D431Cu) /* u32 00000X00: bits[11:8]=bombs available (alt-form only); player struct relative (+0xF30) */ \
    X(ADDR, matchRank,                MatchRank,                 0x020E9C8Cu, 0x020E9C4Cu, 0x020E7B4Cu, 0x020E860Cu, 0x020E862Cu, 0x020E86ACu, 0x020E1448u) /* u32 ZZYYXXVV: byte0=P1..byte3=P4 rank (0=1st 1=2nd 2=3rd 3=4th/absent) */ \
    X(ADDR, timeLeft,                 TimeLeft,                  0x020E6B68u, 0x020E6B28u, 0x020E4A3Cu, 0x020E5504u, 0x020E5524u, 0x020E55A4u, 0x020DE350u) /* u32 remaining time; time limit setting = battleSettings+4: 0000XXYZ XX=minutes*4 (battle) or *A (other); wifi adds +0x20 */

    // In-game aspect ratio scaling patch addresses (expanded under #ifdef MELONPRIME_DS)
#define MP_ROM_FIELDS_DS_SCALE(X) \
    X(ADDR, scalePatchAddr1,          ScalePatchAddr1,           0x0211313Cu, 0x021130FCu, 0x02110FFCu, 0x02111ABCu, 0x02111ADCu, 0x02111B5Cu, 0x02109B64u) \
    X(ADDR, scalePatchAddr2,          ScalePatchAddr2,           0x0211E7E8u, 0x0211E7A8u, 0x0211C638u, 0x0211D168u, 0x0211D114u, 0x0211D208u, 0x02114838u) \
    X(ADDR, scaleValueAddr,           ScaleValueAddr,            0x02112960u, 0x02112920u, 0x02110820u, 0x021112E0u, 0x02111300u, 0x02111380u, 0x021091A4u) /* 16-bit write target */

    // Battle mode HUD addresses (expanded under #ifdef MELONPRIME_DS)
#define MP_ROM_FIELDS_DS_BATTLE(X) \
    X(ADDR, battleMode,               BattleMode,                0x020CD344u, 0x020CD304u, 0x020CB508u, 0x020CBD90u, 0x020CBDB0u, 0x020CBE30u, 0x020C4B74u) \
    X(ADDR, battleSettings,           BattleSettings,            0x020CD3B8u, 0x020CD378u, 0x020CB57Cu, 0x020CBE04u, 0x020CBE24u, 0x020CBEA4u, 0x020C4BE8u) /* +4 = time limit settings */ \
    X(ADDR, basePoint,                BasePoint,                 0x020E9C6Cu, 0x020E9C2Cu, 0x020E7B2Cu, 0x020E85ECu, 0x020E860Cu, 0x020E868Cu, 0x020E1428u) /* -0xB0 = lives, -0x180 = time (per-player: +4 * playerIdx) */

    // =========================================================================
    //  OSD Color Patch — literal pool addresses (expanded under #ifdef MELONPRIME_DS)
    //  Each OsdLiteral_* address holds a 32-bit value 0x0000CCCC (BGR555 color).
    //
    //  H211 "node stolen" shim: 4 ARM instructions that redirect H211's hardcoded
    //  immediate color load to the same literal pool entry as the return/base/complete
    //  group, so H211 uses the patched color without a separate literal.
    //  instr1 (E3A0301F = mov r3,#0x1F alpha) and instr2 (E98D000C = stmib sp,{r2,r3}) are constant.
    //
    //  H211 Separate Color: 10-instruction shim that encodes the color directly in
    //  mov/orr immediates, allowing H211 to use a color independent from other OSD msgs.
    //  instr0 = E3A020LL (mov r2,#ll) and instr1 = E3822CHH (orr r2,r2,#hh<<8) are built at runtime.
    //  instr2-3 (alpha/store) and instr6-9 (timer/flags/ptr) are constant across versions.
    //
    //  Original (pre-patch) values for revert live in the OsdH211*Orig* DATA rows
    //  below and in the scalar k* constants next to the generated tables.
    // =========================================================================
#define MP_ROM_LISTS_DS_OSD(X) \
    X(ADDR, osdLiteral_LostLives,     OsdLiteral_LostLives,      0x0200FED4u, 0x0200FED4u, 0x0200FF18u, 0x0200FEA4u, 0x0200FF10u, 0x0200FEA4u, 0x02021844u) \
    X(ADDR, osdLiteral_KillDeath,     OsdLiteral_KillDeath,      0x02018268u, 0x02018268u, 0x02018288u, 0x0201828Cu, 0x02018280u, 0x0201828Cu, 0x0201A7F8u) \
    X(ADDR, osdLiteral_ReturnBase,    OsdLiteral_ReturnBase,     0x0202DE50u, 0x0202DE50u, 0x0202DE2Cu, 0x0202DE2Cu, 0x0202DE24u, 0x0202DE2Cu, 0x02036AC8u) \
    X(ADDR, osdLiteral_NoAmmo,        OsdLiteral_NoAmmo,         0x0202DE68u, 0x0202DE68u, 0x0202DE44u, 0x0202DE44u, 0x0202DE3Cu, 0x0202DE44u, 0x02036AD4u) \
    X(ADDR, osdLiteral_CowardDetect,  OsdLiteral_CowardDetect,   0x02030534u, 0x02030534u, 0x0203051Cu, 0x020304E8u, 0x020304E0u, 0x020304E8u, 0x02033D98u) \
    X(ADDR, osdLiteral_AcquiringNode, OsdLiteral_AcquiringNode,  0x02031D0Cu, 0x02031D0Cu, 0x02031BF8u, 0x02031BA8u, 0x02031BA0u, 0x02031BA8u, 0x0203285Cu) \
    X(ADDR, osdLiteral_Turret,        OsdLiteral_Turret,         0x02111F6Cu, 0x02111F2Cu, 0x0210FE2Cu, 0x021108ECu, 0x0211090Cu, 0x0211098Cu, 0x02108C28u) \
    X(ADDR, osdLiteral_OctoReset,     OsdLiteral_OctoReset,      0x0212F6D8u, 0x0212F698u, 0x0212D538u, 0x0212E058u, 0x0212E018u, 0x0212E0F8u, 0x021251C4u) \
    X(ADDR, osdLiteral_OctoDrop,      OsdLiteral_OctoDrop,       0x0212FA64u, 0x0212FA24u, 0x0212D8C4u, 0x0212E3E4u, 0x0212E3A4u, 0x0212E484u, 0x02124F98u) \
    X(ADDR, osdLiteral_OctoCond,      OsdLiteral_OctoCond,       0x0212FBCCu, 0x0212FB8Cu, 0x0212DA2Cu, 0x0212E54Cu, 0x0212E50Cu, 0x0212E5ECu, 0x02124D6Cu) \
    X(ADDR, osdLiteral_OctoMissing,   OsdLiteral_OctoMissing,    0x0213042Cu, 0x021303ECu, 0x0212E28Cu, 0x0212EDACu, 0x0212ED6Cu, 0x0212EE4Cu, 0x02126268u) \
    X(ADDR, osdH211ShimAddr,          OsdH211ShimAddr,           0x0202D88Cu, 0x0202D88Cu, 0x0202D8B0u, 0x0202D8B0u, 0x0202D8A8u, 0x0202D8B0u, 0x020365F8u) \
    X(DATA, osdH211ShimInstr0,        OsdH211ShimInstr0,         0xE59F25BCu, 0xE59F25BCu, 0xE59F2574u, 0xE59F2574u, 0xE59F2574u, 0xE59F2574u, 0xE59F24C8u) /* ldr r2,[pc,#offset] -> ReturnBase literal */ \
    X(DATA, osdH211ShimInstr3,        OsdH211ShimInstr3,         0xE59F15B4u, 0xE59F15B4u, 0xE59F156Cu, 0xE59F156Cu, 0xE59F156Cu, 0xE59F156Cu, 0xE59F1488u) /* ldr r1,[pc,#offset] -> font pointer restore */ \
    X(DATA, osdH211SepInstr4,         OsdH211SepInstr4,          0xE59F15B0u, 0xE59F15B0u, 0xE59F1568u, 0xE59F1568u, 0xE59F1568u, 0xE59F1568u, 0xE59F1484u) /* ldr r1,[pc,#offset] -> font ptr holder */ \
    X(DATA, osdH211SepInstr5,         OsdH211SepInstr5,          0xE5911000u, 0xE5911000u, 0xE5911000u, 0xE5911000u, 0xE5911000u, 0xE5911000u, 0xE59110A8u) /* ldr r1,[r1,...] (KR offset differs) */ \
    X(DATA, osdH211OrigShimInstr2,    OsdH211OrigShimInstr2,     0xE59F15B8u, 0xE59F15B8u, 0xE59F1570u, 0xE59F1570u, 0xE59F1570u, 0xE59F1570u, 0xE59F148Cu) /* original instr at shim +0x08 (varies by ROM) */ \
    X(DATA, osdH211SepOrigInstr4,     OsdH211SepOrigInstr4,      0xE5912000u, 0xE5912000u, 0xE5912000u, 0xE5912000u, 0xE5912000u, 0xE5912000u, 0xE59120A8u) /* original instr at sep +0x10 (KR ldr offset differs) */

    // =========================================================================
    //  Aim Smoothing Patch target addresses & original instructions
    // =========================================================================
#define MP_ROM_FIELDS_AIM(X) \
    X(ADDR, aimPatchAddrX,            AimPatchAddrX,             0x02029FE0u, 0x02029FE0u, 0x0202A004u, 0x0202A004u, 0x02029FFCu, 0x0202A004u, 0x02028020u) \
    X(DATA, aimPatchOrigX1,           AimPatchOrigX1,            0xE1D703FEu, 0xE1D703FEu, 0xE1D703FEu, 0xE1D703FEu, 0xE1D703FEu, 0xE1D703FEu, 0xE1D613F8u) \
    X(DATA, aimPatchOrigX2,           AimPatchOrigX2,            0xE1D783FCu, 0xE1D783FCu, 0xE1D783FCu, 0xE1D783FCu, 0xE1D783FCu, 0xE1D783FCu, 0xE1D603FAu) \
    X(DATA, aimPatchX1,               AimPatchX1,                0xE1D703FCu, 0xE1D703FCu, 0xE1D703FCu, 0xE1D703FCu, 0xE1D703FCu, 0xE1D703FCu, 0xE1D603FCu) \
    X(ADDR, aimPatchAddrY,            AimPatchAddrY,             0x0202A008u, 0x0202A008u, 0x0202A02Cu, 0x0202A02Cu, 0x0202A024u, 0x0202A02Cu, 0x02028048u) \
    X(DATA, aimPatchOrigY1,           AimPatchOrigY1,            0xE1D704F6u, 0xE1D704F6u, 0xE1D704F6u, 0xE1D704F6u, 0xE1D704F6u, 0xE1D704F6u, 0xE1D614F0u) \
    X(DATA, aimPatchOrigY2,           AimPatchOrigY2,            0xE1D784F4u, 0xE1D784F4u, 0xE1D784F4u, 0xE1D784F4u, 0xE1D784F4u, 0xE1D784F4u, 0xE1D604F2u) \
    X(DATA, aimPatchY1,               AimPatchY1,                0xE1D704F4u, 0xE1D704F4u, 0xE1D704F4u, 0xE1D704F4u, 0xE1D704F4u, 0xE1D704F4u, 0xE1D604F4u)

    // =========================================================================
    //  Generated LIST_* tables (same names/types as the former hand-written ones)
    // =========================================================================
#define MP_ROM_EMIT_LIST(kind, field, Name, jp10, jp11, us10, us11, eu10, eu11, kr10) \
    inline constexpr RomTable<uint32_t> LIST_##Name = { jp10, jp11, us10, us11, eu10, eu11, kr10 };

    MP_ROM_FIELDS_CORE(MP_ROM_EMIT_LIST)
#ifdef MELONPRIME_CUSTOM_HUD
    MP_ROM_FIELDS_HUD(MP_ROM_EMIT_LIST)
#endif
#ifdef MELONPRIME_DS
    MP_ROM_FIELDS_DS_SCALE(MP_ROM_EMIT_LIST)
#endif
#ifdef MELONPRIME_DS
    MP_ROM_FIELDS_DS_BATTLE(MP_ROM_EMIT_LIST)
#endif
#ifdef MELONPRIME_DS
    MP_ROM_LISTS_DS_OSD(MP_ROM_EMIT_LIST)

    // ── OSD color patch revert: original ARM values from ROM dump ──
    // Default OSD colors before any patch
    static constexpr uint32_t kOsdOrigLit        = 0x00003FEFu; // default bright green (all except no-ammo)
    static constexpr uint32_t kOsdOrigLitNoAmmo   = 0x0000295Fu; // default no-ammo red (H009 only)
    // H211 shim revert: original 4 instructions at the shim site
    // instr0 (+0x00) / instr1 (+0x04) / instr3 (+0x0C) are identical for all ROMs
    // (instr2 at +0x08 varies by ROM: see the OsdH211OrigShimInstr2 DATA row)
    static constexpr uint32_t kH211OrigInstr0     = 0xE3A0201Fu; // mov r2,#0x1F
    static constexpr uint32_t kH211OrigInstr1     = 0xE58D2004u; // str r2,[sp,#4]
    static constexpr uint32_t kH211OrigInstr3     = 0xE58D2008u; // str r2,[sp,#8]
    // H211 separate-color revert: instr at +0x10 varies by ROM (OsdH211SepOrigInstr4 row);
    // instructions +0x14..+0x24 are constant:
    static constexpr uint32_t kH211SepOrigInstr5  = 0xE3A0105Au; // +0x14
    static constexpr uint32_t kH211SepOrigInstr6  = 0xE58D200Cu; // +0x18
    static constexpr uint32_t kH211SepOrigInstr7  = 0xE58D1010u; // +0x1C
    static constexpr uint32_t kH211SepOrigInstr8  = 0xE3A01011u; // +0x20
    static constexpr uint32_t kH211SepOrigInstr9  = 0xE58D1014u; // +0x24
#endif
    MP_ROM_FIELDS_AIM(MP_ROM_EMIT_LIST)

#undef MP_ROM_EMIT_LIST

    // =========================================================================
    //  Compile-time validation: every ADDR row must lie in main RAM
    //  (DATA rows are instruction encodings and are deliberately not checked.)
    // =========================================================================
    namespace RomAddrDetail {
        constexpr bool InMainRam(uint32_t v) noexcept {
            return v >= 0x02000000u && v <= 0x023FFFFFu;
        }
        constexpr bool RangeOk_ADDR(uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                                    uint32_t e, uint32_t f, uint32_t g) noexcept {
            return InMainRam(a) && InMainRam(b) && InMainRam(c) && InMainRam(d)
                && InMainRam(e) && InMainRam(f) && InMainRam(g);
        }
        constexpr bool RangeOk_DATA(uint32_t, uint32_t, uint32_t, uint32_t,
                                    uint32_t, uint32_t, uint32_t) noexcept {
            return true;
        }
    } // namespace RomAddrDetail

#define MP_ROM_CHECK_RANGE(kind, field, Name, jp10, jp11, us10, us11, eu10, eu11, kr10) \
    static_assert(RomAddrDetail::RangeOk_##kind(jp10, jp11, us10, us11, eu10, eu11, kr10), \
                  "LIST_" #Name ": address outside main RAM 0x02000000-0x023FFFFF");

    MP_ROM_FIELDS_CORE(MP_ROM_CHECK_RANGE)
    MP_ROM_FIELDS_HUD(MP_ROM_CHECK_RANGE)
    MP_ROM_FIELDS_DS_SCALE(MP_ROM_CHECK_RANGE)
    MP_ROM_FIELDS_DS_BATTLE(MP_ROM_CHECK_RANGE)
    MP_ROM_LISTS_DS_OSD(MP_ROM_CHECK_RANGE)
    MP_ROM_FIELDS_AIM(MP_ROM_CHECK_RANGE)

#undef MP_ROM_CHECK_RANGE

    // =========================================================================
    //  Struct + constexpr builder (generated from the same rows)
    // =========================================================================
#define MP_ROM_EMIT_FIELD(kind, field, Name, jp10, jp11, us10, us11, eu10, eu11, kr10) \
    uint32_t field;

    struct RomAddresses {
        uint8_t  romGroupIndex;  // index into RomGroup enum (for patch tables etc.)
        MP_ROM_FIELDS_CORE(MP_ROM_EMIT_FIELD)
#ifdef MELONPRIME_CUSTOM_HUD
        MP_ROM_FIELDS_HUD(MP_ROM_EMIT_FIELD)
#endif
#ifdef MELONPRIME_DS
        MP_ROM_FIELDS_DS_SCALE(MP_ROM_EMIT_FIELD)
#endif
#ifdef MELONPRIME_DS
        MP_ROM_FIELDS_DS_BATTLE(MP_ROM_EMIT_FIELD)
#endif
        MP_ROM_FIELDS_AIM(MP_ROM_EMIT_FIELD)
    };

#undef MP_ROM_EMIT_FIELD

#define MP_ROM_EMIT_INIT(kind, field, Name, jp10, jp11, us10, us11, eu10, eu11, kr10) \
    LIST_##Name[i],

    constexpr RomAddresses CreateRomAddress(size_t i) {
        return {
            static_cast<uint8_t>(i),
            MP_ROM_FIELDS_CORE(MP_ROM_EMIT_INIT)
#ifdef MELONPRIME_CUSTOM_HUD
            MP_ROM_FIELDS_HUD(MP_ROM_EMIT_INIT)
#endif
#ifdef MELONPRIME_DS
            MP_ROM_FIELDS_DS_SCALE(MP_ROM_EMIT_INIT)
#endif
#ifdef MELONPRIME_DS
            MP_ROM_FIELDS_DS_BATTLE(MP_ROM_EMIT_INIT)
#endif
            MP_ROM_FIELDS_AIM(MP_ROM_EMIT_INIT)
        };
    }

#undef MP_ROM_EMIT_INIT

    inline constexpr RomTable<RomAddresses> ROM_INSTANCES = {
        CreateRomAddress(0), CreateRomAddress(1), CreateRomAddress(2),
        CreateRomAddress(3), CreateRomAddress(4), CreateRomAddress(5),
        CreateRomAddress(6)
    };

    // Permanent sanity check: each instance's romGroupIndex equals its table index.
    namespace RomAddrDetail {
        constexpr bool RomGroupIndexOk() noexcept {
            for (size_t i = 0; i < ROM_INSTANCES.size(); ++i)
                if (ROM_INSTANCES[i].romGroupIndex != static_cast<uint8_t>(i)) return false;
            return true;
        }
    } // namespace RomAddrDetail
    static_assert(RomAddrDetail::RomGroupIndexOk(),
                  "ROM_INSTANCES[i].romGroupIndex must equal i");

    [[nodiscard]] static inline const RomAddresses* getRomAddrsPtr(RomGroup group) noexcept {
        const size_t idx = static_cast<size_t>(group);
        assert(idx < ROM_INSTANCES.size());
        return &ROM_INSTANCES[idx];
    }


    // The X-macro row lists are private to this header.
#undef MP_ROM_FIELDS_CORE
#undef MP_ROM_FIELDS_HUD
#undef MP_ROM_FIELDS_DS_SCALE
#undef MP_ROM_FIELDS_DS_BATTLE
#undef MP_ROM_LISTS_DS_OSD
#undef MP_ROM_FIELDS_AIM

} // namespace MelonPrime

#endif // MELON_PRIME_ROM_ADDR_TABLE_H
