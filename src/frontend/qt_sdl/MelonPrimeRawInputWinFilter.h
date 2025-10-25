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
// - Double-buffered mouse delta accumulation (fetch_add) to avoid CAS spin.
// - CPU-specific intrinsics are NOT used (generic x86-64 only).
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

    // Double buffer for mouse relative deltas.
    struct alignas(64) DeltaBuf {
        std::atomic<int32_t> dx{ 0 };
        std::atomic<int32_t> dy{ 0 };
    };

    static constexpr int kMaxHotkeyId = 256;

public:
    RawInputWinFilter();
    ~RawInputWinFilter() override = default;

    // QAbstractNativeEventFilter
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // Optional explicit registration. If hwnd != nullptr, target that window.
    bool registerRawInput(HWND hwnd);

    // Mouse delta aggregation
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

private:
    // Registered raw input devices (mouse, keyboard)
    RAWINPUTDEVICE m_rid[2]{};

    // State (64B-aligned fields live inside)
    InputState m_state{};

    // Mouse delta double-buffer
    alignas(64) DeltaBuf  m_delta[2]{};
    std::atomic<uint8_t>  m_writeIdx{ 0 };

    // Legacy leftover (unused here; kept to avoid ABI break)
    alignas(8) std::atomic<uint64_t> m_dxyPack{ 0 };

    // Compat mirrors (byte-per-key/button)
    std::array<std::atomic<uint8_t>, 256> m_vkDownCompat{};
    std::array<std::atomic<uint8_t>, 5>   m_mbCompat{};

    // Hotkey edge memory
    std::array<std::atomic<uint64_t>, (kMaxHotkeyId + 63) / 64> m_hkPrev{};

    // Hotkey masks
    std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};

    // Single RAWINPUT scratch buffer
    alignas(8) BYTE m_rawBuf[sizeof(RAWINPUT) + 64]{};

private:
    // Helpers
    FORCE_INLINE void accumMouseDelta(LONG dx, LONG dy) noexcept;
    FORCE_INLINE void setVkBit(UINT vk, bool down) noexcept;
    FORCE_INLINE bool getVkState(UINT vk) const noexcept;
    FORCE_INLINE bool getMouseButton(int b) const noexcept;
    FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept;
};

#endif // _WIN32
