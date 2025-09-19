// �Ή��w�b�_�Q��(�錾��v�̂���)
#include "MelonPrimeHotkeyVkBinding.h"
#include "InputConfig/InputConfigDialog.h"
// Windows����(Win32�ŗL��������̂���)
#if defined(_WIN32)

namespace melonDS {

    // �萔��`(Qt�}�E�X�󎚃r�b�g���o�̂���)
    static constexpr int kQtMouseMark = 0xF0000000;

    // �����֐��錾(����Qt�L�[�ϊ��̂���)
    static inline bool ConvertSpecialQtKeyToVk(int qt, std::vector<UINT>& out) {
        // �ϊ����ʏ�����(�ǉ��}�������̂���)
        out.clear();
        // Qt����L�[����(����L�[���K���̂���)
        switch (qt) {
            // Shift�n����(���E���Ή��̂���)
        case 0x01000020: /* Qt::Key_Shift */ {
            // ���EVK�ǉ�(�݊��m�ۂ̂���)
            out.push_back(VK_LSHIFT);
            // �EShift�ǉ�(�݊��m�ۂ̂���)
            out.push_back(VK_RSHIFT);
            // �����ԋp(�Ăяo�����p���̂���)
            return true;
        }
                       // Control�n����(���E���Ή��̂���)
        case 0x01000021: /* Qt::Key_Control */ {
            // ��Control�ǉ�(�݊��m�ۂ̂���)
            out.push_back(VK_LCONTROL);
            // �EControl�ǉ�(�݊��m�ۂ̂���)
            out.push_back(VK_RCONTROL);
            // �����ԋp(�Ăяo�����p���̂���)
            return true;
        }
                       // Alt�n����(���E���Ή��̂���)
        case 0x01000023: /* Qt::Key_Alt */ {
            // ��Alt�ǉ�(�݊��m�ۂ̂���)
            out.push_back(VK_LMENU);
            // �EAlt�ǉ�(�݊��m�ۂ̂���)
            out.push_back(VK_RMENU);
            // �����ԋp(�Ăяo�����p���̂���)
            return true;
        }
                       // Tab����(���ڑΉ��̂���)
        case 0x01000001: /* Qt::Key_Tab */ {
            // VK�ǉ�(���ڑΉ��̂���)
            out.push_back(VK_TAB);
            // �����ԋp(�Ăяo�����p���̂���)
            return true;
        }
                       // PageUp����(���ڑΉ��̂���)
        case 0x01000016: /* Qt::Key_PageUp */ {
            // VK�ǉ�(���ڑΉ��̂���)
            out.push_back(VK_PRIOR);
            // �����ԋp(�Ăяo�����p���̂���)
            return true;
        }
                       // PageDown����(���ڑΉ��̂���)
        case 0x01000017: /* Qt::Key_PageDown */ {
            // VK�ǉ�(���ڑΉ��̂���)
            out.push_back(VK_NEXT);
            // �����ԋp(�Ăяo�����p���̂���)
            return true;
        }
                       // Space����(���ڑΉ��̂���)
        case 0x20: /* Qt::Key_Space */ {
            // VK�ǉ�(���ڑΉ��̂���)
            out.push_back(VK_SPACE);
            // �����ԋp(�Ăяo�����p���̂���)
            return true;
        }
                 // ���蕪��(���Ή��L�[�̂���)
        default:
            // ���s�ԋp(�㑱�p�X�։񑗂̂���)
            return false;
        }
    }

    // �֐��{�̒�`(Qt������VK��ϊ��̂���)
    std::vector<UINT> MapQtKeyIntToVks(int qtKey) {
        // �o�͔z���`(���ʕԋp�̂���)
        std::vector<UINT> vks;
        // �}�E�X�󎚔���(�{�^����ʕ���̂���)
        if ((qtKey & kQtMouseMark) == kQtMouseMark) {
            // ���ʃ{�^�����o(��ʎ��ʂ̂���)
            const int btn = (qtKey & ~kQtMouseMark);
            // ���{�^������(VK�Ή��̂���)
            if (btn == 0x00000001 /* Qt::LeftButton */) vks.push_back(VK_LBUTTON);
            // �E�{�^������(VK�Ή��̂���)
            else if (btn == 0x00000002 /* Qt::RightButton */) vks.push_back(VK_RBUTTON);
            // ���{�^������(VK�Ή��̂���)
            else if (btn == 0x00000004 /* Qt::MiddleButton */) vks.push_back(VK_MBUTTON);
            // X1����(VK�Ή��̂���)
            else if (btn == 0x00000008 /* Qt::ExtraButton1 */) vks.push_back(VK_XBUTTON1);
            // X2����(VK�Ή��̂���)
            else if (btn == 0x00000010 /* Qt::ExtraButton2 */) vks.push_back(VK_XBUTTON2);
            // �ԋp����(�}�E�X�����̂���)
            return vks;
        }
        // ����L�[�ϊ����s(����L�[���K���̂���)
        if (ConvertSpecialQtKeyToVk(qtKey, vks)) {
            // �ԋp����(����L�[�����̂���)
            return vks;
        }
        // �p������(ASCII�撼�ʂ̂���)
        if ((qtKey >= '0' && qtKey <= '9') || (qtKey >= 'A' && qtKey <= 'Z')) {
            // ����VK������(�݊��ێ��̂���)
            vks.push_back(static_cast<UINT>(qtKey));
            // �ԋp����(�p�������̂���)
            return vks;
        }
        // F�L�[�攻��(Qt��F1�J�n�R�[�h����̂���)
        if (qtKey >= 0x01000030 /* Qt::Key_F1 */ && qtKey <= 0x0100004F /* Qt::Key_F35 */) {
            // F�L�[�ԍ��Z�o����(�A�Ԍv�Z�̂���)
            const int idx = (qtKey - 0x01000030) + 1;
            // VK�Z�o����(Windows VKF��A���̂���)
            vks.push_back(static_cast<UINT>(VK_F1 + (idx - 1)));
            // �ԋp����(F�L�[�����̂���)
            return vks;
        }
        // ����t�H�[���o�b�N����(�ϊ����s�ی��̂���)
        return vks;
    }

    // �֐��{�̒�`(�ݒ�1���ځ�Raw�o�^�̂���)
    void BindOneHotkeyFromConfig(RawInputWinFilter* filter, int instance,
        const std::string& hkPath, int hkId) {
        // �t�B���^���ݔ���(���S���s�̂���)
        if (!filter) return;
        // �ݒ�e�[�u���擾����(�C���X�^���X�w��̂���)
        auto tbl = Config::GetLocalTable(instance);
        // Qt�����擾����(TOML�Q�Ƃ̂���)
        const int qt = tbl.GetInt(hkPath);
        // VK��ϊ�����(Qt�������K���̂���)
        std::vector<UINT> vks = MapQtKeyIntToVks(qt);
        // Raw�o�^����(HK��VK�Ή��ݒ�̂���)
        filter->setHotkeyVks(hkId, vks);
    }

    // �֐��{�̒�`(Shoot/Zoom�܂Ƃߓo�^�̂���)
    void BindShootZoomFromConfig(RawInputWinFilter* filter, int instance) {
        // �t�B���^���ݔ���(���S���s�̂���)
        if (!filter) return;
        // Shoot�o�^����(���HK�̂���)
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidShootScan", HK_MetroidShootScan);
        // Zoom�o�^����(�Y�[��HK�̂���)
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidZoom", HK_MetroidZoom);
        // ���p�o�^����(���]��HK�̂���)
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidScanShoot", HK_MetroidScanShoot);
    }

    void BindMetroidHotkeysFromConfig(RawInputWinFilter* filter, int instance)
    {
        if (!filter) return;

        // �ˌ�/�Y�[��
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidShootScan", HK_MetroidShootScan);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidScanShoot", HK_MetroidScanShoot);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidZoom", HK_MetroidZoom);

        // �ړ�
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidMoveForward", HK_MetroidMoveForward);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidMoveBack", HK_MetroidMoveBack);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidMoveLeft", HK_MetroidMoveLeft);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidMoveRight", HK_MetroidMoveRight);

        // �A�N�V����
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidJump", HK_MetroidJump);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidMorphBall", HK_MetroidMorphBall);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidHoldMorphBallBoost", HK_MetroidHoldMorphBallBoost);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidScanVisor", HK_MetroidScanVisor);

        // ���x�����i�Q�[�����j
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidIngameSensiUp", HK_MetroidIngameSensiUp);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidIngameSensiDown", HK_MetroidIngameSensiDown);

        // ���ڕ��큕�X�y�V����
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeaponBeam", HK_MetroidWeaponBeam);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeaponMissile", HK_MetroidWeaponMissile);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeapon1", HK_MetroidWeapon1);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeapon2", HK_MetroidWeapon2);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeapon3", HK_MetroidWeapon3);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeapon4", HK_MetroidWeapon4);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeapon5", HK_MetroidWeapon5);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeapon6", HK_MetroidWeapon6);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeaponSpecial", HK_MetroidWeaponSpecial);

        // Next / Previous
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeaponNext", HK_MetroidWeaponNext);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidWeaponPrevious", HK_MetroidWeaponPrevious);

        // UI�n���܂Ƃ߂�
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidUIOk", HK_MetroidUIOk);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidUILeft", HK_MetroidUILeft);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidUIRight", HK_MetroidUIRight);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidUIYes", HK_MetroidUIYes);
        BindOneHotkeyFromConfig(filter, instance, "Keyboard.HK_MetroidUINo", HK_MetroidUINo);
    }

} // namespace melonDS
#endif // _WIN32
