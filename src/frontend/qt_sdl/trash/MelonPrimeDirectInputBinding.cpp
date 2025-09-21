// MelonPrimeDirectInputBinding.cpp
#ifdef _WIN32
#include "MelonPrimeDirectInputBinding.h"
#include "MelonPrimeDirectInputFilter.h"
#include "Config.h"
#include "InputConfig/InputConfigDialog.h"

using Axis = MelonPrimeDirectInputFilter::Axis;
using Config::Table;

// SDL�݊��� �gjoy int�h �� DirectInput �Ɋ񂹂ĉ���
//   - �{�^��: (val & 0xFFFF)
//   - �n�b�g: (val & 0x100) && (val&0xF in {1(U),2(R),4(D),8(L)})
//   - ��    : (val & 0x10000) with axisnum=(val>>24)&0xF, axisdir=(val>>20)&0xF
//             axisnum: 0:X 1:Y 2:Z 3:Rx 4:Ry 5:Rz ��z��
static inline void BindOneHK_FromJoyInt(MelonPrimeDirectInputFilter* din, Table joy, const char* key, int hk) {
    if (!din) return;
    const int v = joy.GetInt(key);
    if (v == -1) { din->clearBinding(hk); return; }

    // �n�b�g �� POV0
    if (v & 0x100) {
        const int hatdir = (v & 0xF);
        if (hatdir == 0x1) din->bindPOVDirection(hk, 0); // U
        else if (hatdir == 0x2) din->bindPOVDirection(hk, 1); // R
        else if (hatdir == 0x4) din->bindPOVDirection(hk, 2); // D
        else if (hatdir == 0x8) din->bindPOVDirection(hk, 3); // L
        return;
    }

    // �� �� �������l
    if (v & 0x10000) {
        const int axisnum = (v >> 24) & 0xF;   // 0:X 1:Y 2:Z 3:Rx 4:Ry 5:Rz
        const int axisdir = (v >> 20) & 0xF;   // 0:+ 1:- 2:trigger
        if (axisdir == 2) {
            // trigger �� RT/LT �̂ǂ��炩�Ɋ񂹂�BSDL�̊���� 5:Rz��RT, 2:Z��LT �Ɖ���
            if (axisnum == 5) din->bindAxisThreshold(hk, Axis::RT, 0.25f);
            else              din->bindAxisThreshold(hk, Axis::LT, 0.25f);
            return;
        }
        // �X�e�B�b�N
        switch (axisnum) {
        case 0: din->bindAxisThreshold(hk, axisdir == 0 ? Axis::LXPos : Axis::LXNeg, 0.5f); break;
        case 1: din->bindAxisThreshold(hk, axisdir == 0 ? Axis::LYPos : Axis::LYNeg, 0.5f); break;
        case 3: din->bindAxisThreshold(hk, axisdir == 0 ? Axis::RXPos : Axis::RXNeg, 0.5f); break;
        case 4: din->bindAxisThreshold(hk, axisdir == 0 ? Axis::RYPos : Axis::RYNeg, 0.5f); break;
        case 2: // Z ���ǂ��炩�̃g���K����
        case 5: // Rz ���ǂ��炩�̃g���K����
        default: din->bindAxisThreshold(hk, Axis::RT, 0.25f); break;
        }
        return;
    }

    // �{�^��
    const int btn = (v & 0xFFFF);
    if (btn >= 0 && btn < 128) din->bindButton(hk, static_cast<uint8_t>(btn));
}

// �K�v�� Metroid HK �ꊇ
void MelonPrime_BindMetroidHotkeysFromJoystickConfig(MelonPrimeDirectInputFilter* din, int instance) {
    if (!din) return;
    Table joy = Config::GetLocalTable(instance).GetTable("Joystick");

    // �ˌ�/�Y�[��
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidShootScan", HK_MetroidShootScan);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidScanShoot", HK_MetroidScanShoot);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidZoom", HK_MetroidZoom);

    // �ړ�
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidMoveForward", HK_MetroidMoveForward);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidMoveBack", HK_MetroidMoveBack);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidMoveLeft", HK_MetroidMoveLeft);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidMoveRight", HK_MetroidMoveRight);

    // �A�N�V����
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidJump", HK_MetroidJump);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidMorphBall", HK_MetroidMorphBall);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidHoldMorphBallBoost", HK_MetroidHoldMorphBallBoost);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidScanVisor", HK_MetroidScanVisor);

    // UI
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidUILeft", HK_MetroidUILeft);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidUIRight", HK_MetroidUIRight);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidUIOk", HK_MetroidUIOk);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidUIYes", HK_MetroidUIYes);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidUINo", HK_MetroidUINo);

    // ����
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidWeaponBeam", HK_MetroidWeaponBeam);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidWeaponMissile", HK_MetroidWeaponMissile);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidWeaponSpecial", HK_MetroidWeaponSpecial);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidWeaponNext", HK_MetroidWeaponNext);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidWeaponPrevious", HK_MetroidWeaponPrevious);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidWeapon1", HK_MetroidWeapon1);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidWeapon2", HK_MetroidWeapon2);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidWeapon3", HK_MetroidWeapon3);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidWeapon4", HK_MetroidWeapon4);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidWeapon5", HK_MetroidWeapon5);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidWeapon6", HK_MetroidWeapon6);

    // ���j���[/���x
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidMenu", HK_MetroidMenu);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidIngameSensiUp", HK_MetroidIngameSensiUp);
    BindOneHK_FromJoyInt(din, joy, "HK_MetroidIngameSensiDown", HK_MetroidIngameSensiDown);
}
#endif
