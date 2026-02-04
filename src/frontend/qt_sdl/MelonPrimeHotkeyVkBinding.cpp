#include "MelonPrimeHotkeyVkBinding.h"
#include "MelonPrimeRawInputWinFilter.h"
#include "Config.h"
#include "EmuInstance.h" // HK_Metroid... íËêîÇÃíËã`å≥Ç∆ÇµÇƒí«â¡

#if defined(_WIN32)

namespace melonDS {

    static constexpr int kQtMouseMark = 0xF0000000;

    static inline bool ConvertSpecialQtKeyToVk(int qt, std::vector<UINT>& out) {
        out.clear();
        switch (qt) {
        case 0x01000020: out.push_back(VK_LSHIFT); out.push_back(VK_RSHIFT); return true;
        case 0x01000021: out.push_back(VK_LCONTROL); out.push_back(VK_RCONTROL); return true;
        case 0x01000023: out.push_back(VK_LMENU); out.push_back(VK_RMENU); return true;
        case 0x01000001: out.push_back(VK_TAB); return true;
        case 0x01000016: out.push_back(VK_PRIOR); return true;
        case 0x01000017: out.push_back(VK_NEXT); return true;
        case 0x20:       out.push_back(VK_SPACE); return true;
        default: return false;
        }
    }

    std::vector<UINT> MapQtKeyIntToVks(int qtKey) {
        std::vector<UINT> vks;
        if ((qtKey & kQtMouseMark) == kQtMouseMark) {
            const int btn = (qtKey & ~kQtMouseMark);
            if (btn == 0x01) vks.push_back(VK_LBUTTON);
            else if (btn == 0x02) vks.push_back(VK_RBUTTON);
            else if (btn == 0x04) vks.push_back(VK_MBUTTON);
            else if (btn == 0x08) vks.push_back(VK_XBUTTON1);
            else if (btn == 0x10) vks.push_back(VK_XBUTTON2);
            return vks;
        }
        if (ConvertSpecialQtKeyToVk(qtKey, vks)) return vks;
        if ((qtKey >= '0' && qtKey <= '9') || (qtKey >= 'A' && qtKey <= 'Z')) {
            vks.push_back(static_cast<UINT>(qtKey));
            return vks;
        }
        if (qtKey >= 0x01000030 && qtKey <= 0x0100004F) { // F1-F35
            const int idx = (qtKey - 0x01000030) + 1;
            vks.push_back(static_cast<UINT>(VK_F1 + (idx - 1)));
            return vks;
        }
        return vks;
    }

    void BindOneHotkeyFromConfig(MelonPrime::RawInputWinFilter* filter, int instance,
        const std::string& hkPath, int hkId) {
        if (!filter) return;
        auto tbl = Config::GetLocalTable(instance);
        const int qt = tbl.GetInt(hkPath);
        std::vector<UINT> vks = MapQtKeyIntToVks(qt);
        filter->setHotkeyVks(hkId, vks);
    }

    void BindMetroidHotkeysFromConfig(MelonPrime::RawInputWinFilter* filter, int instance)
    {
        if (!filter || instance != 0) return;

        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidShootScan", HK_MetroidShootScan);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidScanShoot", HK_MetroidScanShoot);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidZoom", HK_MetroidZoom);

        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidMoveForward", HK_MetroidMoveForward);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidMoveBack", HK_MetroidMoveBack);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidMoveLeft", HK_MetroidMoveLeft);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidMoveRight", HK_MetroidMoveRight);

        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidJump", HK_MetroidJump);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidMorphBall", HK_MetroidMorphBall);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidHoldMorphBallBoost", HK_MetroidHoldMorphBallBoost);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidScanVisor", HK_MetroidScanVisor);

        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeaponBeam", HK_MetroidWeaponBeam);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeaponMissile", HK_MetroidWeaponMissile);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeapon1", HK_MetroidWeapon1);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeapon2", HK_MetroidWeapon2);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeapon3", HK_MetroidWeapon3);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeapon4", HK_MetroidWeapon4);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeapon5", HK_MetroidWeapon5);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeapon6", HK_MetroidWeapon6);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeaponSpecial", HK_MetroidWeaponSpecial);

        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeaponCheck", HK_MetroidWeaponCheck);

        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeaponNext", HK_MetroidWeaponNext);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeaponPrevious", HK_MetroidWeaponPrevious);

        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidUIOk", HK_MetroidUIOk);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidUILeft", HK_MetroidUILeft);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidUIRight", HK_MetroidUIRight);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidUIYes", HK_MetroidUIYes);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidUINo", HK_MetroidUINo);

        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidMenu", HK_MetroidMenu);
    }

} // namespace melonDS
#endif