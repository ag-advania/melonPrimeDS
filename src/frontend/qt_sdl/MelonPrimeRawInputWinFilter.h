#pragma once
#ifdef _WIN32

// ライブラリ読込(QAbstractNativeEventFilterとQt型の可視化のため)
#include <QtCore/QAbstractNativeEventFilter>
// ライブラリ読込(QByteArrayの可視化のため)
#include <QtCore/QByteArray>
// ライブラリ読込(qintptr定義のため)
#include <QtCore/qglobal.h>

// ライブラリ読込(Win32型使用のため)
#include <windows.h>
// ライブラリ読込(HID関連定数のため)
#include <hidsdi.h>

// ライブラリ読込(STL各種のため)
#include <array>
// ライブラリ読込(可変長配列のため)
#include <vector>
// ライブラリ読込(連想配列のため)
#include <unordered_map>
// ライブラリ読込(原子操作のため)
#include <atomic>
// ライブラリ読込(標準整数のため)
#include <cstdint>
// ライブラリ読込(メモリ操作のため)
#include <cstring>

// 強制インライン定義(ホットパス最適化のため)
#ifndef FORCE_INLINE
#  if defined(_MSC_VER)
#    define FORCE_INLINE __forceinline
#  else
#    define FORCE_INLINE __attribute__((always_inline)) inline
#  endif
#endif

/**
 * RawInputフィルタ本体.
 *
 * 低遅延重視でWM_INPUTを直接処理し、キー/マウス状態と相対デルタを集計する.
 */
class RawInputWinFilter : public QAbstractNativeEventFilter
{
public:
    /**
     * 構築子.
     *
     *
     * @brief RawInputデバイス登録と内部状態初期化を行う.
     */
    RawInputWinFilter();

    /**
     * 破棄子.
     *
     *
     * @brief RawInputデバイス登録を解除する.
     */
    ~RawInputWinFilter() override;

    /**
     * ネイティブイベントフィルタ.
     *
     *
     * @param eventType イベント種別文字列.
     * @param message Win32 MSGポインタ.
     * @param result 予約領域.
     * @return bool 継続可否.
     */
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    /**
     * マウス相対デルタ取得.
     *
     *
     * @param outDx X相対量.
     * @param outDy Y相対量.
     */
    void fetchMouseDelta(int& outDx, int& outDy);

    /**
     * 未読デルタ破棄.
     *
     *
     * @brief 蓄積された相対デルタをゼロ化する.
     */
    void discardDeltas();

    /**
     * 全キーダウン状態リセット.
     *
     *
     * @brief VK配列と互換配列を未押下化する.
     */
    void resetAllKeys();

    /**
     * 全マウスボタンリセット.
     *
     *
     * @brief 内部ビットと互換配列を未押下化する.
     */
    void resetMouseButtons();

    /**
     * ホットキーエッジリセット.
     *
     *
     * @brief 押下/解放エッジ検出用の前回状態をクリアする.
     */
    void resetHotkeyEdges();

    /**
     * ホットキー登録.
     *
     *
     * @param hk ホットキーID.
     * @param vks 構成仮想キー配列.
     */
    void setHotkeyVks(int hk, const std::vector<UINT>& vks);

    /**
     * ホットキー押下中判定.
     *
     *
     * @param hk ホットキーID.
     * @return bool 押下中.
     */
    bool hotkeyDown(int hk) const noexcept;

    /**
     * ホットキー立上り検出.
     *
     *
     * @param hk ホットキーID.
     * @return bool 立上り.
     */
    bool hotkeyPressed(int hk) noexcept;

    /**
     * ホットキー立下り検出.
     *
     *
     * @param hk ホットキーID.
     * @return bool 立下り.
     */
    bool hotkeyReleased(int hk) noexcept;

private:
    // 内部マスク構造体定義(前計算照合のため)
    struct HotkeyMask {
        // ビットマップ配列定義(VK 256bit保持のため)
        uint64_t vkMask[4]{ 0,0,0,0 };
        // マウスボタン集合定義(5bit保持のため)
        uint8_t  mouseMask{ 0 };
        // 有効フラグ定義(分岐短絡のため)
        uint8_t  hasMask{ 0 };
        // パディング定義(アライン保持のため)
        uint16_t _pad{ 0 };
    };

    // 入力状態構造体定義(低コスト参照のため)
    struct InputState {
        // VK押下状態定義(64bit×4のため)
        std::array<std::atomic<uint64_t>, 4> vkDown{ {0,0,0,0} };
        // マウスボタン押下状態定義(5bitのため)
        std::atomic<uint8_t>                 mouseButtons{ 0 };
    };

    // ホットキー数上限定義(配列長決定のため)
    static constexpr int kMaxHotkeyId = 256;

    // RAWINPUT登録配列定義(登録/解除管理のため)
    RAWINPUTDEVICE m_rid[2]{};

    // 入力状態保持定義(内部集計のため)
    InputState m_state{};

    // X/Y相対デルタ64bitパック定義(原子RMW回数削減のため)
    alignas(8) std::atomic<uint64_t> m_dxyPack{ 0 };

    // 互換配列定義(既存I/F維持のため)
    std::array<std::atomic<uint8_t>, 256> m_vkDownCompat{};
    // 互換配列定義(既存I/F維持のため)
    std::array<std::atomic<uint8_t>, 5>   m_mbCompat{};

    // エッジ検出用前回状態定義(64bit分割のため)
    std::array<std::atomic<uint64_t>, (kMaxHotkeyId + 63) / 64> m_hkPrev{};

    // 前計算マスク集合定義(高速照合のため)
    std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};

    // フォールバック登録定義(巨大/特殊HKのため)
    std::unordered_map<int, std::vector<UINT>> m_hkToVk;

    // RAWINPUT受信用バッファ定義(単一取得のため)
    alignas(8) BYTE m_rawBuf[sizeof(RAWINPUT) + 64]{};

    // VK状態設定ヘルパ定義(原子1回更新のため)
    FORCE_INLINE void setVkBit(UINT vk, bool down) noexcept;
    // VK状態取得ヘルパ定義(低コスト参照のため)
    FORCE_INLINE bool getVkState(UINT vk) const noexcept;
    // マウスボタン取得ヘルパ定義(低コスト参照のため)
    FORCE_INLINE bool getMouseButton(int b) const noexcept;
    // マスク構築ヘルパ定義(事前計算のため)
    FORCE_INLINE void addVkToMask(HotkeyMask& m, UINT vk) noexcept;
};

#endif // _WIN32
