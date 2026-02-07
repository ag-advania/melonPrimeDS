#include "MelonPrimeInternal.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "main.h"
#include "Screen.h"
#include "MelonPrimeDef.h"

#include <array>
#include <string_view>

    namespace MelonPrime {

    static constexpr std::array<std::string_view, 9> kWeaponNames = {
        "Power Beam", "Volt Driver", "Missile Launcher", "Battlehammer",
        "Imperialist", "Judicator", "Magmaul", "Shock Coil", "Omega Cannon"
    };

    namespace WeaponData {
        struct Info {
            uint8_t id;
            uint16_t mask;
            uint8_t minAmmo;
        };
        constexpr std::array<Info, 9> ORDERED_WEAPONS = { {
            {0, 0x001, 0},    // Power Beam
            {2, 0x004, 0x5},  // Missile
            {7, 0x080, 0xA},  // Volt Driver
            {6, 0x040, 0x4},  // Battlehammer
            {5, 0x020, 0x14}, // Imperialist
            {4, 0x010, 0x5},  // Judicator
            {3, 0x008, 0xA},  // Magmaul
            {1, 0x002, 0xA},  // Shock Coil
            {8, 0x100, 0}     // Omega Cannon
        } };
        constexpr uint64_t BIT_MAP[] = {
            IB_WEAPON_BEAM, IB_WEAPON_MISSILE, IB_WEAPON_1, IB_WEAPON_2,
            IB_WEAPON_3, IB_WEAPON_4, IB_WEAPON_5, IB_WEAPON_6, IB_WEAPON_SPECIAL
        };
        constexpr uint8_t BIT_WEAPON_ID[] = {
            0, 2, 7, 6, 5, 4, 3, 1, 0xFF
        };
        constexpr uint8_t ID_TO_INDEX[] = { 0, 7, 1, 6, 5, 4, 3, 2, 8 };
    }

    HOT_FUNCTION bool MelonPrimeCore::ProcessWeaponSwitch()
    {
        using namespace WeaponData;

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

            auto isWeaponAvailable = [&](const Info& info) -> bool {
                if (info.id != 0 && info.id != 2 && !(having & info.mask)) return false;
                if (info.id == 2) return missileAmmo >= 0xA;
                if (info.id != 0 && info.id != 8) {
                    uint8_t req = info.minAmmo;
                    if (info.id == 3 && isWeavel) req = 0x5;
                    return weaponAmmo >= req;
                }
                return true;
                };

            uint16_t available = 0;
            const size_t count = ORDERED_WEAPONS.size(); // 9

            for (size_t i = 0; i < count; ++i) {
                if (isWeaponAvailable(ORDERED_WEAPONS[i])) available |= (1u << i);
            }

            if (!available) {
                m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
                return false;
            }

            uint8_t safeID = (curID >= 9) ? 0 : curID;
            uint8_t currentIdx = ID_TO_INDEX[safeID];

            // Optimization: Linear search from the next expected index.
            // This avoids modulo (%) logic which is expensive in hot paths, 
            // and simplifies wrapping logic.

            for (size_t offset = 1; offset < count; ++offset) {
                size_t checkIdx;

                if (forward) {
                    checkIdx = currentIdx + offset;
                    if (checkIdx >= count) checkIdx -= count;
                }
                else {
                    // Reverse logic: Handle wrapping safely without signed modulo issues
                    if (offset > currentIdx) checkIdx = count - (offset - currentIdx);
                    else checkIdx = currentIdx - offset;
                }

                // Safety clamp although logic above should prevent OOB
                if (checkIdx >= count) checkIdx = 0;

                if (available & (1u << checkIdx)) {
                    SwitchWeapon(ORDERED_WEAPONS[checkIdx].id);
                    return true;
                }
            }

            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        if (isStylusMode) m_flags.set(StateFlags::BIT_BLOCK_STYLUS);

        uint32_t hot = 0;
        // Optimization: Unroll loop or allow compiler to vectorise this simple check
        for (size_t i = 0; i < 9; ++i) {
            if (IsPressed(BIT_MAP[i])) hot |= (1u << i);
        }

#if defined(_MSC_VER) && !defined(__clang__)
        unsigned long firstSetUL;
        _BitScanForward(&firstSetUL, hot);
        const int firstSet = static_cast<int>(firstSetUL);
#else
        const int firstSet = __builtin_ctz(hot);
#endif

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

        const uint8_t weaponID = BIT_WEAPON_ID[firstSet];
        const uint16_t having = *m_ptrs.havingWeapons;
        const uint32_t ammoData = *m_ptrs.weaponAmmo;
        const uint16_t weaponAmmo = static_cast<uint16_t>(ammoData & 0xFFFF);
        const uint16_t missileAmmo = static_cast<uint16_t>(ammoData >> 16);

        const Info* info = nullptr;
        for (const auto& w : ORDERED_WEAPONS) {
            if (w.id == weaponID) { info = &w; break; }
        }

        if (!info) {
            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        const bool owned = (weaponID == 0 || weaponID == 2) || ((having & info->mask) != 0);

        if (!owned) {
            emuInstance->osdAddMessage(0, "Have not %s yet!", kWeaponNames[weaponID].data());
            m_flags.clear(StateFlags::BIT_BLOCK_STYLUS);
            return false;
        }

        bool hasAmmo = true;
        if (weaponID == 2) {
            hasAmmo = (missileAmmo >= 0xA);
        }
        else if (weaponID != 0 && weaponID != 8) {
            uint8_t required = info->minAmmo;
            if (weaponID == 3 && m_flags.test(StateFlags::BIT_IS_WEAVEL)) required = 0x5;
            hasAmmo = (weaponAmmo >= required);
        }

        if (!hasAmmo) {
            emuInstance->osdAddMessage(0, "Not enough Ammo for %s!", kWeaponNames[weaponID].data());
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