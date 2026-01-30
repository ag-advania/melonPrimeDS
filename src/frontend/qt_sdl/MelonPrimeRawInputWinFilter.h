#ifndef MELON_PRIME_RAW_INPUT_FILTER_H
#define MELON_PRIME_RAW_INPUT_FILTER_H
#ifdef _WIN32

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QBitArray>
#include <QtGlobal>
#include <windows.h>
#include <hidsdi.h>
#include <array>
#include <vector>
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
    RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd);
    ~RawInputWinFilter() override;

    // Qt native filter (Joy2KeySupport ON のときだけ使用)
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // モード切替（スレッド起動/停止を伴う）
    void setJoy2KeySupport(bool enable);
    bool getJoy2KeySupport() const noexcept { return m_joy2KeySupport.load(std::memory_order_relaxed); }

    // API
    static constexpr size_t kMaxHotkeyId = 128;
    void clearAllBindings();
    void setHotkeyVks(int id, const std::vector<UINT>& vks);

    bool hotkeyDown(int id) const noexcept;
    bool hotkeyPressed(int id) noexcept;
    bool hotkeyReleased(int id) noexcept;

    void fetchMouseDelta(int& outX, int& outY);
    void discardDeltas();
    void resetAllKeys();
    void resetMouseButtons();
    void resetHotkeyEdges();
    void setRawInputTarget(HWND hwnd);

    // Helpers (static初期化のためにpublic)
    struct BtnLutEntry { uint8_t downBits; uint8_t upBits; };
    static BtnLutEntry s_btnLut[1024];

private:
    std::atomic<bool> m_joy2KeySupport{ false };
    HWND m_mainHwnd = nullptr;

    // --- 低遅延モード用の独自スレッド ---
    std::atomic<bool> m_runThread{ false };
    HANDLE m_hThread = nullptr;
    HWND m_hiddenWnd = nullptr;

    static DWORD WINAPI ThreadFunc(LPVOID param);
    void threadLoop();
    void startThreadIfNeeded();
    void stopThreadIfRunning();

    // Core Logic
    void registerRawToTarget(HWND targetHwnd);

    // データ処理コア (インライン展開でオーバーヘッド除去)
    FORCE_INLINE void processRawInput(HRAWINPUT hRaw) noexcept;

    // State (alignasでキャッシュ競合防止)
    struct alignas(64) StateBits {
        std::atomic<uint64_t> vkDown[4];
        std::atomic<uint8_t>  mouseButtons;
    } m_state;

    std::atomic<int> m_mouseDeltaX{ 0 };
    std::atomic<int> m_mouseDeltaY{ 0 };

    struct HotkeyMask {
        uint64_t vkMask[4];
        uint8_t  mouseMask;
        bool     hasMask;
    };
    std::array<HotkeyMask, kMaxHotkeyId> m_hkMask;
    uint64_t m_hkPrev[kMaxHotkeyId / 64 + 1];

    // Inline Accessors
    FORCE_INLINE void setVkBit(uint32_t vk, bool down) noexcept {
        if (vk >= 256) return;
        const uint32_t widx = vk >> 6;
        const uint64_t bit = 1ULL << (vk & 63);
        uint64_t cur = m_state.vkDown[widx].load(std::memory_order_relaxed);
        uint64_t nxt = down ? (cur | bit) : (cur & ~bit);
        if (cur != nxt) m_state.vkDown[widx].store(nxt, std::memory_order_relaxed);
    }
};

#endif // _WIN32
#endif // MELON_PRIME_RAW_INPUT_FILTER_H