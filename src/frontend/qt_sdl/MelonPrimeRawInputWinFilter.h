#pragma once
#ifdef _WIN32

// Qt
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QtGlobal>

// Win32
#include <windows.h>

// STL
#include <array>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <cstdint>
#include <cstring>

#ifndef FORCE_INLINE
#  if defined(_MSC_VER)
#    define FORCE_INLINE __forceinline
#  else
#    define FORCE_INLINE __attribute__((always_inline)) inline
#  endif
#endif

// 極限低遅延：
// ・専用入力スレッド（message-only HWND）で WM_INPUT 即時処理
// ・マウス相対は単一バッファ：fetch_add(int32)×2、取得は exchange(0)×2（追加遅延ゼロ）
// ・VK/Mouse は 256bit＋5bit のビットベクタ（relaxed）
// ・UI側ドレインやダブルバッファは撤去
// ・MMCSS “Games” を動的に適用（avrt.dll を動的ロード）

class RawInputWinFilter final : public QAbstractNativeEventFilter
{
public:
    RawInputWinFilter();
    ~RawInputWinFilter() override;

    // Qt側には WM_INPUT を流さない設計なので常に false（ここでは何もしない）
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // 入力スレッド制御（useInputSink=true で非フォーカス時も取得）
    bool startInputThread(bool useInputSink = false);
    void stopInputThread();

    // マウス相対デルタ取得（取り出し時にゼロクリア）
    void fetchMouseDelta(int& outDx, int& outDy);
    void discardDeltas();

    // 現在のマウスボタン状態（bit0..4 = L,R,M,X1,X2）
    uint8_t mouseButtons() const noexcept;

    // VK押下（0..255）
    bool vkDown(uint32_t vk) const noexcept;

    // ホットキー設定／状態
    void setHotkeyVks(int hk, const std::vector<UINT>& vks);
    bool hotkeyDown(int hk) const noexcept;
    bool hotkeyPressed(int hk) noexcept;
    bool hotkeyReleased(int hk) noexcept;

    // 状態リセット
    void resetMouseButtons();
    void resetAllKeys();
    void resetHotkeyEdges();

private:
    enum MouseIndex : uint8_t { kMB_Left = 0, kMB_Right, kMB_Middle, kMB_X1, kMB_X2, kMB_Max };

    struct alignas(16) HotkeyMask {
        uint64_t vkMask[4];
        uint8_t  mouseMask;
        uint8_t  hasMask;
        uint8_t  _pad[6];
    };

    static constexpr size_t kMaxHotkeyId = 256;

    static constexpr uint8_t kMouseButtonLUT[8] = {
        0xFF,       // 0 (unused)
        kMB_Left,   // 1 VK_LBUTTON
        kMB_Right,  // 2 VK_RBUTTON
        0xFF,       // 3
        kMB_Middle, // 4 VK_MBUTTON
        kMB_X1,     // 5 VK_XBUTTON1
        kMB_X2,     // 6 VK_XBUTTON2
        0xFF        // 7
    };

    // ---- 共有状態（読み：エミュ／書き：入力スレッド） ----
    struct alignas(64) StateBits {
        std::array<std::atomic<uint64_t>, 4> vk; // 256bit VK押下
        std::atomic<uint8_t> mouse;              // 5bit L,R,M,X1,X2
        StateBits() : vk{ 0,0,0,0 }, mouse(0) {}
    } m_state;

    // 相対移動（単一バッファ）
    alignas(64) std::atomic<int32_t> m_dx{ 0 };
    alignas(64) std::atomic<int32_t> m_dy{ 0 };

    // ホットキー
    alignas(64) std::array<HotkeyMask, kMaxHotkeyId> m_hkMask{};
    std::array<std::atomic<uint64_t>, (kMaxHotkeyId + 63) / 64> m_hkPrev{};

    // ---- 入力スレッド資源 ----
    std::thread              m_thr;
    std::atomic<bool>        m_thrRunning{ false };
    std::atomic<bool>        m_thrQuit{ false };
    HWND                     m_hwndMsg = nullptr;
    HINSTANCE                m_hinst = nullptr;
    bool                     m_useInputSink = false;

    // RawInput 一時バッファ（十分大きめ）
    alignas(64) BYTE         m_rawBuf[256] = {};

    // ---- MMCSS（動的ロード）----
    // avrt.dll API の最小定義（ヘッダ非依存）
    enum AVRT_PRIORITY { AVRT_PRIORITY_LOW = -1, AVRT_PRIORITY_NORMAL = 0, AVRT_PRIORITY_HIGH = 1, AVRT_PRIORITY_CRITICAL = 2 };
    using PFN_AvSetMmThreadCharacteristicsW = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
    using PFN_AvRevertMmThreadCharacteristics = BOOL(WINAPI*)(HANDLE);
    using PFN_AvSetMmThreadPriority = BOOL(WINAPI*)(HANDLE, AVRT_PRIORITY);

    HMODULE                               m_hAvrt = nullptr;
    PFN_AvSetMmThreadCharacteristicsW     m_pAvSetMmThreadCharacteristicsW = nullptr;
    PFN_AvRevertMmThreadCharacteristics   m_pAvRevertMmThreadCharacteristics = nullptr;
    PFN_AvSetMmThreadPriority             m_pAvSetMmThreadPriority = nullptr;
    HANDLE                                m_mmcssHandle = nullptr;
    DWORD                                 m_mmcssTaskIndex = 0;

    // ---- 内部処理 ----
    static LRESULT CALLBACK  WndProcThunk(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT                  WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

    void                     inputThreadMain();
    bool                     registerRawInput(HWND target);
    void                     applyThreadQoS();
    void                     handleRawInput(const RAWINPUT& raw) noexcept;
    void                     handleRawMouse(const RAWMOUSE& m) noexcept;
    void                     handleRawKeyboard(const RAWKEYBOARD& k) noexcept;
    static void              addVkToMask(HotkeyMask& m, UINT vk) noexcept;

    // ユーティリティ
    FORCE_INLINE bool        getVkState(uint32_t vk) const noexcept;

    // 安全読みラッパ
    FORCE_INLINE bool        readRawInputToBuf(LPARAM lp, const RAWINPUT*& out) noexcept;

    // avrt.dll ロード
    void                     loadAvrt();
};

#endif // _WIN32
