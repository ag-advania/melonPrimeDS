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

    namespace WeaponData {

        enum ID : uint8_t {
            POWER_BEAM   = 0,
            VOLT_DRIVER  = 1,
            MISSILE      = 2,
            MAGMAUL      = 3,
            JUDICATOR    = 4,
            IMPERIALIST  = 5,
            BATTLEHAMMER = 6,
            SHOCK_COIL   = 7,
            OMEGA_CANNON = 8,
            NONE         = 0xFF
        };

        // Display names indexed by weapon ID (0-8)
        static constexpr std::array<std::string_view, 9> kNames = {
            "Power Beam", "Volt Driver", "Missile Launcher", "Battlehammer",
            "Imperialist", "Judicator", "Magmaul", "Shock Coil", "Omega Cannon"
        };

        struct Info {
            uint8_t  id;
            uint16_t mask;
            uint8_t  minAmmo;
        };

        // Weapon cycle order: Beam → Missile → ShockCoil → BH → Imp → Jud → Mag → Volt → Omega
        constexpr std::array<Info, 9> ORDERED_WEAPONS = { {
            { POWER_BEAM,    0x001, 0    },
            { MISSILE,       0x004, 0x5  },
            { SHOCK_COIL,    0x080, 0xA  },
            { BATTLEHAMMER,  0x040, 0x4  },
            { IMPERIALIST,   0x020, 0x14 },
            { JUDICATOR,     0x010, 0x5  },
            { MAGMAUL,       0x008, 0xA  },
            { VOLT_DRIVER,   0x002, 0xA  },
            { OMEGA_CANNON,  0x100, 0    }
        } };

        constexpr uint64_t HOTKEY_BITS[] = {
            IB_WEAPON_BEAM, IB_WEAPON_MISSILE, IB_WEAPON_1, IB_WEAPON_2,
            IB_WEAPON_3,    IB_WEAPON_4,       IB_WEAPON_5, IB_WEAPON_6,
            IB_WEAPON_SPECIAL
        };

        constexpr uint8_t HOTKEY_TO_ID[] = {
            POWER_BEAM, MISSILE, SHOCK_COIL, BATTLEHAMMER, IMPERIALIST,
            JUDICATOR,  MAGMAUL, VOLT_DRIVER, NONE
        };

        // Weapon ID → position in ORDERED_WEAPONS (constexpr LUT)
        // Maps weapon ID (0-8) to its index in ORDERED_WEAPONS
        constexpr std::array<uint8_t, 9> ID_TO_ORDERED_IDX = { 0, 7, 1, 6, 5, 4, 3, 2, 8 };

    } // namespace WeaponData

    // =========================================================================
    // Weapon availability check — fold-expression based (replaces recursive template)
    //
    // Generates one bit per weapon in ORDERED_WEAPONS order.
    // Each weapon's availability is checked at compile-time where possible
    // (PowerBeam is always available, etc.)
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
            if constexpr (info.id == MAGMAUL) {
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
        // Fold expression: OR together all individual weapon availability bits
        return (CheckOneWeapon<Is>(having, weaponAmmo, missileAmmo, isWeavel) | ...);
    }

    // =========================================================================
    // ProcessWeaponSwitch — handles wheel/next/prev and direct hotkey selection
    // =========================================================================
    HOT_FUNCTION bool MelonPrimeCore::ProcessWeaponSwitch()
    {
        using namespace WeaponData;

        // --- Case 1: Mouse Wheel / Next / Prev ---
        if (LIKELY(!IsAnyPressed(IB_WEAPON_ANY))) {
            auto* panel = emuInstance->getMainWindow()->panel;
            if (!panel) return false;

            const int wheelDelta = panel->getDelta();
            const bool nextKey = IsPressed(IB_WEAPON_NEXT);
            const bool prevKey = IsPressed(IB_WEAPON_PREV);

            if (!wheelDelta && !nextKey && !prevKey) return false;

            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

            const bool forward = (wheelDelta < 0) || nextKey;

            // Read weapon state once
            const uint16_t having     = *m_ptrs.havingWeapons;
            const uint32_t ammoData   = *m_ptrs.weaponAmmo;
            const uint16_t weaponAmmo = static_cast<uint16_t>(ammoData & 0xFFFF);
            const uint16_t missileAmmo = static_cast<uint16_t>(ammoData >> 16);
            const bool isWeavel = m_flags.test(StateFlags::BIT_IS_WEAVEL);

            // Compile-time unrolled availability check
            const uint16_t availableBits = CheckAllWeapons(
                having, weaponAmmo, missileAmmo, isWeavel,
                std::make_index_sequence<ORDERED_WEAPONS.size()>{});

            if (!availableBits) {
                m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
                return false;
            }

            const uint8_t curID = *m_ptrs.currentWeapon;
            const uint8_t safeID = (curID >= 9) ? 0 : curID;
            const uint8_t currentIdx = ID_TO_ORDERED_IDX[safeID];
            constexpr size_t count = ORDERED_WEAPONS.size();

            // Cycle through weapons in the desired direction
            for (size_t offset = 1; offset < count; ++offset) {
                size_t checkIdx;
                if (forward) {
                    checkIdx = currentIdx + offset;
                    if (checkIdx >= count) checkIdx -= count;
                } else {
                    checkIdx = (offset > currentIdx)
                        ? count - (offset - currentIdx)
                        : currentIdx - offset;
                }

                if (availableBits & (1u << checkIdx)) {
                    SwitchWeapon(ORDERED_WEAPONS[checkIdx].id);
                    return true;
                }
            }

            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        // --- Case 2: Direct Weapon Hotkeys ---
        if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

        // Build bitmask of pressed weapon hotkeys
        uint32_t hot = 0;
        for (size_t i = 0; i < 9; ++i) {
            if (IsPressed(HOTKEY_BITS[i])) hot |= (1u << i);
        }

        // Find first set bit (lowest-numbered hotkey wins)
#if defined(_MSC_VER) && !defined(__clang__)
        unsigned long firstSetUL;
        _BitScanForward(&firstSetUL, hot);
        const int firstSet = static_cast<int>(firstSetUL);
#else
        const int firstSet = __builtin_ctz(hot);
#endif

        // Special Weapon (Affinity) — index 8
        if (UNLIKELY(firstSet == 8)) {
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

        // Use constexpr LUT to find the Info entry directly (no linear scan)
        const uint8_t orderedIdx = ID_TO_ORDERED_IDX[weaponID];
        const Info& info = ORDERED_WEAPONS[orderedIdx];

        const uint16_t having      = *m_ptrs.havingWeapons;
        const uint32_t ammoData    = *m_ptrs.weaponAmmo;
        const uint16_t weaponAmmo  = static_cast<uint16_t>(ammoData & 0xFFFF);
        const uint16_t missileAmmo = static_cast<uint16_t>(ammoData >> 16);

        // Ownership check
        const bool owned = (weaponID == POWER_BEAM || weaponID == MISSILE)
                         || ((having & info.mask) != 0);

        if (!owned) {
            emuInstance->osdAddMessage(0, "Have not %s yet!", kNames[weaponID].data());
            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        // Ammo check
        bool hasAmmo = true;
        if (weaponID == MISSILE) {
            hasAmmo = (missileAmmo >= 0xA);
        } else if (weaponID != POWER_BEAM && weaponID != OMEGA_CANNON) {
            uint8_t required = info.minAmmo;
            if (weaponID == MAGMAUL && m_flags.test(StateFlags::BIT_IS_WEAVEL)) required = 0x5;
            hasAmmo = (weaponAmmo >= required);
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
    // SwitchWeapon — executes the weapon change by manipulating game RAM
    // =========================================================================
    void MelonPrimeCore::SwitchWeapon(int weaponIndex)
    {
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

        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();

        if (!isStylusMode) {
            using namespace Consts::UI;
            emuInstance->getNDS()->TouchScreen(CENTER_RESET.x(), CENTER_RESET.y());
        } else if (emuInstance->isTouching) {
            emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
        }
        FrameAdvanceTwice();

        if (isRestoreNeeded) {
            const uint8_t current = *m_ptrs.jumpFlag;
            *m_ptrs.jumpFlag = (current & 0xF0) | jumpFlag;
        }
    }

} // namespace MelonPrime
