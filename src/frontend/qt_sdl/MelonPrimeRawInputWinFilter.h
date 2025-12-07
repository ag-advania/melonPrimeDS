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

    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override;

    void fetchMouseDelta(int& outDx, int& outDy);
    void discardDeltas();

    void resetAllKeys();
    void resetMouseButtons();
    void resetHotkeyEdges();

    void setHotkeyVks(int hk, const std::vector<UINT>& vks);

    bool hotkeyDown(int hk) const noexcept;
    bool hotkeyPressed(int hk) noexcept;
    bool hotkeyReleased(int hk) noexcept;


    struct BtnLutEntry { uint8_t down; uint8_t up; };
    static BtnLutEntry s_btnLut[1024] alignas(128);

    static constexpr uint8_t kMouseButtonLUT[8] = {
        0xFF, 0, 1, 0xFF,
        2, 3, 4, 0xFF
    };

private:

    //--------------------------------------------------
    // RawInput Thread
    //--------------------------------------------------
    HANDLE hThread = nullptr;
    HWND hiddenWnd = nullptr;
    std::atomic<bool> runThread{ false };

    static DWORD WINAPI ThreadFunc(LPVOID param);
    void threadLoop();

    static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);


    //--------------------------------------------------
    // 高速 dispatch
    //--------------------------------------------------
    static void onMouse_fast(RawInputWinFilter* self, RAWINPUT* raw);
    static void onKeyboard_fast(RawInputWinFilter* self, RAWINPUT* raw);

    using HandlerFn = void(*)(RawInputWinFilter*, RAWINPUT*);
    static constexpr HandlerFn handlerTbl[2] = {
        &onMouse_fast,
        &onKeyboard_fast
    };


    //--------------------------------------------------
    // RAWINPUT バッファ（※復活）
    //--------------------------------------------------
    alignas(128) BYTE m_rawBuf[128] = {};   // ★ これが無いと GetRawInputData が動かない


    //--------------------------------------------------
    // Key / Mouse 状態
    //--------------------------------------------------
    struct alignas(128) StateBits {
        std::array<std::atomic<uint64_t>, 4> vkDown;
        std::atomic<uint8_t> mouseButtons;
    } m_state{ {0,0,0,0}, 0 };


    FORCE_INLINE bool getVkState(uint32_t vk) const noexcept {
        if (vk >= 256) return false;
        return (m_state.vkDown[vk >> 6].load()
            >> (vk & 63)) & 1ULL;
    }

    FORCE_INLINE bool getMouseButton(uint8_t idx) const noexcept {
        uint8_t b = m_state.mouseButtons.load();
        return (idx < 5) && ((b >> idx) & 1);
    }


    //--------------------------------------------------
    // 【3】Mouse Deltas（atomic → volatile + Interlocked）
    //--------------------------------------------------
    volatile LONG dx = 0;
    volatile LONG dy = 0;


    //--------------------------------------------------
    // Hotkey state
    //--------------------------------------------------
    struct alignas(128) HotkeyMask {
        uint64_t vkMask[4];
        uint8_t  mouseMask;
        uint8_t  hasMask;
        uint8_t  _pad[6];
    };

    static constexpr size_t kMaxHotkeyId = 256;

    alignas(128) std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};
    std::unordered_map<int, std::vector<UINT>> m_hkToVk;
    alignas(128) std::array<std::atomic<uint8_t>, 1024> m_hkPrevAll{};


    //--------------------------------------------------
    // Qt fallback（元と同じ）
    //--------------------------------------------------
    std::array<std::atomic<int>, 256>     m_vkDownCompat{};
    std::array<std::atomic<int>, 5>       m_mbCompat{};


    //--------------------------------------------------
    // Hotkey helper
    //--------------------------------------------------
    static FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept {
        if (vk < 8) {
            static constexpr uint8_t tbl[8] = {
                255,0,1,255,2,3,4,255
            };
            uint8_t idx = tbl[vk];
            if (idx < 5) {
                m.mouseMask |= (1u << idx);
                m.hasMask = 1;
            }
        }
        else if (vk < 256) {
            m.vkMask[vk >> 6] |= (1ULL << (vk & 63));
            m.hasMask = 1;
        }
    }
};

#endif
