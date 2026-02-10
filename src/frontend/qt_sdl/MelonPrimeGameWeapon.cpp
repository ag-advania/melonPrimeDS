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
        // Weapon IDs as constants for clarity
        enum ID : uint8_t {
            POWER_BEAM = 0,
            VOLT_DRIVER = 1,
            MISSILE = 2,
            MAGMAUL = 3,
            JUDICATOR = 4,
            IMPERIALIST = 5,
            BATTLEHAMMER = 6,
            SHOCK_COIL = 7,
            OMEGA_CANNON = 8,
            NONE = 0xFF
        };

        static constexpr std::array<std::string_view, 9> kNames = {
            "Power Beam", "Volt Driver", "Missile Launcher", "Battlehammer",
            "Imperialist", "Judicator", "Magmaul", "Shock Coil", "Omega Cannon"
        };

        struct Info {
            uint8_t id;
            uint16_t mask;
            uint8_t minAmmo;
        };

        constexpr std::array<Info, 9> ORDERED_WEAPONS = { {
            {POWER_BEAM,    0x001, 0},
            {MISSILE,       0x004, 0x5},
            {SHOCK_COIL,    0x080, 0xA},
            {BATTLEHAMMER,  0x040, 0x4},
            {IMPERIALIST,   0x020, 0x14},
            {JUDICATOR,     0x010, 0x5},
            {MAGMAUL,       0x008, 0xA},
            {VOLT_DRIVER,   0x002, 0xA},
            {OMEGA_CANNON,  0x100, 0}
        } };

        constexpr uint64_t HOTKEY_BITS[] = {
            IB_WEAPON_BEAM, IB_WEAPON_MISSILE, IB_WEAPON_1, IB_WEAPON_2,
            IB_WEAPON_3, IB_WEAPON_4, IB_WEAPON_5, IB_WEAPON_6, IB_WEAPON_SPECIAL
        };

        constexpr uint8_t HOTKEY_TO_ID[] = {
            POWER_BEAM, MISSILE, SHOCK_COIL, BATTLEHAMMER, IMPERIALIST,
            JUDICATOR, MAGMAUL, VOLT_DRIVER, NONE
        };

        constexpr uint8_t ID_TO_ORDER_INDEX[] = { 0, 7, 1, 6, 5, 4, 3, 2, 8 };
    }

    // Optimization: Unrolled weapon availability checker
    template<size_t I>
    FORCE_INLINE void CheckWeaponAvailability(uint16_t& availableBits,
        const uint16_t having, const uint16_t weaponAmmo, const uint16_t missileAmmo, const bool isWeavel)
    {
        using namespace WeaponData;
        constexpr const Info& info = ORDERED_WEAPONS[I];

        bool available = false;

        // Compile-time conditional logic where possible
        if constexpr (info.id == POWER_BEAM) {
            available = true;
        }
        else if constexpr (info.id == MISSILE) {
            // Check ownership (always true for Missile in game logic usually, but consistent with original)
            // Original code: if (info.id != POWER_BEAM && info.id != MISSILE && !(having & info.mask)) return false;
            // -> Missile doesn't check 'having' mask in original logic, only PowerBeam and Missile skip it.
            if (missileAmmo >= 0xA) available = true;
        }
        else if constexpr (info.id == OMEGA_CANNON) {
            if (having & info.mask) available = true;
        }
        else {
            if (having & info.mask) {
                uint8_t req = info.minAmmo;
                if (info.id == MAGMAUL && isWeavel) req = 0x5; // Runtime check for Weavel
                if (weaponAmmo >= req) available = true;
            }
        }

        if (available) availableBits |= (1u << I);

        if constexpr (I + 1 < ORDERED_WEAPONS.size()) {
            CheckWeaponAvailability<I + 1>(availableBits, having, weaponAmmo, missileAmmo, isWeavel);
        }
    }

    HOT_FUNCTION bool MelonPrimeCore::ProcessWeaponSwitch()
    {
        using namespace WeaponData;

        // --- Case 1: Mouse Wheel / Next / Prev Keys ---
        if (LIKELY(!IsAnyPressed(IB_WEAPON_ANY))) {
            auto* panel = emuInstance->getMainWindow()->panel;
            if (!panel) return false;

            const int wheelDelta = panel->getDelta();
            const bool nextKey = IsPressed(IB_WEAPON_NEXT);
            const bool prevKey = IsPressed(IB_WEAPON_PREV);

            if (!wheelDelta && !nextKey && !prevKey) return false;

            if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

            const bool forward = (wheelDelta < 0) || nextKey;
            const uint8_t curID = *m_ptrs.currentWeapon;
            const uint16_t having = *m_ptrs.havingWeapons;
            const uint32_t ammoData = *m_ptrs.weaponAmmo;
            const uint16_t weaponAmmo = static_cast<uint16_t>(ammoData & 0xFFFF);
            const uint16_t missileAmmo = static_cast<uint16_t>(ammoData >> 16);
            const bool isWeavel = m_flags.test(StateFlags::BIT_IS_WEAVEL);

            uint16_t availableBits = 0;
            // Unrolled check
            CheckWeaponAvailability<0>(availableBits, having, weaponAmmo, missileAmmo, isWeavel);

            if (!availableBits) {
                m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
                return false;
            }

            uint8_t safeID = (curID >= 9) ? 0 : curID;
            uint8_t currentIdx = ID_TO_ORDER_INDEX[safeID];
            const size_t count = ORDERED_WEAPONS.size();

            // Linear search optimization (avoid modulo)
            for (size_t offset = 1; offset < count; ++offset) {
                size_t checkIdx;
                if (forward) {
                    checkIdx = currentIdx + offset;
                    if (checkIdx >= count) checkIdx -= count;
                }
                else {
                    checkIdx = (offset > currentIdx) ? count - (offset - currentIdx) : currentIdx - offset;
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

        uint32_t hot = 0;
        for (size_t i = 0; i < 9; ++i) {
            if (IsPressed(HOTKEY_BITS[i])) hot |= (1u << i);
        }

#if defined(_MSC_VER) && !defined(__clang__)
        unsigned long firstSetUL;
        _BitScanForward(&firstSetUL, hot);
        const int firstSet = static_cast<int>(firstSetUL);
#else
        const int firstSet = __builtin_ctz(hot);
#endif

        // Special Weapon (Affinity) Check
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
        const uint16_t having = *m_ptrs.havingWeapons;
        const uint32_t ammoData = *m_ptrs.weaponAmmo;
        const uint16_t weaponAmmo = static_cast<uint16_t>(ammoData & 0xFFFF);
        const uint16_t missileAmmo = static_cast<uint16_t>(ammoData >> 16);

        // Find info
        const Info* info = nullptr;
        for (const auto& w : ORDERED_WEAPONS) {
            if (w.id == weaponID) { info = &w; break; }
        }

        if (!info) {
            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        // Availability Check
        const bool owned = (weaponID == POWER_BEAM || weaponID == MISSILE) || ((having & info->mask) != 0);

        if (!owned) {
            emuInstance->osdAddMessage(0, "Have not %s yet!", kNames[weaponID].data());
            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        bool hasAmmo = true;
        if (weaponID == MISSILE) {
            hasAmmo = (missileAmmo >= 0xA);
        }
        else if (weaponID != POWER_BEAM && weaponID != OMEGA_CANNON) {
            uint8_t required = info->minAmmo;
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

        uint8_t currentJumpFlags = *m_ptrs.jumpFlag;
        bool isTransforming = currentJumpFlags & 0x10;
        uint8_t jumpFlag = currentJumpFlags & 0x0F;
        bool isRestoreNeeded = false;

        const bool isAltForm = (*m_ptrs.isAltForm) == 0x02;
        m_flags.assign(StateFlags::BIT_IS_ALT_FORM, isAltForm);

        if (!isTransforming && jumpFlag == 0 && !isAltForm) {
            *m_ptrs.jumpFlag = (currentJumpFlags & 0xF0) | 0x01;
            isRestoreNeeded = true;
        }

        *m_ptrs.weaponChange = ((*m_ptrs.weaponChange) & 0xF0) | 0x0B;
        *m_ptrs.selectedWeapon = weaponIndex;

        emuInstance->getNDS()->ReleaseScreen();
        FrameAdvanceTwice();

        if (!isStylusMode) {
            using namespace Consts::UI;
            emuInstance->getNDS()->TouchScreen(CENTER_RESET.x(), CENTER_RESET.y());
        }
        else if (emuInstance->isTouching) {
            emuInstance->getNDS()->TouchScreen(emuInstance->touchX, emuInstance->touchY);
        }
        FrameAdvanceTwice();

        if (isRestoreNeeded) {
            currentJumpFlags = *m_ptrs.jumpFlag;
            *m_ptrs.jumpFlag = (currentJumpFlags & 0xF0) | jumpFlag;
        }
    }

} // namespace MelonPrime
