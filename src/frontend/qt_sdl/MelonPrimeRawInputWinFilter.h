#pragma once
#ifdef _WIN32

#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QByteArray>
#include <QtCore/qglobal.h>

#include <windows.h>
#include <hidsdi.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#ifndef FORCE_INLINE
#  if defined(_MSC_VER)
#    define FORCE_INLINE __forceinline
#  else
#    define FORCE_INLINE __attribute__((always_inline)) inline
#  endif
#endif

// Low-latency Raw Input filter (Qt + Win32).
// - No RIDEV_NOLEGACY / No RIDEV_NOHOTKEYS.
// - No CPU-specific intrinsics (generic x86-64).
// - Level3 integrated: dedicated input thread + message-only window + MsgWaitForMultipleObjectsEx.
// - Level1 maintained: double-buffered mouse delta (fetch_add), generic 5-bit mouse mask, 64B alignment.
class RawInputWinFilter final : public QAbstractNativeEventFilter
{
public:
    enum : uint32_t {
        kTypeNone = 0,
        kTypeMouse = 1,
        kTypeKeyboard = 2
    };

    struct HotkeyMask {
        uint64_t vkMask[4]{ 0,0,0,0 }; // 256 VK bits
        uint8_t  mouseMask{ 0 };       // 5 mouse bits (L,R,M,X1,X2)
        uint8_t  hasMask{ 0 };         // 1 if any mask is set
        uint16_t _pad{ 0 };
    };

    // 64B-aligned to avoid false sharing on hot path.
    struct alignas(64) InputState {
        std::array<std::atomic<uint64_t>, 4> vkDown{ {0,0,0,0} };
        std::atomic<uint8_t>                mouseButtons{ 0 }; // 5 bits used
    };

    // Double buffer for mouse relative deltas (kept for compatibility).
    struct alignas(64) DeltaBuf {
        std::atomic<int32_t> dx{ 0 };
        std::atomic<int32_t> dy{ 0 };
    };

    static constexpr int kMaxHotkeyId = 256;

public:
    RawInputWinFilter();
    ~RawInputWinFilter() override;

    // QAbstractNativeEventFilter
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // Optional explicit registration to a specific hwnd (bind RawInput to that window).
    // ※ スレッド起動中に呼ぶと、まず全登録を除去→指定hwndに再登録します。
    bool registerRawInput(HWND hwnd);

    // Mouse delta aggregation（従来I/F維持）
    void fetchMouseDelta(int& outDx, int& outDy);
    bool getMouseDelta(int& outDx, int& outDy) { fetchMouseDelta(outDx, outDy); return (outDx | outDy) != 0; }
    void discardDeltas();

    // Resets
    void resetAll();
    void resetAllKeys();
    void resetMouseButtons();
    void resetHotkeyEdges();

    // Hotkey API
    bool hotkeyDown(int hk) const noexcept;
    bool hotkeyPressed(int hk) noexcept;   // edge: 0->1
    bool hotkeyReleased(int hk) noexcept;  // edge: 1->0
    void setHotkeyVks(int hk, const std::vector<UINT>& vks) noexcept;

    // Direct queries
    bool keyDown(UINT vk) const noexcept;
    bool mouseButtonDown(int b) const noexcept; // 0..4 = L,R,M,X1,X2

    // ==== Level3: input thread control ====
    // Start dedicated input thread (MsgWaitForMultipleObjectsEx + message-only window).
    // If inputSink=true, receive even when unfocused (RIDEV_INPUTSINK).
    bool startInputThread(bool inputSink = false) noexcept;

    // Stop thread if running.
    void stopInputThread() noexcept;

    bool isInputThreadRunning() const noexcept {
        return m_threadRunning.load(std::memory_order_acquire);
    }

private:
    // Registered raw input devices (descriptors reused)
    RAWINPUTDEVICE m_rid[2]{};

    // State
    InputState m_state{};

    // Mouse delta double-buffer（互換）
    alignas(64) DeltaBuf  m_delta[2]{};
    std::atomic<uint8_t>  m_writeIdx{ 0 };

    // Legacy leftover（未使用だがABI維持のため残置）
    alignas(8) std::atomic<uint64_t> m_dxyPack{ 0 };

    // Compat mirrors (byte-per-key/button)
    std::array<std::atomic<uint8_t>, 256> m_vkDownCompat{};
    std::array<std::atomic<uint8_t>, 5>   m_mbCompat{};

    // Hotkey edge memory
    std::array<std::atomic<uint64_t>, (kMaxHotkeyId + 63) / 64> m_hkPrev{};

    // Hotkey masks
    std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};

    // Single RAWINPUT scratch buffer（保険的に残す）
    alignas(8) BYTE m_rawBuf[sizeof(RAWINPUT) + 64]{};

    // ==== Level3 thread fields ====
    std::thread        m_inputThread{};
    std::atomic<bool>  m_threadRunning{ false };
    std::atomic<bool>  m_stopRequested{ false };
    std::atomic<HWND>  m_threadHwnd{ nullptr };

private:
    // Helpers（共通ロジック）
    FORCE_INLINE void accumMouseDelta(LONG dx, LONG dy) noexcept;
    FORCE_INLINE void setVkBit(UINT vk, bool down) noexcept;
    FORCE_INLINE bool getVkState(UINT vk) const noexcept;
    FORCE_INLINE bool getMouseButton(int b) const noexcept;
    FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept;

    // Level3 internals
    static LRESULT CALLBACK ThreadWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void inputThreadMain(bool inputSink) noexcept;
    void handleRawInputMessage(LPARAM lParam) noexcept;
    void clearAllRawInputRegistration() noexcept;
};

#endif // _WIN32
