#pragma once
#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QByteArray>
#include <QtCore/QtGlobal>
#include <atomic>
#include <array>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <QBitArray>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#endif

class RawInputWinFilter final : public QAbstractNativeEventFilter
{
public:
    RawInputWinFilter();
    ~RawInputWinFilter() override;

    inline void setJoyHotkeyMaskPtr(const QBitArray* p) noexcept {
        m_joyHK = p ? p : &kEmptyMask;
    }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // �C�����C�����ŃR�[�����Ԃ��팸
    [[gnu::hot]] inline void fetchMouseDelta(int& outDx, int& outDy) noexcept {
        outDx = dx.exchange(0, std::memory_order_relaxed);
        outDy = dy.exchange(0, std::memory_order_relaxed);
    }

    [[gnu::hot]] inline void discardDeltas() noexcept {
        dx.store(0, std::memory_order_relaxed);
        dy.store(0, std::memory_order_relaxed);
    }

    void resetAllKeys() noexcept;
    void resetMouseButtons() noexcept;

    void setHotkeyVks(int hk, const std::vector<UINT>& vks);

    // �ł��p�ɂɌĂ΂��֐����œK��
    [[gnu::hot]] bool hotkeyDown(int hk) const noexcept;
    [[gnu::hot]] bool hotkeyPressed(int hk) noexcept;
    [[gnu::hot]] bool hotkeyReleased(int hk) noexcept;

    inline void resetHotkeyEdges() noexcept {
        // �������œK��: 0���߂���荂����
        std::memset(m_hkPrev.data(), 0, sizeof(m_hkPrev));
    }

private:
#ifdef _WIN32
    RAWINPUTDEVICE rid[2]{};
    const QBitArray* m_joyHK = nullptr;
    inline static const QBitArray kEmptyMask{};

    // �L���b�V�����C�����E�ɐ���i64�o�C�g�j
    alignas(64) BYTE m_rawBuf[sizeof(RAWINPUT) + 64];

    // �r�b�g�}�X�N�œK��: constexpr�Ōv�Z���Ԃ��팸
    static constexpr USHORT kAllMouseBtnMask =
        RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP |
        RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP |
        RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP |
        RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP |
        RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP;

    // �}�b�s���O�\���̂�constexpr�ōœK���i�A�O���Q�[�g���������g�p�j
    struct ButtonMap {
        USHORT down;
        USHORT up;
        uint8_t idx;
    };

    static constexpr ButtonMap kButtonMaps[5] = {
        {RI_MOUSE_LEFT_BUTTON_DOWN,   RI_MOUSE_LEFT_BUTTON_UP,   0},
        {RI_MOUSE_RIGHT_BUTTON_DOWN,  RI_MOUSE_RIGHT_BUTTON_UP,  1},
        {RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, 2},
        {RI_MOUSE_BUTTON_4_DOWN,      RI_MOUSE_BUTTON_4_UP,      3},
        {RI_MOUSE_BUTTON_5_DOWN,      RI_MOUSE_BUTTON_5_UP,      4},
    };
#endif

    // �L���b�V�����C��������false sharing�����
    alignas(64) std::atomic<int> dx{ 0 };
    alignas(64) std::atomic<int> dy{ 0 };

    static constexpr size_t kMouseBtnCount = 5;
    enum : uint8_t { kMB_Left = 0, kMB_Right = 1, kMB_Middle = 2, kMB_X1 = 3, kMB_X2 = 4 };

    // ���������C�A�E�g�œK��
    alignas(64) std::array<std::atomic<uint8_t>, 256> m_vkDown{};
    alignas(64) std::array<std::atomic<uint8_t>, kMouseBtnCount> m_mb{};
    alignas(64) std::array<std::atomic<uint8_t>, 512> m_hkPrev{};

    // �z�b�g�p�X�p��flat_map���g�p�i�����������j
    std::unordered_map<int, std::vector<UINT>> m_hkToVk;
};