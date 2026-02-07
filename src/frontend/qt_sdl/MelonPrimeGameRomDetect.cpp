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

        const auto& addr = getRomAddrs(romInfo->group);

        m_addrCold.baseChosenHunter = addr.addrBaseChosenHunter;
        m_addrHot.inGame = addr.addrInGame;
        m_addrCold.playerPos = addr.addrPlayerPos;
        m_addrCold.baseIsAltForm = addr.addrBaseIsAltForm;
        m_addrCold.baseWeaponChange = addr.addrBaseWeaponChange;
        m_addrCold.baseSelectedWeapon = addr.addrBaseSelectedWeapon;
        m_addrCold.baseAimX = addr.addrBaseAimX;
        m_addrCold.baseAimY = addr.addrBaseAimY;
        m_addrCold.isInAdventure = addr.addrIsInAdventure;
        m_addrHot.isMapOrUserActionPaused = addr.addrIsMapOrUserActionPaused;
        m_addrCold.unlockMapsHunters = addr.addrUnlockMapsHunters;
        m_addrCold.sensitivity = addr.addrSensitivity;
        m_addrCold.mainHunter = addr.addrMainHunter;
        m_addrCold.baseLoadedSpecialWeapon = addr.addrBaseLoadedSpecialWeapon;

        m_addrHot.isInVisorOrMap = m_addrCold.playerPos - Consts::VISOR_OFFSET;
        m_addrCold.baseJumpFlag = m_addrCold.baseSelectedWeapon - 0xA;
        m_addrCold.volSfx8Bit = m_addrCold.unlockMapsHunters;
        m_addrCold.volMusic8Bit = m_addrCold.volSfx8Bit + 0x1;
        m_addrCold.operationAndSound = m_addrCold.unlockMapsHunters - 0x1;
        m_addrCold.unlockMapsHunters2 = m_addrCold.unlockMapsHunters + 0x3;
        m_addrCold.unlockMapsHunters3 = m_addrCold.unlockMapsHunters + 0x7;
        m_addrCold.unlockMapsHunters4 = m_addrCold.unlockMapsHunters + 0xB;
        m_addrCold.unlockMapsHunters5 = m_addrCold.unlockMapsHunters + 0xF;
        m_addrCold.dsNameFlagAndMicVolume = m_addrCold.unlockMapsHunters5 + 0x1;
        m_addrCold.baseInGameSensi = m_addrCold.sensitivity + 0xB4;
        m_addrCold.rankColor = m_addrCold.mainHunter + 0x3;

        m_flags.set(StateFlags::BIT_ROM_DETECTED);

        char message[256];
        snprintf(message, sizeof(message), "MPH Rom Detected: %s", romInfo->name);
        emuInstance->osdAddMessage(0, message);

        RecalcAimSensitivityCache(localCfg);
        ApplyAimAdjustSetting(localCfg);
    }

} // namespace MelonPrime
