#include "MelonPrimeRawHotkeyVkBinding.h"
#include "MelonPrimeRawInputWinFilter.h"
#include "Config.h"
#include "EmuInstance.h"

#if defined(_WIN32)

namespace MelonPrime {

    // ------------------------------------------------------------------------
    // Constants for Qt Key Codes
    // ------------------------------------------------------------------------
    namespace {
        constexpr int kQtKey_Shift = 0x01000020;
        constexpr int kQtKey_Control = 0x01000021;
        constexpr int kQtKey_Alt = 0x01000023;
        constexpr int kQtKey_Tab = 0x01000001;
        constexpr int kQtKey_PageUp = 0x01000016;
        constexpr int kQtKey_PageDown = 0x01000017;
        constexpr int kQtKey_Space = 0x20;
        constexpr int kQtKey_F1 = 0x01000030;
        constexpr int kQtKey_F35 = 0x0100004F;

        constexpr int kQtMouseMark = 0xF0000000;
    }

    // ------------------------------------------------------------------------
    // Helper Functions
    // ------------------------------------------------------------------------

    static inline bool TryAppendSpecialKey(int qt, std::vector<UINT>& out) {
        switch (qt) {
        case kQtKey_Shift:    out.push_back(VK_LSHIFT);   out.push_back(VK_RSHIFT);   return true;
        case kQtKey_Control:  out.push_back(VK_LCONTROL); out.push_back(VK_RCONTROL); return true;
        case kQtKey_Alt:      out.push_back(VK_LMENU);    out.push_back(VK_RMENU);    return true;
        case kQtKey_Tab:      out.push_back(VK_TAB);      return true;
        case kQtKey_PageUp:   out.push_back(VK_PRIOR);    return true;
        case kQtKey_PageDown: out.push_back(VK_NEXT);     return true;
        case kQtKey_Space:    out.push_back(VK_SPACE);    return true;
        default: return false;
        }
    }

    std::vector<UINT> MapQtKeyIntToVks(int qtKey) {
        std::vector<UINT> vks;
        vks.reserve(2); // ほとんどの場合は1つか2つなので確保しておく

        // Mouse Buttons
        if ((qtKey & kQtMouseMark) == kQtMouseMark) {
            const int btn = (qtKey & ~kQtMouseMark);
            switch (btn) {
            case 0x01: vks.push_back(VK_LBUTTON);  break;
            case 0x02: vks.push_back(VK_RBUTTON);  break;
            case 0x04: vks.push_back(VK_MBUTTON);  break;
            case 0x08: vks.push_back(VK_XBUTTON1); break;
            case 0x10: vks.push_back(VK_XBUTTON2); break;
            }
            return vks;
        }

        // Special Keys (Shift, Ctrl, Alt, etc.)
        if (TryAppendSpecialKey(qtKey, vks)) return vks;

        // ASCII Alphanumeric (0-9, A-Z)
        if ((qtKey >= '0' && qtKey <= '9') || (qtKey >= 'A' && qtKey <= 'Z')) {
            vks.push_back(static_cast<UINT>(qtKey));
            return vks;
        }

        // F-Keys (F1 - F35)
        if (qtKey >= kQtKey_F1 && qtKey <= kQtKey_F35) {
            const int idx = (qtKey - kQtKey_F1);
            vks.push_back(static_cast<UINT>(VK_F1 + idx));
            return vks;
        }

        return vks;
    }

    void BindOneHotkeyFromConfig(RawInputWinFilter* filter, int instance,
        const std::string& hkPath, int hkId) {
        if (!filter) return;
        auto tbl = Config::GetLocalTable(instance);
        const int qt = tbl.GetInt(hkPath);
        std::vector<UINT> vks = MapQtKeyIntToVks(qt);
        filter->setHotkeyVks(hkId, vks);
    }

    void BindMetroidHotkeysFromConfig(RawInputWinFilter* filter, int instance)
    {
        if (!filter || instance != 0) return;

        // Binding Definition Table
        struct BindingDef {
            const char* configKey;
            int actionId;
        };

        static const BindingDef kBindings[] = {
            // Camera / Movement / Basic Actions
            { "Keyboard.HK_MetroidShootScan",          HK_MetroidShootScan },
            { "Keyboard.HK_MetroidScanShoot",          HK_MetroidScanShoot },
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

            // UI / Menu
            { "Keyboard.HK_MetroidUIOk",               HK_MetroidUIOk },
            { "Keyboard.HK_MetroidUILeft",             HK_MetroidUILeft },
            { "Keyboard.HK_MetroidUIRight",            HK_MetroidUIRight },
            { "Keyboard.HK_MetroidUIYes",              HK_MetroidUIYes },
            { "Keyboard.HK_MetroidUINo",               HK_MetroidUINo },
            { "Keyboard.HK_MetroidMenu",               HK_MetroidMenu },
        };

        // Batch processing
        for (const auto& bind : kBindings) {
            BindOneHotkeyFromConfig(filter, instance, bind.configKey, bind.actionId);
        }
    }

} // namespace MelonPrime
#endif