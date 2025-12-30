// #ifndef MPFILTER_H はぜったい維持！ pragma once はあてにならないまじで。

#ifndef MPFILTER_H
#define MPFILTER_H

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif

// Qt
#include <QAbstractNativeEventFilter>
#include <QByteArray>

// Win32
#include <windows.h>

// C++/STL
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

// FORCE_INLINE
#ifndef FORCE_INLINE
#  if defined(_MSC_VER)
#    define FORCE_INLINE __forceinline
#  else
#    define FORCE_INLINE __attribute__((always_inline)) inline
#  endif
#endif

class RawInputWinFilter final : public QAbstractNativeEventFilter
{
public:
    //=====================================================
    // ★重要（あなたの定義どおり）
    // joy2keySupport=true  : スレッド無し（Qt WM_INPUT）＝Joy2Key互換維持
    // joy2keySupport=false : HiddenWnd + 内蔵スレッド（RawInputをhiddenWndへ）＝低遅延
    //
    // panel/mainWindow が既に存在する前提で、生成時に HWND を渡せる：
    //   new RawInputWinFilter(joy2keySupport, (HWND)mainWindow->winId());
    //=====================================================
    explicit RawInputWinFilter(bool joy2keySupport = true, HWND mainHwnd = nullptr);
    ~RawInputWinFilter() override;

    void setJoy2KeySupport(bool enabled) noexcept;
    bool getJoy2KeySupport() const noexcept { return m_joy2keySupport.load(std::memory_order_relaxed); }

    // true側低遅延化：配送先を固定（mainWindow->winId() 等）
    // ※ false側（スレッド動作中）は hiddenWnd がターゲットになるので、呼んでも安全に無視する
    void setRawInputTarget(HWND hwnd) noexcept;

    // Qt から来る WM_INPUT（常に false を返す）
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    //=====================================================
    // 入力状態
    //=====================================================
    void fetchMouseDelta(int& outDx, int& outDy) noexcept;
    void discardDeltas() noexcept;

    void resetAllKeys() noexcept;
    void resetMouseButtons() noexcept;
    void resetHotkeyEdges() noexcept;

    //=====================================================
    // Hotkey
    //=====================================================
    void setHotkeyVks(int hk, const std::vector<UINT>& vks);
    bool hotkeyDown(int hk) const noexcept;
    bool hotkeyPressed(int hk) noexcept;
    bool hotkeyReleased(int hk) noexcept;

public:
    //=====================================================
    // ★ マウスボタン LUT（1024）
    //=====================================================
    struct BtnLutEntry { uint8_t down; uint8_t up; };
    static BtnLutEntry s_btnLut[1024];

private:
    //=====================================================
    // ★ RawInputWinFilter 内蔵スレッド（joy2keySupport=false用）
    //=====================================================
    static DWORD WINAPI ThreadFunc(LPVOID param);
    void threadLoop();
    static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void startThreadIfNeeded() noexcept;
    void stopThreadIfRunning() noexcept;

    // RawInput 登録を切り替える
    void registerRawToTarget(HWND target, bool inputSink) noexcept;

private:
    //=====================================================
    // データ
    //=====================================================
    enum MouseIndex : uint8_t { kMB_Left = 0, kMB_Right, kMB_Middle, kMB_X1, kMB_X2, kMB_Max };

    static constexpr USHORT kAllMouseBtnMask =
        RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP |
        RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP |
        RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP |
        RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP |
        RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP;

    static constexpr uint8_t kMouseButtonLUT[8] = {
        0xFF,
        kMB_Left,
        kMB_Right,
        0xFF,
        kMB_Middle,
        kMB_X1,
        kMB_X2,
        0xFF
    };

    // RawInputData buffer（固定バッファ）
    alignas(64) BYTE m_rawBuf[256] = {};

    // state
    struct StateBits {
        std::atomic<uint64_t> vkDown[4];
        std::atomic<uint8_t>  mouseButtons;
    } m_state{ {0,0,0,0}, 0 };

    std::atomic<int> dx{ 0 };
    std::atomic<int> dy{ 0 };

    std::array<std::atomic<uint8_t>, 256>     m_vkDownCompat{};
    std::array<std::atomic<uint8_t>, kMB_Max> m_mbCompat{};

    //=====================================================
    // Hotkey masks
    //=====================================================
    struct alignas(16) HotkeyMask {
        uint64_t vkMask[4];
        uint8_t  mouseMask;
        uint8_t  hasMask;
        uint8_t  _pad[6];
    };

    static constexpr uint32_t kMaxHotkeyId = 256;

    // hk<256 : fixed table
    alignas(64) std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};

    // hk>=256 : snapshot dyn table（unordered_map 排除）
    struct HotkeyDynTable {
        uint32_t count;
        HotkeyMask masks[1];
    };
    std::atomic<HotkeyDynTable*> m_hkDyn{ nullptr };
    std::vector<HotkeyDynTable*> m_hkDynGarbage;

    // edge detect
    alignas(64) std::array<std::atomic<uint64_t>, (kMaxHotkeyId + 63) / 64> m_hkPrev{};
    alignas(64) std::array<std::atomic<uint8_t>, 1024> m_hkPrevAll{};

    //=====================================================
    // Thread / Target
    //=====================================================
    std::atomic<bool> m_joy2keySupport{ true };

    std::atomic<bool> runThread{ false };
    HANDLE hThread{ nullptr };
    HWND   hiddenWnd{ nullptr };

    HWND   m_targetHwnd{ nullptr };

private:
    //=====================================================
    // helpers
    //=====================================================
    FORCE_INLINE void setVkBit(UINT vk, bool down) noexcept
    {
        if (vk >= 256) return;
        const uint32_t w = (uint32_t)vk >> 6;
        const uint64_t bit = 1ULL << ((uint32_t)vk & 63);

        if (down) m_state.vkDown[w].fetch_or(bit, std::memory_order_relaxed);
        else      m_state.vkDown[w].fetch_and(~bit, std::memory_order_relaxed);
    }

    FORCE_INLINE bool getVkBit(UINT vk) const noexcept
    {
        if (vk >= 256) return false;
        const uint32_t w = (uint32_t)vk >> 6;
        const uint64_t bit = 1ULL << ((uint32_t)vk & 63);
        return (m_state.vkDown[w].load(std::memory_order_relaxed) & bit) != 0ULL;
    }

    FORCE_INLINE bool getMouseButton(uint8_t idx) const noexcept
    {
        const uint8_t b = m_state.mouseButtons.load(std::memory_order_relaxed);
        return (idx < 5) && ((b >> idx) & 1u);
    }

    static FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept
    {
        if (vk < 8) {
            const uint8_t b = kMouseButtonLUT[vk];
            if (b < 5) { m.mouseMask |= (1u << b); m.hasMask = 1; }
        }
        else if (vk < 256) {
            m.vkMask[vk >> 6] |= (1ULL << (vk & 63));
            m.hasMask = 1;
        }
    }

    void setHotkeyDynMask(uint32_t idx, const HotkeyMask& nm);
    FORCE_INLINE const HotkeyMask* getDynMask(uint32_t idx) const noexcept
    {
        HotkeyDynTable* t = m_hkDyn.load(std::memory_order_acquire);
        if (!t) return nullptr;
        if (idx >= t->count) return nullptr;
        return &t->masks[idx];
    }

    // raw handlers
    void processRawInputHandle(HRAWINPUT hRaw) noexcept;
    FORCE_INLINE void processRawMouse(const RAWMOUSE& m) noexcept;
    FORCE_INLINE void processRawKeyboard(const RAWKEYBOARD& kb) noexcept;
};

#endif // _WIN32

#endif // MPFILTER_H
