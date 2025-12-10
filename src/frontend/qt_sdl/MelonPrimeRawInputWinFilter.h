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
#include <intrin.h>

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

    //--------------------------------------------------
    // Qt fallback → 完全無効化
    //--------------------------------------------------
    bool nativeEventFilter(const QByteArray&, void*, qintptr*) override
    {
        return false;
    }

    //--------------------------------------------------
    // Public utilities (inline 本体をここに書く)
    //--------------------------------------------------
    FORCE_INLINE void fetchMouseDelta(int& outDx, int& outDy) noexcept
    {
        outDx = m_dx.exchange(0, std::memory_order_acq_rel);
        outDy = m_dy.exchange(0, std::memory_order_acq_rel);
    }

    FORCE_INLINE void discardDeltas() noexcept
    {
        m_dx.store(0, std::memory_order_relaxed);
        m_dy.store(0, std::memory_order_relaxed);
    }

    void resetAllKeys();
    void resetMouseButtons();
    void resetHotkeyEdges();

    //--------------------------------------------------
    // Hotkeys
    //--------------------------------------------------
    static constexpr size_t kMaxHotkeyId = 256;

    bool hotkeyDown(int hk) const noexcept;
    bool hotkeyPressed(int hk) noexcept;
    bool hotkeyReleased(int hk) noexcept;

    void setHotkeyVks(int hk, const std::vector<UINT>& vks);


private:
    //--------------------------------------------------
    // RawInput thread
    //--------------------------------------------------
    HANDLE hThread = nullptr;
    HWND hiddenWnd = nullptr;
    std::atomic<bool> runThread{ false };

    static DWORD WINAPI ThreadFunc(LPVOID);
    void threadLoop();
    static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    //--------------------------------------------------
    // RawInput dispatch
    //--------------------------------------------------
    static void onMouse_fast(RawInputWinFilter* self, RAWINPUT* raw);
    static void onKeyboard_fast(RawInputWinFilter* self, RAWINPUT* raw);

    //--------------------------------------------------
    // Joy2Key 補正（bitmap + BSF）
    //--------------------------------------------------
    void syncWithSendInput();

    //--------------------------------------------------
    // RawInput 一時バッファ（最大80bytes）
    //--------------------------------------------------
    alignas(128) BYTE m_rawBuf[128]{};

    //--------------------------------------------------
    // Keyboard / Mouse raw-bit 状態
    //--------------------------------------------------
    struct alignas(128) StateBits
    {
        std::array<std::atomic<uint64_t>, 4> vkDown;  // 256bit
        std::atomic<uint8_t> mouseButtons;
    } m_state{ {0,0,0,0}, 0 };

    //--------------------------------------------------
    // Keyboard helpers (inline)
    //--------------------------------------------------
    FORCE_INLINE bool getVkState(UINT vk) const noexcept
    {
        if (vk >= 256) return false;
        uint64_t bit = (1ULL << (vk & 63));
        return (m_state.vkDown[vk >> 6].load(std::memory_order_relaxed) & bit) != 0;
    }

    FORCE_INLINE void setVkBit(UINT vk, bool down) noexcept
    {
        if (vk >= 256) return;

        uint32_t w = vk >> 6;
        uint64_t bit = (1ULL << (vk & 63));

        uint64_t cur = m_state.vkDown[w].load(std::memory_order_relaxed);
        uint64_t nxt = down ? (cur | bit) : (cur & ~bit);

        m_state.vkDown[w].store(nxt, std::memory_order_relaxed);
    }

    //--------------------------------------------------
    // Mouse bit helpers
    //--------------------------------------------------
    enum MouseIndex : uint8_t {
        kMB_Left = 0,
        kMB_Right = 1,
        kMB_Middle = 2,
        kMB_X1 = 3,
        kMB_X2 = 4,
    };

    FORCE_INLINE bool getMouseButton(uint8_t idx) const noexcept
    {
        return (m_state.mouseButtons.load(std::memory_order_relaxed) & (1u << idx)) != 0;
    }

    //--------------------------------------------------
    // Mouse delta（最速）
    //--------------------------------------------------
    std::atomic<int> m_dx{ 0 };
    std::atomic<int> m_dy{ 0 };

    FORCE_INLINE void addDelta(LONG dx, LONG dy) noexcept
    {
        m_dx.fetch_add(dx, std::memory_order_relaxed);
        m_dy.fetch_add(dy, std::memory_order_relaxed);
    }


    //--------------------------------------------------
    // Hotkey mask（最速）
    //--------------------------------------------------
    struct alignas(128) HotkeyMask
    {
        uint64_t vkMask[4];   // 256bit
        uint8_t  mouseMask;   // 5bit
        uint8_t  hasMask;
        uint8_t  _pad[6];
    };

    alignas(128) std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};

    //--------------------------------------------------
    // Hotkey fallback (旧互換)
    //--------------------------------------------------
    std::unordered_map<int, std::vector<UINT>> m_hkToVk;

    //--------------------------------------------------
    // Hotkey edge
    //--------------------------------------------------
    alignas(128) std::array<std::atomic<uint8_t>, 1024> m_hkPrevAll{};


    //--------------------------------------------------
    // Joy2Key 補正：使用されるVK（BSF用）
    //--------------------------------------------------
    std::array<uint8_t, 256> m_usedVkTable{};
    alignas(32) uint64_t m_usedVkMask[4]{};


    //--------------------------------------------------
    // bitmap keyboard state
    //--------------------------------------------------
    alignas(64) std::array<uint8_t, 256> m_keyBitmap{};


    //--------------------------------------------------
    // Qt fallback互換（古いインターフェース向け）
    //--------------------------------------------------
    std::array<std::atomic<int>, 256> m_vkDownCompat{};
    std::array<std::atomic<int>, 5>   m_mbCompat{};


    //--------------------------------------------------
    // Hotkey helper
    //--------------------------------------------------
    static FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept
    {
        if (vk < 8)
        {
            static constexpr uint8_t tbl[8] = {
                255,0,1,255,2,3,4,255
            };
            uint8_t idx = tbl[vk];
            if (idx < 5)
            {
                m.mouseMask |= (1u << idx);
                m.hasMask = 1;
            }
        }
        else if (vk < 256)
        {
            m.vkMask[vk >> 6] |= (1ULL << (vk & 63));
            m.hasMask = 1;
        }
    }
};

#endif // _WIN32
