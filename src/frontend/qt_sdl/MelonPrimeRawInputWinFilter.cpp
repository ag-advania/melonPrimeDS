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
    MSG* msg = static_cast<MSG*>(message);

    // �������^�[���FWM_INPUT�ȊO�͑����ɏ��O
    if (Q_LIKELY(msg->message != WM_INPUT)) return false;

    // �ŏ����̃X�^�b�N�o�b�t�@�i128�o�C�g���E�A���C���j
    alignas(128) BYTE buffer[256];
    UINT size = sizeof(buffer);

    // RAWINPUT�f�[�^�擾�i���s�������Ƀ��^�[���j
    if (Q_UNLIKELY(GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam),
        RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)) {
        return false;
    }

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer);
    const DWORD dwType = raw->header.dwType;

    // �}�E�X�����i�ł��p�ɂȏ������ɔz�u�j
    if (Q_LIKELY(dwType == RIM_TYPEMOUSE)) {
        const RAWMOUSE& m = raw->data.mouse;

        // �ړ������F�[���`�F�b�N��P���r�Ŏ��s
        const LONG deltaX = m.lLastX;
        const LONG deltaY = m.lLastY;
        if (Q_LIKELY((deltaX | deltaY) != 0)) {
            dx.fetch_add(deltaX, std::memory_order_relaxed);
            dy.fetch_add(deltaY, std::memory_order_relaxed);
        }

        // �{�^�������F�t���O��0�Ȃ瑦���Ƀ��^�[��
        const USHORT flags = m.usButtonFlags;
        if (Q_LIKELY(flags == 0)) return false;

        // �{�^���}�b�s���O�F�R���p�C�����萔�z��
        static constexpr struct {
            USHORT downFlag, upFlag;
            size_t index;
        } buttonMap[] = {
            {RI_MOUSE_LEFT_BUTTON_DOWN,   RI_MOUSE_LEFT_BUTTON_UP,   kMB_Left},
            {RI_MOUSE_RIGHT_BUTTON_DOWN,  RI_MOUSE_RIGHT_BUTTON_UP,  kMB_Right},
            {RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, kMB_Middle},
            {RI_MOUSE_BUTTON_4_DOWN,      RI_MOUSE_BUTTON_4_UP,      kMB_X1},
            {RI_MOUSE_BUTTON_5_DOWN,      RI_MOUSE_BUTTON_5_UP,      kMB_X2}
        };

        // ���[�v�A�����[���F�e�{�^�����ʏ���
        for (const auto& btn : buttonMap) {
            const USHORT mask = btn.downFlag | btn.upFlag;
            if (Q_UNLIKELY(flags & mask)) {
                m_mb[btn.index].store((flags & btn.downFlag) ? 1 : 0,
                    std::memory_order_relaxed);
            }
        }
    }
    // �L�[�{�[�h����
    else if (Q_UNLIKELY(dwType == RIM_TYPEKEYBOARD)) {
        const RAWKEYBOARD& kb = raw->data.keyboard;
        UINT vk = kb.VKey;
        const USHORT flags = kb.Flags;
        const bool isUp = (flags & RI_KEY_BREAK) != 0;

        // ����L�[���K���F������ŏ���
        switch (vk) {
        case VK_SHIFT:
            vk = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
            break;
        case VK_CONTROL:
            vk = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
            break;
        case VK_MENU:
            vk = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
            break;
        }

        // �͈̓`�F�b�N��Ɍ��q�X�V
        if (Q_LIKELY(vk < m_vkDown.size())) {
            m_vkDown[vk].store(static_cast<uint8_t>(!isUp),
                std::memory_order_relaxed);
        }
    }

    return false;
#else
    Q_UNUSED(eventType) Q_UNUSED(message) Q_UNUSED(result)
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
    // 1) �܂� Raw(KB/Mouse) �o�^������΃`�F�b�N
    auto it = m_hkToVk.find(hk);
    if (it != m_hkToVk.end()) {
        const auto& vks = it->second;
        for (UINT vk : vks) {
            // �}�E�X
            if (vk <= VK_XBUTTON2) {
                switch (vk) {
                case VK_LBUTTON:  if (m_mb[kMB_Left].load(std::memory_order_relaxed))  return true; break;
                case VK_RBUTTON:  if (m_mb[kMB_Right].load(std::memory_order_relaxed)) return true; break;
                case VK_MBUTTON:  if (m_mb[kMB_Middle].load(std::memory_order_relaxed))return true; break;
                case VK_XBUTTON1: if (m_mb[kMB_X1].load(std::memory_order_relaxed))    return true; break;
                case VK_XBUTTON2: if (m_mb[kMB_X2].load(std::memory_order_relaxed))    return true; break;
                }
            }
            // �L�[�{�[�h
            else if (vk < m_vkDown.size() && m_vkDown[vk].load(std::memory_order_relaxed)) {
                return true;
            }
        }
    }

    // 2) ���ɃW���C�X�e�B�b�N�iEmuInstance �����t���X�V���� joyHotkeyMask�j���Q��
    const QBitArray* jm = m_joyHK;

    const int n = jm->size();
    if ((unsigned)hk < (unsigned)n && jm->testBit(hk)){
        return true;
    }

    return false;
}

bool RawInputWinFilter::hotkeyPressed(int hk) noexcept
{
    const bool down = hotkeyDown(hk);
    auto& prev = m_hkPrev[(size_t)hk & 511];
    const uint8_t was = prev.exchange(down, std::memory_order_acq_rel);
    return down && !was;
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    const bool down = hotkeyDown(hk);
    auto& prev = m_hkPrev[(size_t)hk & 511];
    const uint8_t was = prev.exchange(down, std::memory_order_acq_rel);
    return (!down) && was;
}