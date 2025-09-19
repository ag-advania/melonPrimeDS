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
 // �����o�֐��{�̒�`(Win32 RAW�����̂���)
bool RawInputWinFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef _WIN32
    // MSG�擾����(Win32���b�Z�[�W�Q�Ƃ̂���)
    MSG* msg = static_cast<MSG*>(message);
    // �k�����葁�����A����(�h��ړI�̂���)
    if (!msg) return false;

    // WM_INPUT���菈��(Raw���͌��菈���̂���)
    if (msg->message == WM_INPUT) {
        // RAWINPUT��̃o�b�t�@�m�ۏ���(�Œ蒷��̂̂���)
        RAWINPUT raw{};
        // �T�C�Y����������(�擾���w��̂���)
        UINT size = sizeof(RAWINPUT);
        // �f�[�^�擾�ďo����(���͎�̂̂���)
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam),
            RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1) {
            // �擾���s�ʉߏ���(Qt�����ێ��̂���)
            return false;
        }

        // �}�E�X���򔻒菈��(���Έړ�/�{�^�������̂���)
        if (raw.header.dwType == RIM_TYPEMOUSE) {
            // ����X�ݐω��Z����(���b�N���X�X�V�̂���)
            dx.fetch_add(static_cast<int>(raw.data.mouse.lLastX), std::memory_order_relaxed);
            // ����Y�ݐω��Z����(���b�N���X�X�V�̂���)
            dy.fetch_add(static_cast<int>(raw.data.mouse.lLastY), std::memory_order_relaxed);

            // �{�^���t���O�擾����(����/������o�̂���)
            const USHORT f = raw.data.mouse.usButtonFlags;
            // ���������f����(��ԍX�V�̂���)
            if (f & RI_MOUSE_LEFT_BUTTON_DOWN)   m_mb[kMB_Left].store(1, std::memory_order_relaxed);
            // ��������f����(��ԍX�V�̂���)
            if (f & RI_MOUSE_LEFT_BUTTON_UP)     m_mb[kMB_Left].store(0, std::memory_order_relaxed);
            // �E�������f����(��ԍX�V�̂���)
            if (f & RI_MOUSE_RIGHT_BUTTON_DOWN)  m_mb[kMB_Right].store(1, std::memory_order_relaxed);
            // �E������f����(��ԍX�V�̂���)
            if (f & RI_MOUSE_RIGHT_BUTTON_UP)    m_mb[kMB_Right].store(0, std::memory_order_relaxed);
            // ���������f����(��ԍX�V�̂���)
            if (f & RI_MOUSE_MIDDLE_BUTTON_DOWN) m_mb[kMB_Middle].store(1, std::memory_order_relaxed);
            // ��������f����(��ԍX�V�̂���)
            if (f & RI_MOUSE_MIDDLE_BUTTON_UP)   m_mb[kMB_Middle].store(0, std::memory_order_relaxed);
            // X1�������f����(��ԍX�V�̂���)
            if (f & RI_MOUSE_BUTTON_4_DOWN)      m_mb[kMB_X1].store(1, std::memory_order_relaxed);
            // X1������f����(��ԍX�V�̂���)
            if (f & RI_MOUSE_BUTTON_4_UP)        m_mb[kMB_X1].store(0, std::memory_order_relaxed);
            // X2�������f����(��ԍX�V�̂���)
            if (f & RI_MOUSE_BUTTON_5_DOWN)      m_mb[kMB_X2].store(1, std::memory_order_relaxed);
            // X2������f����(��ԍX�V�̂���)
            if (f & RI_MOUSE_BUTTON_5_UP)        m_mb[kMB_X2].store(0, std::memory_order_relaxed);

            // Qt���ʉߕԋp����(�����ێ��̂���)
            return false;
        }

        // �L�[�{�[�h���򔻒菈��(VK������ԍX�V�̂���)
        if (raw.header.dwType == RIM_TYPEKEYBOARD) {
            // RAWKEYBOARD�Q�Ə���(�t�B�[���h���o�̂���)
            const RAWKEYBOARD& kb = raw.data.keyboard;
            // ���z�L�[�ꎞ�擾����(���K���OVK�̂���)
            UINT vk = static_cast<UINT>(kb.VKey);
            // E0�g�����菈��(�C�����ʂ̂���)
            const bool e0 = (kb.Flags & RI_KEY_E0) != 0;
            // ������菈��(����/������ʂ̂���)
            const bool isUp = (kb.Flags & RI_KEY_BREAK) != 0;

            // Shift���K������(���E���ʂ̂���)
            if (vk == VK_SHIFT)   vk = MapVirtualKey(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
            // Control���K������(���E���ʂ̂���)
            if (vk == VK_CONTROL) vk = e0 ? VK_RCONTROL : VK_LCONTROL;
            // Alt���K������(���E���ʂ̂���)
            if (vk == VK_MENU)    vk = e0 ? VK_RMENU : VK_LMENU;

            // �͈͓����菈��(�z�񋫊E�ی�̂���)
            if (vk < m_vkDown.size()) {
                // ������ԏ�������(���b�N���X�X�V�̂���)
                m_vkDown[vk].store(isUp ? 0 : 1, std::memory_order_relaxed);
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
        // ���g�p�}�~����(��Win���̌x������̂���)
        Q_UNUSED(message)
        // ���g�p�}�~����(��Win���̌x������̂���)
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
 // �����o�֐��{�̒�`(�����Ɖ�̂���)
bool RawInputWinFilter::hotkeyDown(int hk) const
{
    // �}�b�v�T������(�o�^�m�F�̂���)
    auto it = m_hkToVk.find(hk);
    // ���o�^�������A����(���S���̂���)
    if (it == m_hkToVk.end()) return false;

    // VK��Q�Ə���(��������̂���)
    const auto& vks = it->second;

    // �}�E�XVK���C���f�b�N�X�ϊ������_��`(����ȗ����̂���)
    auto mouseIndex = [](UINT vk) -> size_t {
        // �����菈��(�C���f�b�N�X�ԋp�̂���)
        if (vk == VK_LBUTTON)  return kMB_Left;
        // �E���菈��(�C���f�b�N�X�ԋp�̂���)
        if (vk == VK_RBUTTON)  return kMB_Right;
        // �����菈��(�C���f�b�N�X�ԋp�̂���)
        if (vk == VK_MBUTTON)  return kMB_Middle;
        // X1���菈��(�C���f�b�N�X�ԋp�̂���)
        if (vk == VK_XBUTTON1) return kMB_X1;
        // X2���菈��(�C���f�b�N�X�ԋp�̂���)
        if (vk == VK_XBUTTON2) return kMB_X2;
        // ��Y���ԋp����(�ԕ����p�̂���)
        return SIZE_MAX;
        };

    // �������菈��(�����ꂩ�^�ŉ����m��̂���)
    for (UINT vk : vks) {
        // �}�E�X���菈��(��p�z��Q�Ƃ̂���)
        const size_t mi = mouseIndex(vk);
        // �}�E�X���򏈗�(�{�^����ԎQ�Ƃ̂���)
        if (mi != SIZE_MAX) {
            // �����Q�Ə���(�������m�̂���)
            if (m_mb[mi].load(std::memory_order_relaxed)) return true;
        }
        else {
            // �͈͓����菈��(�z�񋫊E�ی�̂���)
            if (vk < m_vkDown.size()) {
                // �����Q�Ə���(�������m�̂���)
                if (m_vkDown[vk].load(std::memory_order_relaxed)) return true;
            }
        }
    }

    // �񉟉��ԋp����(�S�ے�̂���)
    return false;
}
