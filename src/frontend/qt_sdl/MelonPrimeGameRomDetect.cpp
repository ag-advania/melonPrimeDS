#include "MelonPrimeInternal.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeGameRomAddrTable.h"
#ifdef MELONPRIME_DS
#include "MelonPrimePatchLifecycle.h"
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
        // Checksum literals are inlined here (single source of truth; the old
        // RomVersions:: constant block in MelonPrimeDef.h was folded in).
        constexpr ChecksumEntry CHECKSUM_TABLE[] = {
            { 0x91B46577u, RomGroup::US1_1, "US1.1" },
            { 0x01476E8Fu, RomGroup::US1_1, "US1.1 ENCRYPTED" },
            { 0x218DA42Cu, RomGroup::US1_0, "US1.0" },
            { 0xE048CD92u, RomGroup::US1_0, "US1.0 ENCRYPTED" },
            { 0x910018A5u, RomGroup::EU1_1, "EU1.1" },
            { 0x31703770u, RomGroup::EU1_1, "EU1.1 ENCRYPTED" },
            { 0x948B1E48u, RomGroup::EU1_1, "EU1.1 BALANCED" },
            { 0x2970A14Fu, RomGroup::EU1_1, "EU1.1 BALANCED V1.2.11" },
            { 0x9E20F3A8u, RomGroup::EU1_1, "EU1.1 RUSSIANED" },
            { 0xA4A8FE5Au, RomGroup::EU1_0, "EU1.0" },
            { 0x979BB267u, RomGroup::EU1_0, "EU1.0 ENCRYPTED" },
            { 0xD75F539Du, RomGroup::JP1_0, "JP1.0" },
            { 0xE795A10Cu, RomGroup::JP1_0, "JP1.0 ENCRYPTED" },
            { 0x42EBF348u, RomGroup::JP1_1, "JP1.1" },
            { 0x0A1203A5u, RomGroup::JP1_1, "JP1.1 ENCRYPTED" },
            { 0xE54682F3u, RomGroup::KR1_0, "KR1.0" },
            { 0xC26916F3u, RomGroup::KR1_0, "KR1.0 ENCRYPTED" },
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
        m_zoomAimCanZoomCache = {};
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
        hot.currentMode             = rom.currentMode;
        hot.battleFlowState         = rom.battleFlowState;
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

        // OPT-L: Resolve inGame + mode/flow pointers immediately — read every frame
        //   before the per-game-join init block runs.
        {
            melonDS::u8* ram = emuInstance->getNDS()->MainRAM;
            m_ptrs.inGame           = GetRamPointer<uint16_t>(ram, m_addrHot.inGame);
            m_ptrs.currentMode      = GetRamPointer<uint8_t>(ram, m_addrHot.currentMode);
            m_ptrs.battleFlowState  = GetRamPointer<uint8_t>(ram, m_addrHot.battleFlowState);
        }

#ifdef MELONPRIME_DS
        PatchLifecycle::DeactivateHooksForRomDetect(
            emuInstance->getNDS(), emuInstance, localCfg, m_currentRom, this);
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

        ReloadAimConfigFromTable(localCfg);
    }

} // namespace MelonPrime
