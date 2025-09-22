#pragma once
#ifdef _WIN32

// Qt
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QBitArray>
#include <QtGlobal>

// Win32
#include <windows.h>
#include <hidsdi.h>

// C++/STL
#include <array>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <cstdint>
#include <cstring>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

// �����C�����C���q���g
#ifndef FORCE_INLINE
#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE __attribute__((always_inline)) inline
#endif
#endif

// SSE/AVX ���g��Ȃ��r���h�ł����S�ɓ��삷��悤�ɏ�������
// �iAVX2������ꍇ�̂݃X�g���[���X�g�A�����g�p�j

class RawInputWinFilter : public QAbstractNativeEventFilter
{
public:
    RawInputWinFilter();
    ~RawInputWinFilter() override;

    // Qt �l�C�e�B�u�C�x���g�t�b�N
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // �}�E�X���΃f���^�擾�i���o�����Ƀ[���N���A�j
    void fetchMouseDelta(int& outDx, int& outDy);

    // �ݐσf���^�̔j��
    void discardDeltas();

    // �S�L�[�i�L�[�{�[�h�j�𖢉�����
    void resetAllKeys();

    // �S�}�E�X�{�^���𖢉�����
    void resetMouseButtons();

    // �S�z�b�g�L�[�̗�����/�����G�b�W��Ԃ����Z�b�g
    void resetHotkeyEdges();

    // HK��VK �̓o�^�i�L�[�{�[�h/�}�E�X���̑O�v�Z�}�X�N�����j
    void setHotkeyVks(int hk, const std::vector<UINT>& vks);

    // SDL ���iEmuInstance�j�����t���X�V���� joyHotkeyMask ���Q�Ƃ�����
    // �i��ɗL����n���O��Ȃ�null�`�F�b�N�s�v�j
    //FORCE_INLINE void setJoyHotkeyMaskPtr(const QBitArray* p) noexcept { m_joyHK = p ? p : &kEmptyMask; }

    // �擾�n
    bool hotkeyDown(int hk) const noexcept;
    bool hotkeyPressed(int hk) noexcept;
    bool hotkeyReleased(int hk) noexcept;

private:
    // --------- �����^�ƒ萔 ---------
    enum MouseIndex : uint8_t { kMB_Left = 0, kMB_Right, kMB_Middle, kMB_X1, kMB_X2, kMB_Max };

    // VK(0..255) �p 256bit �ƁAMouse(5bit) �̑O�v�Z�}�X�N
    struct alignas(16) HotkeyMask {
        uint64_t vkMask[4];  // 256bit = 4 * 64bit
        uint8_t  mouseMask;  // L/R/M/X1/X2 -> 5bit
        uint8_t  hasMask;    // 0:��, 1:�L��
        uint8_t  _pad[6];
    };

    static constexpr size_t kMaxHotkeyId = 256;

    // ���ׂẴ{�^���t���O�i�������^�[���p�j
    static constexpr USHORT kAllMouseBtnMask =
        RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP |
        RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP |
        RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP |
        RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP |
        RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP;

    // Win32 VK_* �� MouseIndex �̊Ȉ�LUT�i0..7�����g�p�j
    static constexpr uint8_t kMouseButtonLUT[8] = {
        0xFF,       // 0 (unused)
        kMB_Left,   // VK_LBUTTON   (1)
        kMB_Right,  // VK_RBUTTON   (2)
        0xFF,       // 3
        kMB_Middle, // VK_MBUTTON   (4)
        kMB_X1,     // VK_XBUTTON1  (5)
        kMB_X2,     // VK_XBUTTON2  (6)
        0xFF        // 7
    };

    // �C�x���g�����p�̏��o�b�t�@�iHID�͈���Ȃ��̂ŏ\���j
    alignas(64) BYTE m_rawBuf[256] = {};

    // �L�[/�}�E�X�����ԏ�ԁi��R�X�g�Q�Ƃ̂��߃r�b�g�x�N�^���j
    struct StateBits {
        std::atomic<uint64_t> vkDown[4];      // 256bit �̉������
        std::atomic<uint8_t>  mouseButtons;   // 5bit �̉�����ԁiL,R,M,X1,X2�j
    } m_state{ {0,0,0,0}, 0 };

    // ��p�xAPI: VK�̌��ݏ�Ԏ擾
    FORCE_INLINE bool getVkState(uint32_t vk) const noexcept {
        if (vk >= 256) return false;
        const uint64_t w = m_state.vkDown[vk >> 6].load(std::memory_order_relaxed);
        return (w & (1ULL << (vk & 63))) != 0ULL;
    }
    // ��p�xAPI: Mouse�{�^�����
    FORCE_INLINE bool getMouseButton(uint8_t idx) const noexcept {
        const uint8_t b = m_state.mouseButtons.load(std::memory_order_relaxed);
        return (idx < 5) && ((b >> idx) & 1u);
    }
    // ��p�xAPI: VK�Z�b�g/�N���A
    FORCE_INLINE void setVkBit(uint32_t vk, bool down) noexcept {
        if (vk >= 256) return;
        const uint64_t mask = 1ULL << (vk & 63);
        auto& word = m_state.vkDown[vk >> 6];
        if (down) (void)word.fetch_or(mask, std::memory_order_relaxed);
        else      (void)word.fetch_and(~mask, std::memory_order_relaxed);
    }

    // �O�v�Z�ς� Hotkey �}�X�N�i�����p�X�p�j
    alignas(64) std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};

    // �t�H�[���o�b�N�p�i�傫��HK ID�Ȃǁj
    std::unordered_map<int, std::vector<UINT>> m_hkToVk;

    // joyHotkeyMask �Q��
    //inline static const QBitArray kEmptyMask{};
    //const QBitArray* m_joyHK = &kEmptyMask;

    // �����/������G�b�W���o�p�iO(1)�j
    std::array<std::atomic<uint64_t>, (kMaxHotkeyId + 63) / 64> m_hkPrev{};

    // RawInput �o�^���
    RAWINPUTDEVICE m_rid[2]{};

    // ���΃f���^
    std::atomic<int> dx{ 0 }, dy{ 0 };

    // �Â������Ƃ̌݊��i�K�v�Ȃ�c���j
    std::array<std::atomic<int>, 256> m_vkDownCompat{};
    std::array<std::atomic<int>, kMB_Max> m_mbCompat{};

    // �w���p
    static FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept;
};

#endif // _WIN32
