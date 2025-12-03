#pragma once
#ifdef _WIN32

#include <QAbstractNativeEventFilter>
#include <QByteArray>

#include <windows.h>
#include <hidsdi.h>

#include <array>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <cstdint>
#include <cstring>

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

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    void fetchMouseDelta(int& outDx, int& outDy);
    void discardDeltas();

    void resetAllKeys();
    void resetMouseButtons();
    void resetHotkeyEdges();

    void setHotkeyVks(int hk, const std::vector<UINT>& vks);

    bool hotkeyDown(int hk) const noexcept;
    bool hotkeyPressed(int hk) noexcept;
    bool hotkeyReleased(int hk) noexcept;

    //=========================================================
// ç≈êVî≈ÇÃ 1024 LUT
//=========================================================
    struct BtnEntry {
        uint8_t d;
        uint8_t u;
    };
    static BtnEntry s_btnLut[1024];

private:
    //=========================================================
    // RawInput Thread
    //=========================================================
    HANDLE hThread = nullptr;
    HWND hiddenWnd = nullptr;
    std::atomic<bool> runThread{ false };

    static DWORD WINAPI ThreadFunc(LPVOID param);
    void threadLoop();
    static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void joy2keyUpdateBoundKeys();

    //=========================================================
    // Internal State
    //=========================================================
    enum MouseIndex : uint8_t { kMB_Left = 0, kMB_Right, kMB_Middle, kMB_X1, kMB_X2, kMB_Max };

    struct alignas(16) HotkeyMask {
        uint64_t vkMask[4];
        uint8_t  mouseMask;
        uint8_t  hasMask;
        uint8_t  _pad[6];
    };

    static constexpr size_t kMaxHotkeyId = 256;

    alignas(64) BYTE m_rawBuf[256] = {};

    struct StateBits {
        std::atomic<uint64_t> vkDown[4];
        std::atomic<uint8_t>  mouseButtons;
    } m_state{ {0,0,0,0}, 0 };

    FORCE_INLINE bool getVkState(uint32_t vk) const noexcept {
        if (vk >= 256) return false;
        const uint64_t w = m_state.vkDown[vk >> 6].load(std::memory_order_relaxed);
        return (w >> (vk & 63)) & 1ULL;
    }

    FORCE_INLINE void setVkBit(uint32_t vk, bool down) noexcept {
        if (vk >= 256) return;
        const uint32_t widx = vk >> 6;
        const uint64_t bit = 1ULL << (vk & 63);
        uint64_t cur = m_state.vkDown[widx].load(std::memory_order_relaxed);
        uint64_t nxt = down ? (cur | bit) : (cur & bit ? cur & ~bit : cur & ~bit);
        m_state.vkDown[widx].store(nxt, std::memory_order_relaxed);
    }

    FORCE_INLINE bool getMouseButton(uint8_t idx) const noexcept {
        const uint8_t b = m_state.mouseButtons.load(std::memory_order_relaxed);
        return (idx < 5) && ((b >> idx) & 1u);
    }

    std::atomic<int> dx{ 0 };
    std::atomic<int> dy{ 0 };

    alignas(64) std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};
    std::unordered_map<int, std::vector<UINT>> m_hkToVk;

    std::array<std::atomic<uint64_t>, (kMaxHotkeyId + 63) / 64> m_hkPrev{};

    std::array<std::atomic<int>, 256> m_vkDownCompat{};
    std::array<std::atomic<int>, kMB_Max> m_mbCompat{};

    static constexpr USHORT kAllMouseBtnMask =
        RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP |
        RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP |
        RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP |
        RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP |
        RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP;

    static FORCE_INLINE uint8_t ConvertMouseButtonLUT(USHORT flagsDown) {
        switch (flagsDown & 0x1F) {
        case RI_MOUSE_LEFT_BUTTON_DOWN: return 1 << kMB_Left;
        case RI_MOUSE_RIGHT_BUTTON_DOWN: return 1 << kMB_Right;
        case RI_MOUSE_MIDDLE_BUTTON_DOWN: return 1 << kMB_Middle;
        case RI_MOUSE_BUTTON_4_DOWN: return 1 << kMB_X1;
        case RI_MOUSE_BUTTON_5_DOWN: return 1 << kMB_X2;
        default: return 0;
        }
    }

    static FORCE_INLINE uint8_t ConvertMouseButtonUpLUT(USHORT flagsUp) {
        switch (flagsUp & 0x1F) {
        case RI_MOUSE_LEFT_BUTTON_UP: return 1 << kMB_Left;
        case RI_MOUSE_RIGHT_BUTTON_UP: return 1 << kMB_Right;
        case RI_MOUSE_MIDDLE_BUTTON_UP: return 1 << kMB_Middle;
        case RI_MOUSE_BUTTON_4_UP: return 1 << kMB_X1;
        case RI_MOUSE_BUTTON_5_UP: return 1 << kMB_X2;
        default: return 0;
        }
    }

    static FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept;
};

#endif // _WIN32
