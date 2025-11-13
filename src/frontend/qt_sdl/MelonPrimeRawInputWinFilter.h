#pragma once
#ifdef _WIN32

// Qt
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QBitArray>
#include <QtGlobal>

// Win32
#include <windows.h>
#include <hidsdi.h>

// C++/STL
#include <array>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <cstdint>
#include <cstring>

// FORCE_INLINE
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

    // WM_INPUT を Qt 側で受け取る（常に false）
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // 相対デルタ取得（取り出し時にゼロクリア）
    void fetchMouseDelta(int& outDx, int& outDy);

    // 相対デルタ消去
    void discardDeltas();

    // 全キー状態リセット
    void resetAllKeys();

    // 全マウスボタン状態リセット
    void resetMouseButtons();

    // ホットキーエッジリセット
    void resetHotkeyEdges();

    // ホットキーのキー登録（事前計算マスクを構築）
    void setHotkeyVks(int hk, const std::vector<UINT>& vks);

    // ホットキー状態取得
    bool hotkeyDown(int hk) const noexcept;
    bool hotkeyPressed(int hk) noexcept;
    bool hotkeyReleased(int hk) noexcept;

private:
    // マウスボタン index
    enum MouseIndex : uint8_t { kMB_Left = 0, kMB_Right, kMB_Middle, kMB_X1, kMB_X2, kMB_Max };

    // 前計算マスク
    struct alignas(16) HotkeyMask {
        uint64_t vkMask[4];  // 256bit
        uint8_t  mouseMask;  // L/R/M/X1/X2 (5bit)
        uint8_t  hasMask;
        uint8_t  _pad[6];
    };

    static constexpr size_t kMaxHotkeyId = 256;

    // マウスフラグまとめ（早期 return 用）
    static constexpr USHORT kAllMouseBtnMask =
        RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP |
        RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP |
        RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP |
        RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP |
        RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP;

    // VK → MouseIndex テーブル（0..7 のみ）
    static constexpr uint8_t kMouseButtonLUT[8] = {
        0xFF,       // 0 unused
        kMB_Left,   // 1 VK_LBUTTON
        kMB_Right,  // 2 VK_RBUTTON
        0xFF,       // 3
        kMB_Middle, // 4 VK_MBUTTON
        kMB_X1,     // 5 VK_XBUTTON1
        kMB_X2,     // 6 VK_XBUTTON2
        0xFF        // 7
    };

    // 固定長 RawInput バッファ
    alignas(64) BYTE m_rawBuf[256] = {};

    // 実時間キー/マウス状態
    struct StateBits {
        std::atomic<uint64_t> vkDown[4];   // 256 bit
        std::atomic<uint8_t>  mouseButtons;
    } m_state{ {0,0,0,0}, 0 };

    // VK押下状態
    FORCE_INLINE bool getVkState(uint32_t vk) const noexcept {
        if (vk >= 256) return false;
        const uint64_t w = m_state.vkDown[vk >> 6].load(std::memory_order_relaxed);
        return (w & (1ULL << (vk & 63))) != 0ULL;
    }

    // Mouse押下状態
    FORCE_INLINE bool getMouseButton(uint8_t idx) const noexcept {
        const uint8_t b = m_state.mouseButtons.load(std::memory_order_relaxed);
        return (idx < 5) && ((b >> idx) & 1u);
    }

    // VKセット/クリア
    FORCE_INLINE void setVkBit(uint32_t vk, bool down) noexcept {
        if (vk >= 256) return;
        const uint64_t mask = 1ULL << (vk & 63);
        auto& word = m_state.vkDown[vk >> 6];
        if (down) (void)word.fetch_or(mask, std::memory_order_relaxed);
        else      (void)word.fetch_and(~mask, std::memory_order_relaxed);
    }

    // 前計算マスク
    alignas(64) std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};

    // フォールバック用
    std::unordered_map<int, std::vector<UINT>> m_hkToVk;

    // エッジ検出
    std::array<std::atomic<uint64_t>, (kMaxHotkeyId + 63) / 64> m_hkPrev{};

    // RawInput登録
    RAWINPUTDEVICE m_rid[2]{};

    // 相対デルタ（あなたの希望で分離版のまま）
    std::atomic<int> dx{ 0 };
    std::atomic<int> dy{ 0 };

    // 互換
    std::array<std::atomic<int>, 256> m_vkDownCompat{};
    std::array<std::atomic<int>, kMB_Max> m_mbCompat{};

    // マスク構築
    static FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept;
};

#endif // _WIN32
