#include "MelonPrimeInternal.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeGameRomAddrTable.h"
#ifdef MELONPRIME_DS
#include "MelonPrimeArm9Hook.h"
#endif

#include <array>

namespace MelonPrime {

    namespace {
        // -------------------------------------------------------------------------
        //  Detection is hybrid:
        //
        //  1. PRIMARY — full checksum (header + ARM9 + ARM7 CRC32, computed once at
        //     ROM load). When it matches a known entry the RomGroup is authoritative,
        //     so every shipped revision/variant (e.g. EU1.0 vs EU1.1, whose in-RAM
        //     layouts differ by 0x80) selects the correct address table regardless of
        //     what the header revision byte happens to contain.
        //
        //  2. FALLBACK — NDS header gameCode (@0x0C) + revision (@0x1E), used only
        //     when the checksum is unrecognized (trimmed / modified / brand-new dump).
        //     gameCode->region is authoritative (confirmed against MphRead's version
        //     table: AMHE=USA, AMHP=EUR, AMHJ=JPN, AMHK=KOR). The revision byte is the
        //     No-Intro rev convention and is only directly verified for a subset of
        //     dumps, which is exactly why it is the fallback rather than the primary.
        // -------------------------------------------------------------------------

        // PRIMARY table: checksum -> authoritative RomGroup + exact OSD label.
        struct ChecksumEntry { uint32_t checksum; RomGroup group; const char* name; };
        constexpr ChecksumEntry CHECKSUM_TABLE[] = {
            { RomVersions::US1_1,                  RomGroup::US1_1, "US1.1" },
            { RomVersions::US1_1_ENCRYPTED,        RomGroup::US1_1, "US1.1 ENCRYPTED" },
            { RomVersions::US1_0,                  RomGroup::US1_0, "US1.0" },
            { RomVersions::US1_0_ENCRYPTED,        RomGroup::US1_0, "US1.0 ENCRYPTED" },
            { RomVersions::EU1_1,                  RomGroup::EU1_1, "EU1.1" },
            { RomVersions::EU1_1_ENCRYPTED,        RomGroup::EU1_1, "EU1.1 ENCRYPTED" },
            { RomVersions::EU1_1_BALANCED,         RomGroup::EU1_1, "EU1.1 BALANCED" },
            { RomVersions::EU1_1_BALANCED_V1_2_11, RomGroup::EU1_1, "EU1.1 BALANCED V1.2.11" },
            { RomVersions::EU1_1_RUSSIANED,        RomGroup::EU1_1, "EU1.1 RUSSIANED" },
            { RomVersions::EU1_0,                  RomGroup::EU1_0, "EU1.0" },
            { RomVersions::EU1_0_ENCRYPTED,        RomGroup::EU1_0, "EU1.0 ENCRYPTED" },
            { RomVersions::JP1_0,                  RomGroup::JP1_0, "JP1.0" },
            { RomVersions::JP1_0_ENCRYPTED,        RomGroup::JP1_0, "JP1.0 ENCRYPTED" },
            { RomVersions::JP1_1,                  RomGroup::JP1_1, "JP1.1" },
            { RomVersions::JP1_1_ENCRYPTED,        RomGroup::JP1_1, "JP1.1 ENCRYPTED" },
            { RomVersions::KR1_0,                  RomGroup::KR1_0, "KR1.0" },
            { RomVersions::KR1_0_ENCRYPTED,        RomGroup::KR1_0, "KR1.0 ENCRYPTED" },
        };

        // FALLBACK: header gameCode + revision -> RomGroup.
        struct HeaderMatch { bool matched; RomGroup group; const char* baseName; };

        HeaderMatch MapHeaderToRomGroup(uint32_t gameCode, uint8_t romVersion) {
            const bool rev1 = (romVersion != 0);
            switch (gameCode) {
            case MphGameCode::US: return { true, rev1 ? RomGroup::US1_1 : RomGroup::US1_0, rev1 ? "US1.1" : "US1.0" };
            case MphGameCode::EU: return { true, rev1 ? RomGroup::EU1_1 : RomGroup::EU1_0, rev1 ? "EU1.1" : "EU1.0" };
            case MphGameCode::JP: return { true, rev1 ? RomGroup::JP1_1 : RomGroup::JP1_0, rev1 ? "JP1.1" : "JP1.0" };
            case MphGameCode::KR: return { true, RomGroup::KR1_0, "KR1.0" };
            default:              return { false, RomGroup::JP1_0, "Unknown" };
            }
        }
    } // namespace

    COLD_FUNCTION void MelonPrimeCore::DetectRomAndSetAddresses()
    {
        RomGroup    group;
        const char* osdName;
        bool        isVariant = false;  // true => matched by header fallback, not checksum

        // --- 1. Primary: authoritative checksum match ---
        const ChecksumEntry* hit = nullptr;
        for (const auto& e : CHECKSUM_TABLE) {
            if (globalChecksum == e.checksum) { hit = &e; break; }
        }

        if (hit) {
            group   = hit->group;
            osdName = hit->name;
        } else {
            // --- 2. Fallback: NDS header gameCode + revision ---
            const HeaderMatch hm = MapHeaderToRomGroup(globalGameCode, globalRomVersion);
            if (!hm.matched) return;  // not an MPH ROM
            group     = hm.group;
            osdName   = hm.baseName;
            isVariant = true;
        }

        // Copy the full address set for this ROM variant
        m_currentRom = *getRomAddrsPtr(group);

        // --- Initialize hot addresses from base values ---
        auto& hot = m_addrHot;
        const auto& rom = m_currentRom;

        hot.inGame                  = rom.inGame;
        hot.isMapOrUserActionPaused = rom.isMapOrUserActionPaused;

        // Player-relative (base values, recalculated on player position change)
        hot.isAltForm           = rom.baseIsAltForm;
        hot.jumpFlag            = rom.baseJumpFlag;
        hot.weaponChange        = rom.baseWeaponChange;
        hot.selectedWeapon      = rom.baseSelectedWeapon;
        hot.aimX                = rom.baseAimX;
        hot.aimY                = rom.baseAimY;
        hot.loadedSpecialWeapon = rom.baseLoadedSpecialWeapon;
        hot.boostGauge          = rom.boostGauge;
        hot.isBoosting          = rom.isBoosting;
        hot.isInVisorOrMap      = rom.isInVisorOrMap;
        hot.chosenHunter        = rom.baseChosenHunter;
        hot.inGameSensi         = rom.baseInGameSensi;
        hot.currentWeapon       = rom.baseCurrentWeapon;
        hot.havingWeapons       = rom.baseHavingWeapons;
        hot.weaponAmmo          = rom.baseWeaponAmmo;

        m_flags.set(StateFlags::BIT_ROM_DETECTED);

        // OPT-L: Resolve inGame pointer immediately — it's read every frame
        //   before the in-game init block runs, so it must be available as soon
        //   as BIT_ROM_DETECTED is set. Other m_ptrs.* are resolved later in
        //   the per-game-join init block (player-position dependent).
        {
            melonDS::u8* ram = emuInstance->getNDS()->MainRAM;
            m_ptrs.inGame = GetRamPointer<uint16_t>(ram, m_addrHot.inGame);
        }

#ifdef MELONPRIME_DS
        ARM9Hook_Install(
            emuInstance->getNDS(),
            localCfg,
            m_currentRom.romGroupIndex,
            this);
#endif

        char message[256];
        if (isVariant) {
            // Known region/revision but an unrecognized binary (mod / trim / new dump).
            snprintf(message, sizeof(message),
                "MPH Rom Detected: %s (variant, CRC 0x%08X)", osdName, globalChecksum);
        } else {
            snprintf(message, sizeof(message), "MPH Rom Detected: %s", osdName);
        }
        emuInstance->osdAddMessage(0, message);

        RecalcAimSensitivityCache(localCfg);
        ApplyAimAdjustSetting(localCfg);
    }

} // namespace MelonPrime
