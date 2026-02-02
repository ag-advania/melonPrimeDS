#ifndef MELON_PRIME_RAW_INPUT_FILTER_H
#define MELON_PRIME_RAW_INPUT_FILTER_H

#ifdef _WIN32

// ビルド高速化と競合回避
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QtGlobal>
#include <windows.h>
#include <hidsdi.h>
#include <array>
#include <vector>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

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
    static RawInputWinFilter* Acquire(bool joy2KeySupport, HWND mainHwnd);
    static void Release();

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    void setJoy2KeySupport(bool enable);
    bool getJoy2KeySupport() const noexcept { return m_joy2KeySupport.load(std::memory_order_relaxed); }

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

    struct BtnLutEntry { uint8_t downBits; uint8_t upBits; };
    static BtnLutEntry s_btnLut[1024];

    struct VkRemapEntry { uint8_t normal; uint8_t extended; };
    static VkRemapEntry s_vkRemap[256];

private:
    RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd);
    ~RawInputWinFilter() override;

    static RawInputWinFilter* s_instance;
    static int s_refCount;
    static std::mutex s_mutex;
    static std::once_flag s_initFlag; // テーブル初期化用フラグ

    static void initializeTables();   // 初期化関数

    std::atomic<bool> m_joy2KeySupport{ false };
    HWND m_mainHwnd = nullptr;

    std::atomic<bool> m_runThread{ false };
    HANDLE m_hThread = nullptr;
    HWND m_hiddenWnd = nullptr;

    static DWORD WINAPI ThreadFunc(LPVOID param);
    void threadLoop();
    void startThreadIfNeeded();
    void stopThreadIfRunning();

    void registerRawToTarget(HWND targetHwnd);

    FORCE_INLINE void processRawInput(HRAWINPUT hRaw) noexcept;
    void processRawInputBatched() noexcept;

    struct alignas(64) StateBits {
        std::atomic<uint64_t> vkDown[4];
        std::atomic<uint8_t>  mouseButtons;
        uint8_t _pad[7];
    } m_state;

    union MouseDeltaPack {
        struct { int32_t x; int32_t y; } s;
        uint64_t combined;
    };
    std::atomic<uint64_t> m_mouseDeltaCombined{ 0 };

    struct alignas(8) HotkeyMask {
        uint64_t vkMask[4];
        uint8_t  mouseMask;
        bool     hasMask;
        uint8_t  _pad[6];
    };
    static_assert(sizeof(HotkeyMask) == 40, "HotkeyMask size check");

    std::array<HotkeyMask, kMaxHotkeyId> m_hkMask;
    uint64_t m_hkPrev[kMaxHotkeyId / 64 + 1];

    FORCE_INLINE void setVkBit(uint32_t vk, bool down) noexcept {
        if (vk >= 256) return;
        const uint32_t widx = vk >> 6;
        const uint64_t bit = 1ULL << (vk & 63);
        uint64_t cur = m_state.vkDown[widx].load(std::memory_order_relaxed);
        uint64_t nxt = down ? (cur | bit) : (cur & ~bit);
        if (cur != nxt) m_state.vkDown[widx].store(nxt, std::memory_order_relaxed);
    }

    FORCE_INLINE uint32_t remapVk(uint32_t vk, USHORT makeCode, USHORT flags) const noexcept {
        if (vk == VK_SHIFT) {
            return MapVirtualKey(makeCode, MAPVK_VSC_TO_VK_EX);
        }
        const VkRemapEntry& entry = s_vkRemap[vk];
        if (entry.normal != 0) {
            return (flags & RI_KEY_E0) ? entry.extended : entry.normal;
        }
        return vk;
    }
};

#endif // _WIN32
#endif // MELON_PRIME_RAW_INPUT_FILTER_H