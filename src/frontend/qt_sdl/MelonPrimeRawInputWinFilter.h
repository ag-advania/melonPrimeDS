// #ifndef MPFILTER_H はぜったい維持！ pragma once はあてにならないまじで。

#ifndef MPFILTER_H
#define MPFILTER_H

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

// FORCE_INLINE
#ifndef FORCE_INLINE
#  if defined(_MSC_VER)
#    define FORCE_INLINE __forceinline
#  else
#    define FORCE_INLINE __attribute__((always_inline)) inline
#  endif
#endif

class RawInputWinFilter : public QAbstractNativeEventFilter
{
public:
    RawInputWinFilter();
    ~RawInputWinFilter() override;

    // WM_INPUT �� Qt ���Ŏ󂯎��i��� false�j
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // ���΃f���^�擾�i���o�����Ƀ[���N���A�j
    void fetchMouseDelta(int& outDx, int& outDy);

    // ���΃f���^����
    void discardDeltas();

    // �S�L�[��ԃ��Z�b�g
    void resetAllKeys();

    // �S�}�E�X�{�^����ԃ��Z�b�g
    void resetMouseButtons();

    // �z�b�g�L�[�G�b�W���Z�b�g
    void resetHotkeyEdges();

    // �z�b�g�L�[�̃L�[�o�^�i���O�v�Z�}�X�N���\�z�j
    void setHotkeyVks(int hk, const std::vector<UINT>& vks);

    // �z�b�g�L�[��Ԏ擾
    bool hotkeyDown(int hk) const noexcept;
    bool hotkeyPressed(int hk) noexcept;
    bool hotkeyReleased(int hk) noexcept;

private:
    // �}�E�X�{�^�� index
    enum MouseIndex : uint8_t { kMB_Left = 0, kMB_Right, kMB_Middle, kMB_X1, kMB_X2, kMB_Max };

    // �O�v�Z�}�X�N
    struct alignas(16) HotkeyMask {
        uint64_t vkMask[4];  // 256bit
        uint8_t  mouseMask;  // L/R/M/X1/X2 (5bit)
        uint8_t  hasMask;
        uint8_t  _pad[6];
    };

    static constexpr size_t kMaxHotkeyId = 256;

    // �}�E�X�t���O�܂Ƃ߁i���� return �p�j
    static constexpr USHORT kAllMouseBtnMask =
        RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP |
        RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP |
        RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP |
        RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP |
        RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP;

    // VK �� MouseIndex �e�[�u���i0..7 �̂݁j
    static constexpr uint8_t kMouseButtonLUT[8] = {
        0xFF,       // 0 unused
        kMB_Left,   // 1 VK_LBUTTON
        kMB_Right,  // 2 VK_RBUTTON
        0xFF,       // 3
        kMB_Middle, // 4 VK_MBUTTON
        kMB_X1,     // 5 VK_XBUTTON1
        kMB_X2,     // 6 VK_XBUTTON2
        0xFF        // 7
    };

    // �Œ蒷 RawInput �o�b�t�@
    alignas(64) BYTE m_rawBuf[256] = {};

    // �����ԃL�[/�}�E�X���
    struct StateBits {
        std::atomic<uint64_t> vkDown[4];   // 256 bit
        std::atomic<uint8_t>  mouseButtons;
    } m_state{ {0,0,0,0}, 0 };

    // VK�������
    FORCE_INLINE bool getVkState(uint32_t vk) const noexcept {
        if (vk >= 256) return false;
        const uint64_t w = m_state.vkDown[vk >> 6].load(std::memory_order_relaxed);
        return (w & (1ULL << (vk & 63))) != 0ULL;
    }

    // Mouse�������
    FORCE_INLINE bool getMouseButton(uint8_t idx) const noexcept {
        const uint8_t b = m_state.mouseButtons.load(std::memory_order_relaxed);
        return (idx < 5) && ((b >> idx) & 1u);
    }

    // VK�Z�b�g/�N���A
    FORCE_INLINE void setVkBit(uint32_t vk, bool down) noexcept {
        if (vk >= 256) return;
        const uint64_t mask = 1ULL << (vk & 63);
        auto& word = m_state.vkDown[vk >> 6];
        if (down) (void)word.fetch_or(mask, std::memory_order_relaxed);
        else      (void)word.fetch_and(~mask, std::memory_order_relaxed);
    }

    // �O�v�Z�}�X�N
    alignas(64) std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};

    // �t�H�[���o�b�N�p
    std::unordered_map<int, std::vector<UINT>> m_hkToVk;

    // hk>=kMaxHotkeyId でも mask で判定できるようにする（低サイクル）
    std::unordered_map<int, HotkeyMask> m_hkMaskDyn;

    // �G�b�W���o

    // エッジ検出（hk全域：staticを消す／分岐を消す）
    alignas(64) std::array<std::atomic<uint32_t>, 1024> m_hkPrevAll{};

    // RawInput�o�^
    RAWINPUTDEVICE m_rid[2]{};

    // ���΃f���^�i���Ȃ��̊�]�ŕ����ł̂܂܁j
    std::atomic<int> dx{ 0 };
    std::atomic<int> dy{ 0 };

    // �݊�
    std::array<std::atomic<int>, 256> m_vkDownCompat{};
    std::array<std::atomic<int>, kMB_Max> m_mbCompat{};

    // �}�X�N�\�z
    static FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept;
};

#endif // _WIN32

#endif // MPSETTINGSDIALOG_H
