// �w�b�_�Q�Ɛ錾(�N���X��`�ƃV�O�l�`����v�̂���)
#include "MelonPrimeRawInputWinFilter.h"
// C�W�����o�͎Q�Ɛ錾(�f�o�b�O��printf���p�̂���)
#include <cstdio>
// �A���S���Y���Q�Ɛ錾(�⏕���[�e�B���e�B�̂���)
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <hidsdi.h>
#include <QBitArray>
#endif


///**
/// * �R���X�g���N�^��`.
/// *
/// * RawInput�o�^�Ɠ�����ԏ��������s��.
/// */
 // �����o�֐��{�̒�`(�����������̂���)
RawInputWinFilter::RawInputWinFilter()
{
    // �}�E�X�f�o�C�X�o�^�ݒ菈��(usage 0x01/0x02�w��̂���)
    rid[0] = { 0x01, 0x02, 0, nullptr };
    // �L�[�{�[�h�f�o�C�X�o�^�ݒ菈��(usage 0x01/0x06�w��̂���)
    rid[1] = { 0x01, 0x06, 0, nullptr };

    // �f�o�C�X�o�^�ďo����(��M�J�n�̂���)
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));

    // �L�[�z�񏉊�������(�댟�m�h�~�̂���)
    for (auto& a : m_vkDown) a.store(0, std::memory_order_relaxed);
    // �}�E�X�z�񏉊�������(�댟�m�h�~�̂���)
    for (auto& b : m_mb)     b.store(0, std::memory_order_relaxed);
    // ���΃f���^����������(�c���r���̂���)
    dx.store(0, std::memory_order_relaxed);
    // ���΃f���^����������(�c���r���̂���)
    dy.store(0, std::memory_order_relaxed);
}

///**
/// * �f�X�g���N�^��`.
/// *
/// * RawInput�o�^�������s��.
/// */
 // �����o�֐��{�̒�`(��n�������̂���)
RawInputWinFilter::~RawInputWinFilter()
{
    // �}�E�X�����w��ݒ菈��(�o�^�����̂���)
    rid[0].dwFlags = RIDEV_REMOVE; rid[0].hwndTarget = nullptr;
    // �L�[�{�[�h�����w��ݒ菈��(�o�^�����̂���)
    rid[1].dwFlags = RIDEV_REMOVE; rid[1].hwndTarget = nullptr;
    // �o�^�����ďo����(�N���[���A�b�v�̂���)
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}


/**
 * �l�C�e�B�u�C�x���g�t�B���^��`�i��T�C�N���œK���Łj
 *
 * �œK���|�C���g:
 * 1. �������^�[���ɂ��s�v�ȏ����X�L�b�v
 * 2. ���b�N�A�b�v�e�[�u���ɂ���������팸
 * 3. �r�b�g���Z�̍œK��
 * 4. �L���b�V���Ǐ����̌���
 */
 /**
  * �l�C�e�B�u�C�x���g�t�B���^��`�i��T�C�N���œK���Łj
  *
  * �œK���|�C���g:
  * 1. �������^�[���ɂ��s�v�ȏ����X�L�b�v
  * 2. ���b�N�A�b�v�e�[�u���ɂ���������팸
  * 3. �r�b�g���Z�̍œK��
  * 4. �L���b�V���Ǐ����̌���
  */
bool RawInputWinFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef _WIN32
    // OS���b�Z�[�W�擾����(�l�C�e�B�u���Q�Ƃ̂���)
    MSG* msg = static_cast<MSG*>(message);

    // �������^�[������(WM_INPUT�ȊO�̖��ʏ�������̂���)
    if (!msg || msg->message != WM_INPUT) return false;

    // �X�^�b�N��o�b�t�@�m�ۏ���(�q�[�v�m�ۉ���ɂ���x���̂���)
    alignas(64) BYTE buffer[sizeof(RAWINPUT)]; // �L���b�V�����C�����E�ɃA���C��(�������A�N�Z�X�������̂���)
    // RAWINPUT�r���[�擾����(�^���S�ȍĉ��߂̂���)
    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer);
    // �T�C�Y����������(API�Ăяo�������̂���)
    UINT size = sizeof(RAWINPUT);

    // ���̓f�[�^�擾����(Win32 API���p�̂���)
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam),
        RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1) {
        // ���s���������A����(���萫�m�ۂ̂���)
        return false;
    }

    // �f�o�C�X�^�C�v���o����(��������̂���)
    const DWORD dwType = raw->header.dwType;

    // �}�E�X���������`(��p�o�H�ō����������邽��)
    if (dwType == RIM_TYPEMOUSE) {
        // �}�E�X�\���̎Q�Ə���(�t�B�[���h�A�N�Z�X�ȗ����̂���)
        const RAWMOUSE& m = raw->data.mouse;

        // ���Έړ����Z���菈��(���ړ����̕s�v���Z����̂���)
        if (m.lLastX | m.lLastY) { // ��[�����菈��(OR�ŗ��������`�F�b�N�̂���)
            // X���Z����(���b�N���X���Z�ɂ���I�[�o�[�w�b�h�̂���)
            dx.fetch_add(m.lLastX, std::memory_order_relaxed);
            // Y���Z����(���b�N���X���Z�ɂ���I�[�o�[�w�b�h�̂���)
            dy.fetch_add(m.lLastY, std::memory_order_relaxed);
        }

        // �{�^���t���O�擾����(����񐔍ŏ����̂���)
        const USHORT f = m.usButtonFlags;
        // ���t���O�������A����(�s�v��������̂���)
        if (!f) return false;

        // �v�f�^���o����(m_mb�v�f�̐������^����|�C���^�^�𓾂邽��)
        using MbElem = std::remove_reference_t<decltype(m_mb[0])>;
        // �|�C���^�^�ʖ���`(�ǐ�����̂���)
        using MbElemPtr = MbElem*;

        // �}�b�s���O�\���̒�`(�^���S�|�C���^�ێ��̂���)
        struct ButtonMapping {
            // �����t���O��`(�r�b�g����̂���)
            USHORT downFlag;
            // ����t���O��`(�r�b�g����̂���)
            USHORT upFlag;
            // �Ώی��q�|�C���^��`(����store���s�̂���)
            MbElemPtr target;
        };

        // �}�b�s���O�z���`(this�����̂��ߔ�static�̗p�̂���)
        const ButtonMapping mappings[] = {
            // ���{�^���Ή���`(����/����𓝍��������邽��)
            {RI_MOUSE_LEFT_BUTTON_DOWN,   RI_MOUSE_LEFT_BUTTON_UP,   &m_mb[static_cast<std::size_t>(kMB_Left)]},
            // �E�{�^���Ή���`(����/����𓝍��������邽��)
            {RI_MOUSE_RIGHT_BUTTON_DOWN,  RI_MOUSE_RIGHT_BUTTON_UP,  &m_mb[static_cast<std::size_t>(kMB_Right)]},
            // ���{�^���Ή���`(����/����𓝍��������邽��)
            {RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, &m_mb[static_cast<std::size_t>(kMB_Middle)]},
            // X1�{�^���Ή���`(����/����𓝍��������邽��)
            {RI_MOUSE_BUTTON_4_DOWN,      RI_MOUSE_BUTTON_4_UP,      &m_mb[static_cast<std::size_t>(kMB_X1)]},
            // X2�{�^���Ή���`(����/����𓝍��������邽��)
            {RI_MOUSE_BUTTON_5_DOWN,      RI_MOUSE_BUTTON_5_UP,      &m_mb[static_cast<std::size_t>(kMB_X2)]}
        };

        // ���[�v�����J�n��`(����팸�ƋǏ�������̂���)
        for (const auto& map : mappings) {
            // �}�X�N��������(�P��AND�Ŋ֌W�L������̂���)
            const USHORT mask = map.downFlag | map.upFlag;
            // �֌W���菈��(�Y�����̂�store���s�̂���)
            if (f & mask) {
                // �l���菈��(����=1/���=0�̓�l����̂���)
                const uint8_t v = (f & map.downFlag) ? uint8_t(1) : uint8_t(0);
                // ���q�������ݏ���(relaxed�Œ�I�[�o�[�w�b�h�ێ��̂���)
                map.target->store(v, std::memory_order_relaxed);
            }
        }
    }
    // �L�[�{�[�h���������`(��p�o�H�ō����������邽��)
    else if (dwType == RIM_TYPEKEYBOARD) {
        // �L�[�{�[�h�\���̎Q�Ə���(�t�B�[���h�A�N�Z�X�ȗ����̂���)
        const RAWKEYBOARD& kb = raw->data.keyboard;
        // ���z�L�[�����擾����(�㑱�̐��K���ɔ����邽��)
        UINT vk = kb.VKey;
        // �t���O�擾����(����/�������̂���)
        const USHORT flags = kb.Flags;
        // ������菈��(�i�[�l���]�ɗp���邽��)
        const bool isKeyUp = (flags & RI_KEY_BREAK) != 0;

        // ����L�[���K������(���E/�g�����萮���̂���)
        if (vk == VK_SHIFT) {
            // ���EShift���ʏ���(�X�L�����R�[�h������L�[�擾�̂���)
            vk = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
        }
        // Ctrl���E��������(�g���r�b�g�ō��E����̂���)
        else if (vk == VK_CONTROL) {
            // ���ECtrl��������(�����L�[�̐��m�ȋL�^�̂���)
            vk = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
        }
        // Alt���E��������(�g���r�b�g�ō��E����̂���)
        else if (vk == VK_MENU) {
            // ���EAlt��������(�����L�[�̐��m�ȋL�^�̂���)
            vk = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
        }

        // �͈͖h�䏈��(�z�񋫊E���S�m�ۂ̂���)
        if (vk < m_vkDown.size()) {
            // ���q�������ݏ���(relaxed�Œ�I�[�o�[�w�b�h�ێ��̂���)
            m_vkDown[vk].store(static_cast<uint8_t>(!isKeyUp), std::memory_order_relaxed);
        }
    }
    // HID���������e����(�z��O�f�o�C�X�����̂���)

    // ���蕜�A����(Qt���֏����p���Ϗ��̂���)
    return false;
#else
    // ���g�p�����}�~����(�r���h�x������̂���)
    Q_UNUSED(eventType) Q_UNUSED(message) Q_UNUSED(result)
        // ���蕜�A����(��Windows���݊��̂���)
        return false;
#endif
}


/**
 * TODO �ǉ��̍œK���āi�N���X�݌v���x���j:
 *
 * 1. �o�b�t�@�T�C�Y�̎��O�m��:
 *    class RawInputWinFilter {
 *        alignas(64) BYTE m_buffer[sizeof(RAWINPUT)]; // �����o�ϐ��Ƃ��ĕێ�
 *    };
 *
 * 2. �r�b�g�}�X�N�����̍œK��:
 *    // �����̃t���O����x�Ƀ`�F�b�N
 *    constexpr USHORT ALL_BUTTON_FLAGS =
 *        RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP |
 *        RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP |
 *        RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP |
 *        RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP |
 *        RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP;
 *
 *    if (!(f & ALL_BUTTON_FLAGS)) return false; // �S�{�^�����֌W�Ȃ瑦���^�[��
 *
 *
 */

///**
/// * ���΃f���^�擾�֐���`.
/// *
/// * �ݐϒl�����o���[���N���A����.
/// */
 // �����o�֐��{�̒�`(���΃f���^���o���̂���)
void RawInputWinFilter::fetchMouseDelta(int& outDx, int& outDy)
{
    // �� relaxed �ŏ\���i�P�ɍŐV�l��������邾���j
    outDx = dx.exchange(0, std::memory_order_relaxed);
    outDy = dy.exchange(0, std::memory_order_relaxed);
}

///**
/// * ���΃f���^�j���֐���`.
/// *
/// * �c���𑦎��[��������.
/// */
 // �����o�֐��{�̒�`(�c�������̂���)
void RawInputWinFilter::discardDeltas()
{
    dx.exchange(0, std::memory_order_relaxed);
    dy.exchange(0, std::memory_order_relaxed);
}

///**
/// * �S�L�[��ԃ��Z�b�g�֐���`.
/// *
/// * ���ׂĖ������֖߂�.
/// */
 // �����o�֐��{�̒�`(�딚�}�~�̂���)
void RawInputWinFilter::resetAllKeys()
{
    // �z�񔽕��[��������(�������Քr���̂���)
    for (auto& a : m_vkDown) a.store(0, std::memory_order_relaxed);
}

///**
/// * �}�E�X�{�^����ԃ��Z�b�g�֐���`.
/// *
/// * ���E/��/X1/X2�𖢉����֖߂�.
/// */
 // �����o�֐��{�̒�`(�딚�}�~�̂���)
void RawInputWinFilter::resetMouseButtons()
{
    // �z�񔽕��[��������(�������Քr���̂���)
    for (auto& b : m_mb) b.store(0, std::memory_order_relaxed);
}

///**
/// * HK��VK�o�^�֐���`.
/// *
/// * �������X�V����.
/// */
 // �����o�֐��{�̒�`(�ݒ蔽�f�̂���)
void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks)
{
    // �o�^�X�V����(�㏑���K�p�̂���)
    m_hkToVk[hk] = vks;
}
///**
/// * HK��������֐���`.
/// *
/// * �����ꂩ��VK���������Ȃ�true.
/// */
bool RawInputWinFilter::hotkeyDown(int hk) const
{
    // 1) Raw�i�}�E�X/�L�[�{�[�h�j���Ƀ`�F�b�N
    auto it = m_hkToVk.find(hk);
    if (it != m_hkToVk.end()) {
        const auto& vks = it->second;
        for (UINT vk : vks) {
            if (vk <= VK_XBUTTON2) {
                switch (vk) {
                case VK_LBUTTON:  if (m_mb[kMB_Left].load(std::memory_order_relaxed)) return true; break;
                case VK_RBUTTON:  if (m_mb[kMB_Right].load(std::memory_order_relaxed)) return true; break;
                case VK_MBUTTON:  if (m_mb[kMB_Middle].load(std::memory_order_relaxed)) return true; break;
                case VK_XBUTTON1: if (m_mb[kMB_X1].load(std::memory_order_relaxed)) return true; break;
                case VK_XBUTTON2: if (m_mb[kMB_X2].load(std::memory_order_relaxed)) return true; break;
                }
            }
            else if (vk < m_vkDown.size() && m_vkDown[vk].load(std::memory_order_relaxed)) {
                return true;
            }
        }
    }
    return false;
    /*
    // 2) joystick�iQBitArray�j����Ń`�F�b�N
    const QBitArray* jm = m_joyHK;
    return jm && static_cast<unsigned>(hk) < static_cast<unsigned>(jm->size()) && jm->testBit(hk);*/
}

bool RawInputWinFilter::hotkeyPressed(int hk) noexcept {
    const bool down = hotkeyDown(hk); // ���܉������H
    auto& prev = m_hkPrev[static_cast<size_t>(hk) & 511];
    const uint8_t p = prev.exchange(down, std::memory_order_acq_rel);
    return down && !p;  // ��true�őO��false�Ȃ� Pressed
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept {
    const bool down = hotkeyDown(hk);
    auto& prev = m_hkPrev[static_cast<size_t>(hk) & 511];
    const uint8_t p = prev.exchange(down, std::memory_order_acq_rel);
    return (!down) && p; // ��false�őO��true�Ȃ� Released
}
