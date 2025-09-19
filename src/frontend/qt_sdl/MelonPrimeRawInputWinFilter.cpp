// �w�b�_�Q�Ɛ錾(�N���X��`�ƃV�O�l�`����v�̂���)
#include "MelonPrimeRawInputWinFilter.h"
// C�W�����o�͎Q�Ɛ錾(�f�o�b�O��printf���p�̂���)
#include <cstdio>
// �A���S���Y���Q�Ɛ錾(�⏕���[�e�B���e�B�̂���)
#include <algorithm>

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
    // MSG�擾����(Win32���b�Z�[�W�Q�Ƃ̂���)
    MSG* msg = static_cast<MSG*>(message);
    // �k�����葁�����A����(�h��ړI�̂���)
    if (!msg) return false;

    // WM_INPUT���菈��(Raw���͌��菈���̂���)
    if (msg->message == WM_INPUT) {
        // �X�^�b�N�z�uRAWINPUT�o�b�t�@����(�q�[�v����̂���)
        alignas(8) BYTE buffer[sizeof(RAWINPUT)];
        RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer);
        // �T�C�Y����������(�擾���w��̂���)
        UINT size = sizeof(RAWINPUT);

        // �f�[�^�擾�ďo����(���͎�̂̂���)
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam),
            RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1) {
            // �擾���s�ʉߏ���(Qt�����ێ��̂���)
            return false;
        }

        // �}�E�X���򔻒菈��(���Έړ�/�{�^�������̂���)
        if (raw->header.dwType == RIM_TYPEMOUSE) {
            // �}�E�X�f�[�^���ڎQ�Ə���(�|�C���^�팸�̂���)
            const auto& m = raw->data.mouse;

            // ���΍��W�����X�V����(�A�g�~�b�N�팸�̂���)
            dx.fetch_add(m.lLastX, std::memory_order_relaxed);
            dy.fetch_add(m.lLastY, std::memory_order_relaxed);

            // �{�^���t���O�擾����(����/������o�̂���)
            const USHORT f = m.usButtonFlags;

            // �r�b�g�}�X�N�ꊇ���菈��(����팸�̂���)
            if (f & (RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP)) {
                m_mb[kMB_Left].store(f & RI_MOUSE_LEFT_BUTTON_DOWN ? 1 : 0, std::memory_order_relaxed);
            }
            if (f & (RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP)) {
                m_mb[kMB_Right].store(f & RI_MOUSE_RIGHT_BUTTON_DOWN ? 1 : 0, std::memory_order_relaxed);
            }
            if (f & (RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP)) {
                m_mb[kMB_Middle].store(f & RI_MOUSE_MIDDLE_BUTTON_DOWN ? 1 : 0, std::memory_order_relaxed);
            }
            if (f & (RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP)) {
                m_mb[kMB_X1].store(f & RI_MOUSE_BUTTON_4_DOWN ? 1 : 0, std::memory_order_relaxed);
            }
            if (f & (RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP)) {
                m_mb[kMB_X2].store(f & RI_MOUSE_BUTTON_5_DOWN ? 1 : 0, std::memory_order_relaxed);
            }

            // Qt���ʉߕԋp����(�����ێ��̂���)
            return false;
        }

        // �L�[�{�[�h���򔻒菈��(VK������ԍX�V�̂���)
        if (raw->header.dwType == RIM_TYPEKEYBOARD) {
            // RAWKEYBOARD���ڎQ�Ə���(�t�B�[���h���o�̂���)
            const auto& kb = raw->data.keyboard;
            // ���z�L�[�擾����(���K���OVK�̂���)
            UINT vk = kb.VKey;
            // �t���O�擾����(���荂�����̂���)
            const USHORT flags = kb.Flags;

            // ����L�[�������菈��(switch�œK���̂���)
            switch (vk) {
            case VK_SHIFT:
                // Shift���K������(���E���ʂ̂���)
                vk = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
                break;
            case VK_CONTROL:
                // Control���K������(���E���ʂ̂���)
                vk = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
                break;
            case VK_MENU:
                // Alt���K������(���E���ʂ̂���)
                vk = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
                break;
            }

            // �͈͓����菈��(�z�񋫊E�ی�̂���)
            if (vk < m_vkDown.size()) {
                // ������Ԓ��ڏ�������(�O�����Z�q�팸�̂���)
                m_vkDown[vk].store(!(flags & RI_KEY_BREAK), std::memory_order_relaxed);
            }

            // Qt���ʉߕԋp����(�����ێ��̂���)
            return false;
        }

        // �t�H�[���o�b�N�ʉߕԋp����(���Ή���ʈێ��̂���)
        return false;
    }
#else
    // ���g�p�}�~����(��Win���̌x������̂���)
    Q_UNUSED(eventType)
        Q_UNUSED(message)
        Q_UNUSED(result)
#endif
        // ����ʉߕԋp����(Qt�������p���̂���)
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
    // X�����擾����(���ݒl��̂̂���)
    outDx = dx.exchange(0, std::memory_order_acq_rel);
    // Y�����擾����(���ݒl��̂̂���)
    outDy = dy.exchange(0, std::memory_order_acq_rel);
}

///**
/// * ���΃f���^�j���֐���`.
/// *
/// * �c���𑦎��[��������.
/// */
 // �����o�֐��{�̒�`(�c�������̂���)
void RawInputWinFilter::discardDeltas()
{
    // X�[������������(�����m�ۂ̂���)
    dx.exchange(0, std::memory_order_acq_rel);
    // Y�[������������(�����m�ۂ̂���)
    dy.exchange(0, std::memory_order_acq_rel);
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

