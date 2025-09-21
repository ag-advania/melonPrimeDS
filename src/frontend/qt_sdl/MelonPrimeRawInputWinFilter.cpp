#include "MelonPrimeRawInputWinFilter.h"
#include <cstring>
#include <algorithm>
#include <immintrin.h>

#ifdef _WIN32
#include <windows.h>
#include <hidsdi.h>
#endif

RawInputWinFilter::RawInputWinFilter()
{
    rid[0] = { 0x01, 0x02, 0, nullptr }; // mouse
    rid[1] = { 0x01, 0x06, 0, nullptr }; // keyboard
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));

    // ����������������荂����
    std::memset(m_vkDown.data(), 0, sizeof(m_vkDown));
    std::memset(m_mb.data(), 0, sizeof(m_mb));
    std::memset(m_hkPrev.data(), 0, sizeof(m_hkPrev));

    dx.store(0, std::memory_order_relaxed);
    dy.store(0, std::memory_order_relaxed);

    if (!m_joyHK) m_joyHK = &kEmptyMask;
}

RawInputWinFilter::~RawInputWinFilter()
{
    rid[0].dwFlags = RIDEV_REMOVE; rid[0].hwndTarget = nullptr;
    rid[1].dwFlags = RIDEV_REMOVE; rid[1].hwndTarget = nullptr;
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

bool RawInputWinFilter::nativeEventFilter(const QByteArray& /*eventType*/, void* message, qintptr* /*result*/)
{
#ifdef _WIN32
    MSG* const msg = static_cast<MSG*>(message);

    // �������^�[���ŕ���\�������P
    if (Q_UNLIKELY(!msg)) [[unlikely]] return false;
    if (Q_LIKELY(msg->message != WM_INPUT)) [[likely]] return false;

    // �o�b�t�@�T�C�Y�����O�v�Z
    constexpr UINT expectedSize = sizeof(RAWINPUT);
    UINT size = expectedSize;

    // GetRawInputData�̌Ăяo���œK��
    const UINT result = GetRawInputData(
        reinterpret_cast<HRAWINPUT>(msg->lParam),
        RID_INPUT,
        m_rawBuf,
        &size,
        sizeof(RAWINPUTHEADER)
    );

    if (Q_UNLIKELY(result == static_cast<UINT>(-1))) [[unlikely]] return false;

    const RAWINPUT* const raw = reinterpret_cast<const RAWINPUT*>(m_rawBuf);
    const DWORD deviceType = raw->header.dwType;

    // �ł��p�ɂȃP�[�X���ŏ��ɏ���
    if (Q_LIKELY(deviceType == RIM_TYPEMOUSE)) [[likely]] {
        const RAWMOUSE& mouse = raw->data.mouse;

        // �}�E�X�ړ��̏����i�œK���j
        const LONG deltaX = mouse.lLastX;
        const LONG deltaY = mouse.lLastY;

        // �r�b�g���Z��0�`�F�b�N��������
        if (Q_LIKELY((deltaX | deltaY) != 0)) [[likely]] {
            dx.fetch_add(deltaX, std::memory_order_relaxed);
            dy.fetch_add(deltaY, std::memory_order_relaxed);
        }

        const USHORT buttonFlags = mouse.usButtonFlags;
        const USHORT relevantFlags = buttonFlags & kAllMouseBtnMask;

        // �֌W�Ȃ��C�x���g�͑����ɃX�L�b�v
        if (Q_LIKELY(relevantFlags == 0)) [[likely]] return false;

        // �{�^��������W�J���[�v�ōœK��
        constexpr auto& maps = kButtonMaps;

        // �R���p�C���̍œK���𑣐i
#pragma GCC unroll 5
        for (size_t i = 0; i < 5; ++i) {
            const auto& map = maps[i];
            const USHORT mask = map.down | map.up;

            if (Q_UNLIKELY(relevantFlags & mask)) [[unlikely]] {
                const uint8_t state = (buttonFlags & map.down) ? 1u : 0u;
                m_mb[map.idx].store(state, std::memory_order_relaxed);
            }
        }
    }
    else if (Q_UNLIKELY(deviceType == RIM_TYPEKEYBOARD)) [[unlikely]] {
        const RAWKEYBOARD& keyboard = raw->data.keyboard;
        UINT virtualKey = keyboard.VKey;
        const USHORT flags = keyboard.Flags;
        const bool isKeyUp = (flags & RI_KEY_BREAK) != 0;

        // �C���L�[�̐��K���i����\�������P�j
        switch (virtualKey) {
        case VK_SHIFT:
            virtualKey = MapVirtualKey(keyboard.MakeCode, MAPVK_VSC_TO_VK_EX);
            break;
        case VK_CONTROL:
            virtualKey = (flags & RI_KEY_E0) ? VK_RCONTROL : VK_LCONTROL;
            break;
        case VK_MENU:
            virtualKey = (flags & RI_KEY_E0) ? VK_RMENU : VK_LMENU;
            break;
        default:
            break;
        }

        // ���E�`�F�b�N���œK��
        if (Q_LIKELY(virtualKey < m_vkDown.size())) [[likely]] {
            const uint8_t state = static_cast<uint8_t>(!isKeyUp);
            m_vkDown[virtualKey].store(state, std::memory_order_relaxed);
        }
    }

    return false;
#else
    return false;
#endif
}

void RawInputWinFilter::resetAllKeys() noexcept
{
    // SIMD�g�p�������iAVX2���g����ꍇ�j
#ifdef __AVX2__
    const __m256i zero = _mm256_setzero_si256();
    uint8_t* ptr = reinterpret_cast<uint8_t*>(m_vkDown.data());

    for (size_t i = 0; i < sizeof(m_vkDown); i += 32) {
        _mm256_store_si256(reinterpret_cast<__m256i*>(ptr + i), zero);
    }
#else
    std::memset(m_vkDown.data(), 0, sizeof(m_vkDown));
#endif
}

void RawInputWinFilter::resetMouseButtons() noexcept
{
    std::memset(m_mb.data(), 0, sizeof(m_mb));
}

void RawInputWinFilter::setHotkeyVks(int hk, const std::vector<UINT>& vks)
{
    m_hkToVk[hk] = vks;
}

bool RawInputWinFilter::hotkeyDown(int hk) const noexcept
{
    // 1) �L�[�{�[�h�E�}�E�X��ԃ`�F�b�N�i�œK���j
    const auto it = m_hkToVk.find(hk);
    if (Q_LIKELY(it != m_hkToVk.end())) [[likely]] {
        const auto& virtualKeys = it->second;

        // �����ȃx�N�^�̏ꍇ�͓W�J���[�v���g�p
        for (const UINT vk : virtualKeys) {
            if (Q_UNLIKELY(vk <= VK_XBUTTON2)) [[unlikely]] {
                // �}�E�X�{�^���`�F�b�N�ilookup table���g�p�j
                static constexpr uint8_t mouseButtonMap[] = {
                    255, // VK_NULL
                    kMB_Left,   // VK_LBUTTON
                    kMB_Right,  // VK_RBUTTON
                    255,        // VK_CANCEL
                    kMB_Middle, // VK_MBUTTON
                    kMB_X1,     // VK_XBUTTON1
                    kMB_X2,     // VK_XBUTTON2
                };

                if (vk < sizeof(mouseButtonMap)) {
                    const uint8_t mbIndex = mouseButtonMap[vk];
                    if (mbIndex != 255 && m_mb[mbIndex].load(std::memory_order_relaxed)) {
                        return true;
                    }
                }
            }
            else if (Q_LIKELY(vk < m_vkDown.size())) [[likely]] {
                if (Q_UNLIKELY(m_vkDown[vk].load(std::memory_order_relaxed))) [[unlikely]] {
                    return true;
                }
            }
        }
    }

    // 2) �W���C�X�e�B�b�N��ԃ`�F�b�N
    const QBitArray* const joyMask = m_joyHK;
    const int maskSize = joyMask->size();

    if (Q_LIKELY(static_cast<unsigned>(hk) < static_cast<unsigned>(maskSize))) [[likely]] {
        return joyMask->testBit(hk);
    }

    return false;
}

bool RawInputWinFilter::hotkeyPressed(int hk) noexcept
{
    const bool currentState = hotkeyDown(hk);
    const size_t index = static_cast<size_t>(hk) & 511;

    // atomic exchange ���g���đO�̏�Ԃ��擾
    const uint8_t previousState = m_hkPrev[index].exchange(
        static_cast<uint8_t>(currentState),
        std::memory_order_acq_rel
    );

    // �����オ��G�b�W���o
    return currentState && !previousState;
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept
{
    const bool currentState = hotkeyDown(hk);
    const size_t index = static_cast<size_t>(hk) & 511;

    // atomic exchange ���g���đO�̏�Ԃ��擾
    const uint8_t previousState = m_hkPrev[index].exchange(
        static_cast<uint8_t>(currentState),
        std::memory_order_acq_rel
    );

    // ����������G�b�W���o
    return !currentState && previousState;
}