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

//------------------------------------------
// FORCE_INLINE
//------------------------------------------
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

    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override;

    //=========================================
    // マウス入力（リングバッファ）
    //=========================================
    void fetchMouseDelta(int& outDx, int& outDy);
    void discardDeltas();

    //=========================================
    // Reset
    //=========================================
    void resetAllKeys();
    void resetMouseButtons();
    void resetHotkeyEdges();

    //=========================================
    // Hotkey
    //=========================================
    void setHotkeyVks(int hk, const std::vector<UINT>& vks);
    bool hotkeyDown(int hk) const noexcept;
    bool hotkeyPressed(int hk) noexcept;
    bool hotkeyReleased(int hk) noexcept;

    //-----------------------------------------
    // Mouse button LUT
    //-----------------------------------------
    struct BtnLutEntry { uint8_t d; uint8_t u; };
    static BtnLutEntry s_btnLut[1024] alignas(128);

    static constexpr uint8_t kMouseButtonLUT[8] = {
        0xFF, 0, 1, 0xFF,
        2, 3, 4, 0xFF
    };

private:

    //=========================================
    // RawInput Thread
    //=========================================
    HANDLE hThread = nullptr;
    HWND   hiddenWnd = nullptr;
    std::atomic<bool> runThread{ false };

    static DWORD WINAPI ThreadFunc(LPVOID param);
    void threadLoop();
    static LRESULT CALLBACK HiddenWndProc(HWND, UINT, WPARAM, LPARAM);

    //=========================================
    // RawInput handler dispatch
    //=========================================
    static void onMouse_fast(RawInputWinFilter*, RAWINPUT*);
    static void onKeyboard_fast(RawInputWinFilter*, RAWINPUT*);

    using HandlerFn = void(*)(RawInputWinFilter*, RAWINPUT*);
    static constexpr HandlerFn handlerTbl[2] = {
        &onMouse_fast,
        &onKeyboard_fast
    };

    //=========================================
    // RawInput buffer
    //=========================================
    alignas(128) BYTE m_rawBuf[128] = {};

    //=========================================
    // Key / Mouse 状態
    //=========================================
    struct alignas(128) StateBits {
        std::array<std::atomic<uint64_t>, 4> vk;
        std::atomic<uint8_t> mouse;
    } m_state{ {0,0,0,0},0 };

    FORCE_INLINE bool getVk(uint32_t vk) const noexcept {
        return (vk < 256) &&
            ((m_state.vk[vk >> 6].load(std::memory_order_relaxed) >> (vk & 63)) & 1ULL);
    }

    FORCE_INLINE bool getMouse(uint8_t idx) const noexcept {
        uint8_t b = m_state.mouse.load(std::memory_order_relaxed);
        return (idx < 5) && ((b >> idx) & 1);
    }

    //=========================================
    //---- ★ dx/dy リングバッファ（64要素）★
    //=========================================
    static constexpr uint32_t MOUSEBUF_MASK = 63; // 64 entries

    alignas(128) std::array<int32_t, 64> m_dxBuf{};
    alignas(128) std::array<int32_t, 64> m_dyBuf{};

    std::atomic<uint32_t> m_head{ 0 };
    std::atomic<uint32_t> m_tail{ 0 };

    FORCE_INLINE void pushDelta(int32_t x, int32_t y) noexcept
    {
        uint32_t h = m_head.load(std::memory_order_relaxed);

        m_dxBuf[h] = x;
        m_dyBuf[h] = y;

        uint32_t next = (h + 1) & MOUSEBUF_MASK;
        m_head.store(next, std::memory_order_relaxed);

        // オーバーフロー時は tail を一つ進めて追いつかせる
        uint32_t t = m_tail.load(std::memory_order_relaxed);
        if (next == t) {
            m_tail.store((t + 1) & MOUSEBUF_MASK, std::memory_order_relaxed);
        }
    }

    //=========================================
    // Hotkey Mask
    //=========================================
    struct alignas(128) HotkeyMask {
        uint64_t vkMask[4];
        uint8_t  mouseMask;
        uint8_t  hasMask;
        uint8_t  pad[6];
    };

    static constexpr size_t kMaxHotkeyId = 256;

    alignas(128) std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};
    std::unordered_map<int, std::vector<UINT>> m_hkFallback;
    alignas(128) std::array<std::atomic<uint8_t>, 1024> m_hkPrev{};

    //=========================================
    // Qt fallback
    //=========================================
    std::array<std::atomic<int>, 256> m_vkCompat{};
    std::array<std::atomic<int>, 5>   m_mbCompat{};

    //=========================================
    // Mask 追加 helper
    //=========================================
    static FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept
    {
        if (vk < 8) {
            static constexpr uint8_t tbl[8] = { 255,0,1,255,2,3,4,255 };
            uint8_t idx = tbl[vk];
            if (idx < 5) {
                m.mouseMask |= (1u << idx);
                m.hasMask = 1;
            }
            return;
        }

        if (vk < 256) {
            m.vkMask[vk >> 6] |= (1ULL << (vk & 63));
            m.hasMask = 1;
        }
    }
};

#endif // _WIN32
