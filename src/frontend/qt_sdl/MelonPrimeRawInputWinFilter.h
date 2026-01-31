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

// ============================================================
// High-Performance RawInput Filter
// 
// Optimizations applied:
// 1. Batched RawInput processing with GetRawInputBuffer
// 2. Single-pass hotkey evaluation with bitwise OR
// 3. VK remapping LUT for keyboard processing
// 4. MsgWaitForMultipleObjects for efficient thread waiting
// 5. Cache-aligned structures with explicit padding
// 6. Minimized atomic operations via local accumulation
// ============================================================

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

    // Helpers
    struct BtnLutEntry { uint8_t downBits; uint8_t upBits; };
    static BtnLutEntry s_btnLut[1024];

    // 【最適化】VK Remapping LUT for keyboard left/right distinction
    struct VkRemapEntry { uint8_t normal; uint8_t extended; };
    static VkRemapEntry s_vkRemap[256];

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

    // 【最適化】バッチ処理版 RawInput 処理
    FORCE_INLINE void processRawInput(HRAWINPUT hRaw) noexcept;
    void processRawInputBatched() noexcept;

    // State (alignasでキャッシュ競合防止)
    struct alignas(64) StateBits {
        std::atomic<uint64_t> vkDown[4];
        std::atomic<uint8_t>  mouseButtons;
        uint8_t _pad[7]; // 明示的パディング
    } m_state;

    // 【最適化】 X(32bit) と Y(32bit) を 64bit変数にパッキング
    // アトミック操作を1回で済ませるための共用体
    union MouseDeltaPack {
        struct { int32_t x; int32_t y; } s;
        uint64_t combined;
    };
    std::atomic<uint64_t> m_mouseDeltaCombined{ 0 };

    // 【最適化】HotkeyMask構造体を64バイト境界にアライン
    struct alignas(8) HotkeyMask {
        uint64_t vkMask[4];  // 32 bytes
        uint8_t  mouseMask;  // 1 byte
        bool     hasMask;    // 1 byte
        uint8_t  _pad[6];    // 明示的パディングで40バイト
    };
    static_assert(sizeof(HotkeyMask) == 40, "HotkeyMask size check");

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

    // 【最適化】バッチ処理用: 複数のVKビットを一度に設定
    FORCE_INLINE void setVkBitBatched(uint64_t* localVk, uint32_t vk, bool down) noexcept {
        if (vk >= 256) return;
        const uint32_t widx = vk >> 6;
        const uint64_t bit = 1ULL << (vk & 63);
        if (down) localVk[widx] |= bit;
        else localVk[widx] &= ~bit;
    }

    // 【最適化】VKリマップ (LUT使用)
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
