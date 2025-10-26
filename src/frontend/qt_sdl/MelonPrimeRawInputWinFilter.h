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

// 互換ミラー（m_vkDownCompat/m_mbCompat）を書かないなら 0 に
#ifndef RAWFILTER_ENABLE_COMPAT_MIRRORS
#define RAWFILTER_ENABLE_COMPAT_MIRRORS 1
#endif

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
        uint8_t  hasMask{ 0 };
        uint16_t _pad{ 0 };
    };

    struct alignas(64) InputState {
        std::array<std::atomic<uint64_t>, 4> vkDown{ {0,0,0,0} };
        std::atomic<uint8_t>                mouseButtons{ 0 }; // 5 bits
    };

    struct alignas(64) DeltaBuf {
        std::atomic<int32_t> dx{ 0 };
        std::atomic<int32_t> dy{ 0 };
    };

    static constexpr int kMaxHotkeyId = 256;

public:
    RawInputWinFilter();
    ~RawInputWinFilter() override = default;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    bool registerRawInput(HWND hwnd);
    bool registerRawInputEx(HWND hwnd, bool inputSink);
    // 任意フラグを直接渡したい場合用（NOLEGACY/NOHOTKEYSは付けないでください）
    bool registerRawInputEx2(HWND hwnd, DWORD flags);

    // フレーム先頭で呼ぶだけ～WM_INPUTを即時ドレインして最新状態へ
    bool drainRawInputNow(int maxLoops = 256);

    void fetchMouseDelta(int& outDx, int& outDy);
    bool getMouseDelta(int& outDx, int& outDy) { fetchMouseDelta(outDx, outDy); return (outDx | outDy) != 0; }
    void discardDeltas();

    void resetAll();
    void resetAllKeys();
    void resetMouseButtons();
    void resetHotkeyEdges();

    bool hotkeyDown(int hk) const noexcept;
    bool hotkeyPressed(int hk) noexcept;
    bool hotkeyReleased(int hk) noexcept;
    void setHotkeyVks(int hk, const std::vector<UINT>& vks) noexcept;
    void setHotkeyVksRaw(int hk, const UINT* vks, size_t count) noexcept;

    bool keyDown(UINT vk) const noexcept;
    bool mouseButtonDown(int b) const noexcept;

private:
    RAWINPUTDEVICE m_rid[2]{};

    InputState m_state{};

    alignas(64) DeltaBuf  m_delta[2]{};
    std::atomic<uint8_t>  m_writeIdx{ 0 };

    alignas(8) std::atomic<uint64_t> m_dxyPack{ 0 }; // 旧互換

#if RAWFILTER_ENABLE_COMPAT_MIRRORS
    std::array<std::atomic<uint8_t>, 256> m_vkDownCompat{};
    std::array<std::atomic<uint8_t>, 5>   m_mbCompat{};
#endif

    std::array<std::atomic<uint64_t>, (kMaxHotkeyId + 63) / 64> m_hkPrev{};
    std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};

    alignas(8) BYTE m_rawBuf[sizeof(RAWINPUT) + 64]{};

private:
    // 共通ハンドラ（nativeEventFilter/ドレインの双方から呼ぶ）
    FORCE_INLINE void handleRawInput(const RAWINPUT& ri) noexcept;
    FORCE_INLINE void handleRawMouse(const RAWMOUSE& m) noexcept;
    FORCE_INLINE void handleRawKeyboard(const RAWKEYBOARD& k) noexcept;

    FORCE_INLINE void accumMouseDelta(LONG dx, LONG dy) noexcept;
    FORCE_INLINE void setVkBit(UINT vk, bool down) noexcept;
    FORCE_INLINE bool getVkState(UINT vk) const noexcept;
    FORCE_INLINE bool getMouseButton(int b) const noexcept;
    FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept;
};

#endif // _WIN32
