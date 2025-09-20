// �w�b�_�Q�Ɛ錾(�N���X��`�ƃV�O�l�`����v�̂���)
#include "MelonPrimeRawInputWinFilter.h"
// C�W�����o�͎Q�Ɛ錾(�f�o�b�O��printf���p�̂���)
#include <cstdio>
// �A���S���Y���Q�Ɛ錾(�⏕���[�e�B���e�B�̂���)
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <hidsdi.h>
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



///**
/// * �l�C�e�B�u�C�x���g�t�B���^��`.
/// *
/// * WM_INPUT����}�E�X/�L�[�{�[�h��Ԃ����W����.
/// */
bool RawInputWinFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef _WIN32
    MSG* msg = static_cast<MSG*>(message);
    if (!msg) return false;

    // RawInput
    if (msg->message == WM_INPUT) {
        alignas(8) BYTE buffer[sizeof(RAWINPUT)];
        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer);
        UINT size = sizeof(RAWINPUT);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam),
            RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1) {
            return false;
        }

        if (raw->header.dwType == RIM_TYPEMOUSE) {
            const auto& m = raw->data.mouse;
            dx.fetch_add(m.lLastX, std::memory_order_relaxed);
            dy.fetch_add(m.lLastY, std::memory_order_relaxed);
            const USHORT f = m.usButtonFlags;
            if (f & (RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP)) m_mb[kMB_Left].store((f & RI_MOUSE_LEFT_BUTTON_DOWN) ? 1 : 0, std::memory_order_relaxed);
            if (f & (RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP)) m_mb[kMB_Right].store((f & RI_MOUSE_RIGHT_BUTTON_DOWN) ? 1 : 0, std::memory_order_relaxed);
            if (f & (RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP)) m_mb[kMB_Middle].store((f & RI_MOUSE_MIDDLE_BUTTON_DOWN) ? 1 : 0, std::memory_order_relaxed);
            if (f & (RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP)) m_mb[kMB_X1].store((f & RI_MOUSE_BUTTON_4_DOWN) ? 1 : 0, std::memory_order_relaxed);
            if (f & (RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP)) m_mb[kMB_X2].store((f & RI_MOUSE_BUTTON_5_DOWN) ? 1 : 0, std::memory_order_relaxed);
            return false;
        }

        if (raw->header.dwType == RIM_TYPEKEYBOARD) {
            const auto& kb = raw->data.keyboard;
            UINT vk = kb.VKey;
            const USHORT flags = kb.Flags;
            switch (vk) {
            case VK_SHIFT:   vk = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX); break;
            case VK_CONTROL: vk = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL; break;
            case VK_MENU:    vk = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;    break;
            }
            if (vk < m_vkDown.size()) {
                m_vkDown[vk].store(!(flags & RI_KEY_BREAK), std::memory_order_relaxed);
            }
            return false;
        }
        return false;
    }
#else
    Q_UNUSED(eventType) Q_UNUSED(message) Q_UNUSED(result)
#endif
        return false;
}

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
    // �}�b�v�T������(�o�^�m�F�̂���)
    auto it = m_hkToVk.find(hk);
    // ���o�^�������A����(���S���̂���)
    if (it == m_hkToVk.end()) return false;

    // VK��Q�Ə���(��������̂���)
    const auto& vks = it->second;

    // �������菈��(�����ꂩ�^�ŉ����m��̂���)
    for (UINT vk : vks) {
        // �}�E�X�������菈��(�r�b�g���Z���p�̂���)
        if (vk <= VK_XBUTTON2) {
            // �}�E�X�{�^�����ڔ��菈��(����팸�̂���)
            switch (vk) {
            case VK_LBUTTON:  if (m_mb[kMB_Left].load(std::memory_order_relaxed)) return true; break;
            case VK_RBUTTON:  if (m_mb[kMB_Right].load(std::memory_order_relaxed)) return true; break;
            case VK_MBUTTON:  if (m_mb[kMB_Middle].load(std::memory_order_relaxed)) return true; break;
            case VK_XBUTTON1: if (m_mb[kMB_X1].load(std::memory_order_relaxed)) return true; break;
            case VK_XBUTTON2: if (m_mb[kMB_X2].load(std::memory_order_relaxed)) return true; break;
            }
        }
        // �L�[�{�[�h���菈��(�z�񋫊E�ی�̂���)
        else if (vk < m_vkDown.size() && m_vkDown[vk].load(std::memory_order_relaxed)) {
            return true;
        }
    }

    // �񉟉��ԋp����(�S�ے�̂���)
    return false;
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
