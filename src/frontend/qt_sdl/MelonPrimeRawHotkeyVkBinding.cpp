#include "MelonPrimeRawHotkeyVkBinding.h"
#include "MelonPrimeRawInputWinFilter.h"
#include "MelonPrimeDef.h"
#include "Config.h"
#include "EmuInstance.h"

#if defined(_WIN32)

namespace MelonPrime {

    // =========================================================================
    // R2: BindOneHotkeyFromConfig — now zero heap allocations.
    //
    // Uses the pointer+count setHotkeyVks overload introduced in R2,
    // passing SmallVkList's data directly instead of constructing a
    // temporary std::vector. Eliminates the last remaining heap allocation
    // in the hotkey binding path.
    // =========================================================================
    void BindOneHotkeyFromConfig(RawInputWinFilter* filter, RawInputSubscription* subscription, int instance,
        const std::string& hkPath, int hkId) {
        if (!filter || !subscription) return;
        auto tbl = Config::GetLocalTable(instance);
        const int qt = tbl.GetInt(hkPath);
        SmallVkList vks = MapQtKeyIntToVks(qt);
        filter->setHotkeyVks(subscription, hkId, vks.begin(), vks.size());
    }

    // =========================================================================
    // REFACTORED (R1 + R2): BindMetroidHotkeysFromConfig
    //
    // R1: Config table acquired once (was per-hotkey), SmallVkList for mapping
    // R2: Eliminated std::vector bridge — passes SmallVkList pointers directly
    //     to setHotkeyVks(int, const UINT*, size_t).
    //
    // Result: 0 heap allocations, 1 config table lookup (was one lookup and
    // one vector allocation per binding).
    // =========================================================================
    RawHotkeyOwnership BindMetroidHotkeysFromConfig(
        RawInputWinFilter* filter, RawInputSubscription* subscription, int instance)
    {
        RawHotkeyOwnership ownership;
        if (!filter || !subscription) return ownership;

        struct BindingDef {
            const char* configKey;
            int actionId;
        };

        static const BindingDef kBindings[] = {
            // Camera / Movement / Basic Actions
            { "Keyboard.HK_MetroidShootScan",          HK_MetroidShootScan },
            { "Keyboard.HK_MetroidStylusTouch",        HK_MetroidStylusTouch },
            { "Keyboard.HK_MetroidScanShoot",          HK_MetroidScanShoot },
            { "Keyboard.HK_MetroidScanShootStylus",    HK_MetroidScanShootStylus },
            { "Keyboard.HK_MetroidZoom",               HK_MetroidZoom },
            { "Keyboard.HK_MetroidMoveForward",        HK_MetroidMoveForward },
            { "Keyboard.HK_MetroidMoveBack",           HK_MetroidMoveBack },
            { "Keyboard.HK_MetroidMoveLeft",           HK_MetroidMoveLeft },
            { "Keyboard.HK_MetroidMoveRight",          HK_MetroidMoveRight },
            { "Keyboard.HK_MetroidJump",               HK_MetroidJump },
            { "Keyboard.HK_MetroidMorphBall",          HK_MetroidMorphBall },
            { "Keyboard.HK_MetroidHoldMorphBallBoost", HK_MetroidHoldMorphBallBoost },
            { "Keyboard.HK_MetroidScanVisor",          HK_MetroidScanVisor },

            // Weapons
            { "Keyboard.HK_MetroidWeaponBeam",         HK_MetroidWeaponBeam },
            { "Keyboard.HK_MetroidWeaponMissile",      HK_MetroidWeaponMissile },
            { "Keyboard.HK_MetroidWeapon1",            HK_MetroidWeapon1 },
            { "Keyboard.HK_MetroidWeapon2",            HK_MetroidWeapon2 },
            { "Keyboard.HK_MetroidWeapon3",            HK_MetroidWeapon3 },
            { "Keyboard.HK_MetroidWeapon4",            HK_MetroidWeapon4 },
            { "Keyboard.HK_MetroidWeapon5",            HK_MetroidWeapon5 },
            { "Keyboard.HK_MetroidWeapon6",            HK_MetroidWeapon6 },
            { "Keyboard.HK_MetroidWeaponSpecial",      HK_MetroidWeaponSpecial },
            { "Keyboard.HK_MetroidWeaponCheck",        HK_MetroidWeaponCheck },
            { "Keyboard.HK_MetroidWeaponNext",         HK_MetroidWeaponNext },
            { "Keyboard.HK_MetroidWeaponPrevious",     HK_MetroidWeaponPrevious },
            { "Keyboard.HK_MetroidWeaponNextSecondary",     HK_MetroidWeaponNextSecondary },
            { "Keyboard.HK_MetroidWeaponPreviousSecondary", HK_MetroidWeaponPreviousSecondary },

            // UI / Menu
            { "Keyboard.HK_MetroidUIOk",               HK_MetroidUIOk },
            { "Keyboard.HK_MetroidUILeft",             HK_MetroidUILeft },
            { "Keyboard.HK_MetroidUIRight",            HK_MetroidUIRight },
            { "Keyboard.HK_MetroidUIYes",              HK_MetroidUIYes },
            { "Keyboard.HK_MetroidUINo",               HK_MetroidUINo },
            { "Keyboard.HK_MetroidMenu",               HK_MetroidMenu },
        };

        // Single config table lookup for the entire binding batch.
        auto tbl = Config::GetLocalTable(instance);

        for (const auto& bind : kBindings) {
            const int qt = tbl.GetInt(bind.configKey);
            SmallVkList vks = MapQtKeyIntToVks(qt);
            // R2: Direct pointer pass — no std::vector construction
            filter->setHotkeyVks(subscription, bind.actionId, vks.begin(), vks.size());
            const uint64_t actionBit = 1ULL << bind.actionId;
            if (qt == InputKey::MouseWheelUp
                || qt == InputKey::MouseWheelDown) {
                ownership.wheelImpulseMask |= actionBit;
            }
            else if (qt != -1 && !vks.empty()) {
                ownership.rawOwnedGameplayMask |= actionBit;
            }
            else if (qt != -1) {
                // Empty Raw mapping is intentional here: the canonical Qt
                // identity remains live for chords, F25+, non-ASCII keys,
                // keypad combinations, and any future unsupported value.
                ownership.qtFallbackGameplayMask |= actionBit;
            }
        }
        return ownership;
    }

} // namespace MelonPrime
#endif
