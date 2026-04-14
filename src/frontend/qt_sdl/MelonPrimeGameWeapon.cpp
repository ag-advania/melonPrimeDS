#include "MelonPrimeInternal.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "main.h"
#include "Screen.h"
#include "MelonPrimeDef.h"

#include <array>
#include <string_view>
#include <utility>

namespace MelonPrime {

    static_assert(IB_WEAPON_MISSILE == (IB_WEAPON_BEAM << 1), "Weapon input bits must stay contiguous");
    static_assert(IB_WEAPON_1 == (IB_WEAPON_MISSILE << 1), "Weapon input bits must stay contiguous");
    static_assert(IB_WEAPON_2 == (IB_WEAPON_1 << 1), "Weapon input bits must stay contiguous");
    static_assert(IB_WEAPON_3 == (IB_WEAPON_2 << 1), "Weapon input bits must stay contiguous");
    static_assert(IB_WEAPON_4 == (IB_WEAPON_3 << 1), "Weapon input bits must stay contiguous");
    static_assert(IB_WEAPON_5 == (IB_WEAPON_4 << 1), "Weapon input bits must stay contiguous");
    static_assert(IB_WEAPON_6 == (IB_WEAPON_5 << 1), "Weapon input bits must stay contiguous");
    static_assert(IB_WEAPON_SPECIAL == (IB_WEAPON_6 << 1), "Weapon input bits must stay contiguous");

    namespace WeaponData {

        enum ID : uint8_t {
            POWER_BEAM   = 0,
            VOLT_DRIVER  = 1,
            MISSILE      = 2,
            BATTLEHAMMER = 3,
            IMPERIALIST  = 4,
            JUDICATOR    = 5,
            MAGMAUL      = 6,
            SHOCK_COIL   = 7,
            OMEGA_CANNON = 8,
            NONE         = 0xFF
        };

        // Display names indexed by game-internal weapon ID (0-8).
        // NOTE: Game-internal IDs differ from the enum label names.
        //       e.g. internal ID 3 = Battlehammer, not Magmaul.
        static constexpr std::array<std::string_view, 9> kNames = {
            "Power Beam", "Volt Driver", "Missile Launcher", "Battlehammer",
            "Imperialist", "Judicator", "Magmaul", "Shock Coil", "Omega Cannon"
        };

        struct Info {
            uint8_t  id;
            uint16_t mask;
            uint8_t  minAmmo;
        };

        // Weapon cycle order: Beam -> Missile -> ShockCoil -> Magmaul -> Judicator
        //                  -> Imperialist -> Battlehammer -> Volt Driver -> Omega
        //
        // NOTE:
        //   minAmmo must follow the old MIN_AMMO[weaponID] mapping from the legacy
        //   implementation, not the ordered-slot index. The previous refactor kept
        //   the masks/order correct but mismatched several minAmmo values, which
        //   changed the available-weapon set used by wheel / next / prev switching.
        constexpr std::array<Info, 9> ORDERED_WEAPONS = { {
            { POWER_BEAM,    0x001, 0x00 }, // MIN_AMMO[0] = 0x00
            { MISSILE,       0x004, 0x0A }, // MIN_AMMO[2] = 0x0A
            { SHOCK_COIL,    0x080, 0x0A }, // MIN_AMMO[7] = 0x0A
            { MAGMAUL,       0x040, 0x0A }, // MIN_AMMO[6] = 0x0A
            { JUDICATOR,     0x020, 0x05 }, // MIN_AMMO[5] = 0x05
            { IMPERIALIST,   0x010, 0x14 }, // MIN_AMMO[4] = 0x14
            { BATTLEHAMMER,  0x008, 0x04 }, // MIN_AMMO[3] = 0x04 (Weavel: 0x05)
            { VOLT_DRIVER,   0x002, 0x05 }, // MIN_AMMO[1] = 0x05
            { OMEGA_CANNON,  0x100, 0x00 }  // MIN_AMMO[8] = 0x00
        } };

        constexpr uint8_t HOTKEY_TO_ID[] = {
            POWER_BEAM, MISSILE, SHOCK_COIL, MAGMAUL, JUDICATOR,
            IMPERIALIST, BATTLEHAMMER, VOLT_DRIVER, NONE
        };

        // Weapon ID -> position in ORDERED_WEAPONS (constexpr LUT)
        constexpr std::array<uint8_t, 9> ID_TO_ORDERED_IDX = { 0, 7, 1, 6, 5, 4, 3, 2, 8 };

        constexpr uint16_t kOmegaCannonOwnedBitMask = WeaponMask::OmegaCannon;
        constexpr uint16_t kOmegaRestrictedCycleBits =
            static_cast<uint16_t>((1u << ID_TO_ORDERED_IDX[POWER_BEAM]) |
                                  (1u << ID_TO_ORDERED_IDX[MISSILE]) |
                                  (1u << ID_TO_ORDERED_IDX[OMEGA_CANNON]));

    } // namespace WeaponData



    FORCE_INLINE bool HasOmegaCannonFlag(uint16_t ownedWeaponBits)
    {
        return (ownedWeaponBits & WeaponData::kOmegaCannonOwnedBitMask) != 0;
    }

    FORCE_INLINE bool IsWeaponAllowedWhileOmegaCannonActive(uint8_t weaponId)
    {
        using namespace WeaponData;
        return weaponId == POWER_BEAM || weaponId == MISSILE || weaponId == OMEGA_CANNON;
    }

    COLD_FUNCTION void ShowOmegaWeaponSwitchBlockedMessage(EmuInstance* emuInstance)
    {
        emuInstance->osdAddMessage(0, "You can't switch to that weapon while Omega Cannon is active!");
    }

    // =========================================================================
    // Weapon availability check -- fold-expression based
    // =========================================================================
    template <size_t I>
    FORCE_INLINE uint16_t CheckOneWeapon(
        uint16_t having, uint16_t weaponAmmo, uint16_t missileAmmo, bool isWeavel)
    {
        using namespace WeaponData;
        constexpr const Info& info = ORDERED_WEAPONS[I];

        if constexpr (info.id == POWER_BEAM) {
            return (1u << I);
        }
        else if constexpr (info.id == MISSILE) {
            return (missileAmmo >= 0xA) ? (1u << I) : 0;
        }
        else if constexpr (info.id == OMEGA_CANNON) {
            return (having & info.mask) ? (1u << I) : 0;
        }
        else {
            if (!(having & info.mask)) return 0;
            uint8_t req = info.minAmmo;
            if constexpr (info.id == BATTLEHAMMER) {
                if (isWeavel) req = 0x5;
            }
            return (weaponAmmo >= req) ? (1u << I) : 0;
        }
    }

    template <size_t... Is>
    FORCE_INLINE uint16_t CheckAllWeapons(
        uint16_t having, uint16_t weaponAmmo, uint16_t missileAmmo, bool isWeavel,
        std::index_sequence<Is...>)
    {
        return (CheckOneWeapon<Is>(having, weaponAmmo, missileAmmo, isWeavel) | ...);
    }

    // =========================================================================
    // Common weapon state -- read once, reuse in both cycle and hotkey paths
    // =========================================================================
    struct WeaponState {
        uint16_t having;
        uint16_t weaponAmmo;
        uint16_t missileAmmo;

        FORCE_INLINE WeaponState(const uint16_t* havingPtr, const uint32_t* ammoPtr)
            : having(*havingPtr)
        {
            const uint32_t ammoData = *ammoPtr;
            weaponAmmo  = static_cast<uint16_t>(ammoData & 0xFFFF);
            missileAmmo = static_cast<uint16_t>(ammoData >> 16);
        }
    };

    // =========================================================================
    // ProcessWeaponSwitch -- handles wheel/next/prev and direct hotkey selection
    //
    // OPT vs previous version:
    //   1. Weapon cycling uses O(1) bit-scan instead of O(n) loop
    //   2. Direct hotkey mask uses fold expression instead of runtime loop
    //   3. Common WeaponState struct eliminates duplicate RAM reads
    // =========================================================================
    HOT_FUNCTION bool MelonPrimeCore::ProcessWeaponSwitch()
    {
        using namespace WeaponData;

        // --- Case 1: Mouse Wheel / Next / Prev ---
        if (LIKELY(!IsAnyPressed(IB_WEAPON_ANY))) {
            // OPT-A: wheelDelta is now pre-fetched by UpdateInputState into m_input.
            //   Eliminates: emuInstance->getMainWindow()->panel->getDelta() (~18-28 cyc)
            //   The caller (HandleInGameLogic) already gates on hasWeaponInput,
            //   so this path only executes when wheel/next/prev is active.
            const int wheelDelta = m_input.wheelDelta;
            const bool nextKey = IsPressed(IB_WEAPON_NEXT);
            const bool prevKey = IsPressed(IB_WEAPON_PREV);

            if (!wheelDelta && !nextKey && !prevKey) return false;

            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

            const bool forward = (wheelDelta < 0) || nextKey;

            const WeaponState ws(m_ptrs.havingWeapons, m_ptrs.weaponAmmo);
            const bool isWeavel = m_flags.test(StateFlags::BIT_IS_WEAVEL);

            uint16_t availableBits = CheckAllWeapons(
                ws.having, ws.weaponAmmo, ws.missileAmmo, isWeavel,
                std::make_index_sequence<ORDERED_WEAPONS.size()>{});

            if (UNLIKELY(HasOmegaCannonFlag(ws.having))) {
                availableBits &= kOmegaRestrictedCycleBits;
            }

            if (!availableBits) {
                m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
                return false;
            }

            const uint8_t curID = *m_ptrs.currentWeapon;
            const uint8_t safeID = (curID >= 9) ? 0 : curID;
            const uint8_t currentIdx = ID_TO_ORDERED_IDX[safeID];

            // OPT: O(1) weapon cycling via bit-scan.
            //
            // Previous code used a loop iterating up to 8 times with modular
            // arithmetic per iteration. The new approach:
            //   Forward:  find lowest set bit ABOVE currentIdx, else wrap to lowest overall
            //   Backward: find highest set bit BELOW currentIdx, else wrap to highest overall
            //
            // This replaces up to 8 iterations x (branch + mod) with 2-3 bit ops.
            // Latency-sensitive because weapon switch triggers FrameAdvanceTwice
            // immediately after, so every us here is added to input-to-action delay.
            const uint16_t candidates = availableBits & ~(1u << currentIdx);
            if (!candidates) {
                m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
                return false;
            }

            size_t targetIdx;
            if (forward) {
                // Mask bits strictly above currentIdx
                const uint16_t upper = candidates & ~((1u << (currentIdx + 1)) - 1);
                targetIdx = upper ? BitScanFwd(upper) : BitScanFwd(candidates);
            } else {
                // Mask bits strictly below currentIdx
                const uint16_t lower = candidates & ((1u << currentIdx) - 1);
                targetIdx = lower ? BitScanRev(lower) : BitScanRev(candidates);
            }

            SwitchWeapon(ORDERED_WEAPONS[targetIdx].id);
            return true;
        }

        // --- Case 2: Direct Weapon Hotkeys ---
        if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

        // OPT-WH1: Direct contiguous extract for weapon hotkey bits.
        // IB_WEAPON_BEAM..IB_WEAPON_SPECIAL are contiguous (17..25),
        // so this is equivalent to the old BuildHotkeyMask() path with fewer ops.
        const uint32_t hot = static_cast<uint32_t>((m_input.press >> 17) & 0x1FFu);
        const int firstSet = static_cast<int>(BitScanFwd(hot));

        const WeaponState ws(m_ptrs.havingWeapons, m_ptrs.weaponAmmo);
        const bool isOmegaCannonFlagActive = HasOmegaCannonFlag(ws.having);

        // Special Weapon (Affinity) -- index 8
        if (UNLIKELY(firstSet == 8)) {
            if (isOmegaCannonFlagActive) {
                SwitchWeapon(OMEGA_CANNON);
                return true;
            }

            const uint8_t loaded = *m_ptrs.loadedSpecialWeapon;
            if (loaded == 0xFF) {
                emuInstance->osdAddMessage(0, "Have not Special Weapon yet!");
                m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
                return false;
            }
            SwitchWeapon(loaded);
            return true;
        }

        const uint8_t weaponID = HOTKEY_TO_ID[firstSet];
        const uint8_t orderedIdx = ID_TO_ORDERED_IDX[weaponID];
        const Info& info = ORDERED_WEAPONS[orderedIdx];

        if (UNLIKELY(isOmegaCannonFlagActive && !IsWeaponAllowedWhileOmegaCannonActive(weaponID))) {
            ShowOmegaWeaponSwitchBlockedMessage(emuInstance);
            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        // Ownership check
        const bool owned = (weaponID == POWER_BEAM || weaponID == MISSILE)
                         || ((ws.having & info.mask) != 0);

        if (!owned) {
            emuInstance->osdAddMessage(0, "Have not %s yet!", kNames[weaponID].data());
            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        // Ammo check
        bool hasAmmo = true;
        if (weaponID == MISSILE) {
            hasAmmo = (ws.missileAmmo >= 0xA);
        } else if (weaponID != POWER_BEAM && weaponID != OMEGA_CANNON) {
            uint8_t required = info.minAmmo;
            if (weaponID == BATTLEHAMMER && m_flags.test(StateFlags::BIT_IS_WEAVEL)) required = 0x5;
            hasAmmo = (ws.weaponAmmo >= required);
        }

        if (!hasAmmo) {
            emuInstance->osdAddMessage(0, "Not enough Ammo for %s!", kNames[weaponID].data());
            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        SwitchWeapon(weaponID);
        return true;
    }

    // =========================================================================
    // SwitchWeapon -- executes the weapon change by manipulating game RAM
    //
    // R2: Cache NDS pointer once at the top for consistency with
    //     HandleInGameLogic pattern. Eliminates 1 pointer chase.
    // =========================================================================
    void MelonPrimeCore::SwitchWeapon(int weaponIndex)
    {
        const uint8_t targetWeaponId = static_cast<uint8_t>(weaponIndex);

        if (UNLIKELY(HasOmegaCannonFlag(*m_ptrs.havingWeapons)
            && !IsWeaponAllowedWhileOmegaCannonActive(targetWeaponId))) {
            ShowOmegaWeaponSwitchBlockedMessage(emuInstance);
            return;
        }

        if ((*m_ptrs.selectedWeapon) == weaponIndex) return;

        if (m_flags.test(StateFlags::BIT_IN_ADVENTURE)) {
            if (m_flags.test(StateFlags::BIT_PAUSED)) {
                emuInstance->osdAddMessage(0, "You can't switch weapon now!");
                return;
            }
            if ((*m_ptrs.isInVisorOrMap) == 0x1) return;
        }

        const uint8_t currentJumpFlags = *m_ptrs.jumpFlag;
        const bool isTransforming = (currentJumpFlags & 0x10) != 0;
        const uint8_t jumpFlag    = currentJumpFlags & 0x0F;
        bool isRestoreNeeded = false;

        const bool isAltForm = (*m_ptrs.isAltForm) == 0x02;
        m_flags.assign(StateFlags::BIT_IS_ALT_FORM, isAltForm);

        if (!isTransforming && jumpFlag == 0 && !isAltForm) {
            *m_ptrs.jumpFlag = (currentJumpFlags & 0xF0) | 0x01;
            isRestoreNeeded = true;
        }

        *m_ptrs.weaponChange   = ((*m_ptrs.weaponChange) & 0xF0) | 0x0B;
        *m_ptrs.selectedWeapon = static_cast<uint8_t>(weaponIndex);

        // R2: Cache NDS pointer once (was called twice via emuInstance->getNDS())
        auto* const nds = emuInstance->getNDS();
        nds->ReleaseScreen();
        FrameAdvanceTwice();

        if (!isStylusMode) {
            using namespace Consts::UI;
            nds->TouchScreen(CENTER_RESET.x(), CENTER_RESET.y());
        } else if (emuInstance->isTouching) {
            nds->TouchScreen(emuInstance->touchX, emuInstance->touchY);
        }
        FrameAdvanceTwice();

        if (isRestoreNeeded) {
            const uint8_t current = *m_ptrs.jumpFlag;
            *m_ptrs.jumpFlag = (current & 0xF0) | jumpFlag;
        }
    }

} // namespace MelonPrime
