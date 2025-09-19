// �w�b�_���d��ܖh�~(�r���h����̂���)
#pragma once
// Windows����(Win32�ŗL��������̂���)
#if defined(_WIN32)
// �W���z��Q��(�R���e�i���p�̂���)
#include <array>
// ���I�z��Q��(�ϒ�VK��ێ��̂���)
#include <vector>
// �A�z�z��Q��(HK��VK�Ή��\�Ǘ��̂���)
#include <unordered_map>
// ������Q��(�p�X�w��̂���)
#include <string>


#include <windows.h>

// �O���w�b�_�Q��(Raw�t�B���^API���p�̂���)
#include "MelonPrimeRawInputWinFilter.h"
// �O���w�b�_�Q��(�ݒ�擾�̂���)
#include "Config.h"

// ���O��Ԏw��(�Փˉ���̂���)
namespace melonDS {

    // �O���錾(�������[�e�B���e�B���J�̂���)
    /**
     * Qt�L�[������VK��ϊ��֐���`.
     *
     * @param qtKey Qt::Key/Qt::MouseButton�̐���.
     * @return �Ή�����VK�R�[�h�Q.
     */
     // �֐��錾(�L�[��ʐ��K���̂���)
    std::vector<UINT> MapQtKeyIntToVks(int qtKey);


    /**
     * �C��HK�o�^�֐���`.
     *
     * @param filter RawInput�t�B���^.
     * @param instance �ݒ�C���X�^���X�ԍ�.
     * @param hkPath TOML��HK�p�X(��:"Keyboard.HK_MetroidScanShoot").
     * @param hkId HK���ʎq(�Q�[�����ŗp����ID).
     */
     // �֐��錾(�ėp�o�^�̂���)
    void BindOneHotkeyFromConfig(RawInputWinFilter* filter, int instance,
        const std::string& hkPath, int hkId);

    // ���ǉ��FMPH�n�̎�vHK���ꊇ��Raw�ɓo�^
    void BindMetroidHotkeysFromConfig(RawInputWinFilter* filter, int instance);

} // namespace melonDS
#endif // _WIN32
