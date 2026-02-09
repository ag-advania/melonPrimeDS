#include "MelonPrimeInternal.h"
#include "EmuInstance.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeGameRomAddrTable.h"

#include <array>

namespace MelonPrime {

    COLD_FUNCTION void MelonPrimeCore::DetectRomAndSetAddresses()
    {
        struct RomInfo {
            uint32_t checksum;
            const char* name;
            RomGroup group;
        };

        static const std::array<RomInfo, 16> ROM_INFO_TABLE = { {
            {RomVersions::US1_1,           "US1.1",           RomGroup::US1_1},
            {RomVersions::US1_1_ENCRYPTED, "US1.1 ENCRYPTED", RomGroup::US1_1},
            {RomVersions::US1_0,           "US1.0",           RomGroup::US1_0},
            {RomVersions::US1_0_ENCRYPTED, "US1.0 ENCRYPTED", RomGroup::US1_0},
            {RomVersions::EU1_1,           "EU1.1",           RomGroup::EU1_1},
            {RomVersions::EU1_1_ENCRYPTED, "EU1.1 ENCRYPTED", RomGroup::EU1_1},
            {RomVersions::EU1_1_BALANCED,  "EU1.1 BALANCED",  RomGroup::EU1_1},
            {RomVersions::EU1_1_RUSSIANED, "EU1.1 RUSSIANED", RomGroup::EU1_1},
            {RomVersions::EU1_0,           "EU1.0",           RomGroup::EU1_0},
            {RomVersions::EU1_0_ENCRYPTED, "EU1.0 ENCRYPTED", RomGroup::EU1_0},
            {RomVersions::JP1_0,           "JP1.0",           RomGroup::JP1_0},
            {RomVersions::JP1_0_ENCRYPTED, "JP1.0 ENCRYPTED", RomGroup::JP1_0},
            {RomVersions::JP1_1,           "JP1.1",           RomGroup::JP1_1},
            {RomVersions::JP1_1_ENCRYPTED, "JP1.1 ENCRYPTED", RomGroup::JP1_1},
            {RomVersions::KR1_0,           "KR1.0",           RomGroup::KR1_0},
            {RomVersions::KR1_0_ENCRYPTED, "KR1.0 ENCRYPTED", RomGroup::KR1_0},
        } };

        const RomInfo* romInfo = nullptr;
        for (const auto& info : ROM_INFO_TABLE) {
            if (globalChecksum == info.checksum) {
                romInfo = &info;
                break;
            }
        }

        if (!romInfo) return;

        // ★修正: ポインタをデリファレンスして実体にコピー
        m_currentRom = *getRomAddrsPtr(romInfo->group);

        // --- Hotアドレスの初期化 ---
        // ★修正: アロー演算子(->) を ドット(.) に変更
        m_addrHot.inGame = m_currentRom.inGame;
        m_addrHot.isMapOrUserActionPaused = m_currentRom.isMapOrUserActionPaused;

        // Player系 (初期値)
        m_addrHot.isAltForm = m_currentRom.baseIsAltForm;
        m_addrHot.jumpFlag = m_currentRom.baseJumpFlag;
        m_addrHot.weaponChange = m_currentRom.baseWeaponChange;
        m_addrHot.selectedWeapon = m_currentRom.baseSelectedWeapon;
        m_addrHot.aimX = m_currentRom.baseAimX;
        m_addrHot.aimY = m_currentRom.baseAimY;
        m_addrHot.loadedSpecialWeapon = m_currentRom.baseLoadedSpecialWeapon;
        m_addrHot.boostGauge = m_currentRom.boostGauge;
        m_addrHot.isBoosting = m_currentRom.isBoosting;
        m_addrHot.isInVisorOrMap = m_currentRom.isInVisorOrMap;

        m_addrHot.chosenHunter = m_currentRom.baseChosenHunter;
        m_addrHot.inGameSensi = m_currentRom.baseInGameSensi;

        m_addrHot.currentWeapon = m_currentRom.baseCurrentWeapon;
        m_addrHot.havingWeapons = m_currentRom.baseHavingWeapons;
        m_addrHot.weaponAmmo = m_currentRom.baseWeaponAmmo;

        m_flags.set(StateFlags::BIT_ROM_DETECTED);

        char message[256];
        snprintf(message, sizeof(message), "MPH Rom Detected: %s", romInfo->name);
        emuInstance->osdAddMessage(0, message);

        RecalcAimSensitivityCache(localCfg);
        ApplyAimAdjustSetting(localCfg);
    }

} // namespace MelonPrime