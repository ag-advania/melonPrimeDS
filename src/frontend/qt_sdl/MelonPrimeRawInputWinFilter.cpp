// MelonPrimeRawInputWinFilter.cpp
// High-Performance RawInput Filter Implementation
//
// Optimizations:
// 1. GetRawInputBuffer for batched processing
// 2. Local accumulation to minimize atomic operations
// 3. VK remapping LUT
// 4. Single-pass hotkey evaluation
// 5. MsgWaitForMultipleObjects for efficient waiting

#ifdef _WIN32

#include "MelonPrimeRawInputWinFilter.h"
#include <cassert>
#include <process.h>

//=====================================================
// LUT Init - Button Flags
//=====================================================
RawInputWinFilter::BtnLutEntry RawInputWinFilter::s_btnLut[1024];

//=====================================================
// LUT Init - VK Remapping
//=====================================================
RawInputWinFilter::VkRemapEntry RawInputWinFilter::s_vkRemap[256];

static struct RawLutInit {
    RawLutInit() {
        // Button LUT initialization
        for (int i = 0; i < 1024; ++i) {
            uint8_t d = 0, u = 0;
            if (i & RI_MOUSE_BUTTON_1_DOWN) d |= 0x01;
            if (i & RI_MOUSE_BUTTON_1_UP)   u |= 0x01;
            if (i & RI_MOUSE_BUTTON_2_DOWN) d |= 0x02;
            if (i & RI_MOUSE_BUTTON_2_UP)   u |= 0x02;
            if (i & RI_MOUSE_BUTTON_3_DOWN) d |= 0x04;
            if (i & RI_MOUSE_BUTTON_3_UP)   u |= 0x04;
            if (i & RI_MOUSE_BUTTON_4_DOWN) d |= 0x08;
            if (i & RI_MOUSE_BUTTON_4_UP)   u |= 0x08;
            if (i & RI_MOUSE_BUTTON_5_DOWN) d |= 0x10;
            if (i & RI_MOUSE_BUTTON_5_UP)   u |= 0x10;
            RawInputWinFilter::s_btnLut[i] = { d, u };
        }

        // VK Remapping LUT initialization
        memset(RawInputWinFilter::s_vkRemap, 0, sizeof(RawInputWinFilter::s_vkRemap));

        // Control key remapping
        RawInputWinFilter::s_vkRemap[VK_CONTROL] = { VK_LCONTROL, VK_RCONTROL };

        // Alt/Menu key remapping
        RawInputWinFilter::s_vkRemap[VK_MENU] = { VK_LMENU, VK_RMENU };

        // Note: VK_SHIFT uses MapVirtualKey, handled separately in remapVk()
    }
} s_lutInit;

//=====================================================
// Constructor / Destructor
//=====================================================
RawInputWinFilter::RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd)
    : m_mainHwnd(mainHwnd)
{
    // 初期化
    for (auto& a : m_state.vkDown) a.store(0);
    m_state.mouseButtons.store(0);
    m_mouseDeltaCombined.store(0);

    memset(m_hkMask.data(), 0, sizeof(m_hkMask));
    memset(m_hkPrev, 0, sizeof(m_hkPrev));

    // 初期モード設定
    m_joy2KeySupport.store(joy2KeySupport);

    if (joy2KeySupport) {
        // ON: メインウィンドウに登録 (Qt経由)
        if (m_mainHwnd) registerRawToTarget(m_mainHwnd);
    }
    else {
        // OFF: 独自スレッド起動 (低遅延)
        startThreadIfNeeded();
    }
}

RawInputWinFilter::~RawInputWinFilter()
{
    stopThreadIfRunning();
}

//=====================================================
// モード切り替え
//=====================================================
void RawInputWinFilter::setJoy2KeySupport(bool enable)
{
    const bool prev = m_joy2KeySupport.load(std::memory_order_relaxed);
    if (prev == enable) return;

    m_joy2KeySupport.store(enable, std::memory_order_relaxed);

    if (enable) {
        // OFF -> ON: 独自スレッド停止 -> メインウィンドウ登録
        stopThreadIfRunning();
        if (m_mainHwnd) registerRawToTarget(m_mainHwnd);
    }
    else {
        // ON -> OFF: メインウィンドウ登録は上書きされるので放置でOK -> 独自スレッド開始
        startThreadIfNeeded();
    }
}

void RawInputWinFilter::setRawInputTarget(HWND hwnd)
{
    if (m_mainHwnd != hwnd) {
        m_mainHwnd = hwnd;
        // 現在 ON モードなら、新しいウィンドウに対して再登録
        if (m_joy2KeySupport.load(std::memory_order_relaxed)) {
            if (m_mainHwnd) registerRawToTarget(m_mainHwnd);
        }
    }
}

//=====================================================
// RawInput 登録
//=====================================================
void RawInputWinFilter::registerRawToTarget(HWND targetHwnd)
{
    if (!targetHwnd) return;

    RAWINPUTDEVICE rid[2];
    // Mouse
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02;
    rid[0].dwFlags = RIDEV_INPUTSINK; // バックグラウンドでも受信
    rid[0].hwndTarget = targetHwnd;

    // Keyboard
    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x06;
    rid[1].dwFlags = RIDEV_INPUTSINK;
    rid[1].hwndTarget = targetHwnd;

    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

//=====================================================
// Qt Native Filter (Joy2KeySupport ON の時のみ)
//=====================================================
bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
    // OFF のときは独自スレッド側で処理しているので、こちらは無視
    if (!m_joy2KeySupport.load(std::memory_order_relaxed)) return false;

    MSG* msg = static_cast<MSG*>(message);
    if (!msg || msg->message != WM_INPUT) return false;

    processRawInput(reinterpret_cast<HRAWINPUT>(msg->lParam));

    return false; // Qtにイベントを流す（消費しない）
}

//=====================================================
// 【最適化】単一イベント処理 (スタックバッファ版)
//=====================================================
FORCE_INLINE void RawInputWinFilter::processRawInput(HRAWINPUT hRaw) noexcept
{
    constexpr UINT kBufSize = 1024;
    alignas(16) uint8_t rawBuf[kBufSize];

    UINT size = kBufSize;
    if (GetRawInputData(hRaw, RID_INPUT, rawBuf, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1) {
        return;
    }

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(rawBuf);
    const DWORD type = raw->header.dwType;

    // ---------------- Mouse ----------------
    if (type == RIM_TYPEMOUSE) {
        const RAWMOUSE& m = raw->data.mouse;

        // 移動量 (Relative)
        if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
            const LONG dx_ = m.lLastX;
            const LONG dy_ = m.lLastY;
            if (dx_ | dy_) {
                // 64bit CASループで X,Y を一度に更新
                uint64_t cur = m_mouseDeltaCombined.load(std::memory_order_relaxed);
                uint64_t nxt;
                do {
                    MouseDeltaPack p;
                    p.combined = cur;
                    p.s.x += dx_;
                    p.s.y += dy_;
                    nxt = p.combined;
                } while (!m_mouseDeltaCombined.compare_exchange_weak(cur, nxt, std::memory_order_relaxed));
            }
        }

        // ボタン
        const USHORT f = m.usButtonFlags & 0x03FF;
        if (f) {
            const auto& lut = s_btnLut[f];
            if (lut.downBits | lut.upBits) {
                uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
                uint8_t nxt = (cur | lut.downBits) & ~lut.upBits;
                if (cur != nxt) m_state.mouseButtons.store(nxt, std::memory_order_relaxed);
            }
        }
        return;
    }

    // ---------------- Keyboard ----------------
    if (type == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& kb = raw->data.keyboard;
        UINT vk = kb.VKey;
        const USHORT flags = kb.Flags;

        if (vk == 0 || vk >= 255) return;

        // 【最適化】LUTを使用した左右補正
        vk = remapVk(vk, kb.MakeCode, flags);

        bool isDown = !(flags & RI_KEY_BREAK);
        setVkBit(vk, isDown);
        return;
    }
}

//=====================================================
// 【最適化】バッチ処理版 RawInput 処理
// 高ポーリングレートマウスで効果的
//=====================================================
void RawInputWinFilter::processRawInputBatched() noexcept
{
    constexpr UINT kBufferSize = 16384; // 16KB buffer for batch processing
    alignas(16) uint8_t buffer[kBufferSize];

    // Get required buffer size first
    UINT size = kBufferSize;
    UINT count = GetRawInputBuffer(reinterpret_cast<RAWINPUT*>(buffer), &size, sizeof(RAWINPUTHEADER));

    if (count == 0 || count == (UINT)-1) return;

    // Local accumulators to minimize atomic operations
    int32_t accumX = 0, accumY = 0;
    uint8_t btnDown = 0, btnUp = 0;

    // Keyboard state changes (accumulated locally)
    uint64_t localVkDown[4];
    uint64_t localVkUp[4];
    memset(localVkDown, 0, sizeof(localVkDown));
    memset(localVkUp, 0, sizeof(localVkUp));
    bool hasKeyChanges = false;

    // Process all events in batch
    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buffer);
    for (UINT i = 0; i < count; ++i) {
        const DWORD type = raw->header.dwType;

        if (type == RIM_TYPEMOUSE) {
            const RAWMOUSE& m = raw->data.mouse;

            // Accumulate mouse movement
            if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                accumX += m.lLastX;
                accumY += m.lLastY;
            }

            // Accumulate button changes
            const USHORT f = m.usButtonFlags & 0x03FF;
            if (f) {
                const auto& lut = s_btnLut[f];
                btnDown |= lut.downBits;
                btnUp |= lut.upBits;
            }
        }
        else if (type == RIM_TYPEKEYBOARD) {
            const RAWKEYBOARD& kb = raw->data.keyboard;
            UINT vk = kb.VKey;

            if (vk != 0 && vk < 255) {
                vk = remapVk(vk, kb.MakeCode, kb.Flags);

                const uint32_t widx = vk >> 6;
                const uint64_t bit = 1ULL << (vk & 63);

                if (!(kb.Flags & RI_KEY_BREAK)) {
                    localVkDown[widx] |= bit;
                    localVkUp[widx] &= ~bit; // Cancel any pending up
                }
                else {
                    localVkUp[widx] |= bit;
                    localVkDown[widx] &= ~bit; // Cancel any pending down
                }
                hasKeyChanges = true;
            }
        }

        // Move to next RAWINPUT structure
        // NEXTRAWINPUTBLOCK uses QWORD which is not defined in MinGW, so we calculate manually
        raw = reinterpret_cast<RAWINPUT*>(reinterpret_cast<BYTE*>(raw) +
            ((raw->header.dwSize + sizeof(void*) - 1) & ~(sizeof(void*) - 1)));
    }

    // Apply accumulated mouse delta (single atomic operation)
    if (accumX | accumY) {
        uint64_t cur = m_mouseDeltaCombined.load(std::memory_order_relaxed);
        uint64_t nxt;
        do {
            MouseDeltaPack p;
            p.combined = cur;
            p.s.x += accumX;
            p.s.y += accumY;
            nxt = p.combined;
        } while (!m_mouseDeltaCombined.compare_exchange_weak(cur, nxt, std::memory_order_relaxed));
    }

    // Apply accumulated button changes
    if (btnDown | btnUp) {
        uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
        uint8_t nxt = (cur | btnDown) & ~btnUp;
        if (cur != nxt) m_state.mouseButtons.store(nxt, std::memory_order_relaxed);
    }

    // Apply accumulated keyboard changes
    if (hasKeyChanges) {
        for (int w = 0; w < 4; ++w) {
            if (localVkDown[w] | localVkUp[w]) {
                uint64_t cur = m_state.vkDown[w].load(std::memory_order_relaxed);
                uint64_t nxt = (cur | localVkDown[w]) & ~localVkUp[w];
                if (cur != nxt) {
                    m_state.vkDown[w].store(nxt, std::memory_order_relaxed);
                }
            }
        }
    }
}

//=====================================================
// 独自スレッド制御 (低遅延モード用)
//=====================================================
void RawInputWinFilter::startThreadIfNeeded()
{
    if (m_runThread.load(std::memory_order_relaxed)) return;

    m_runThread.store(true, std::memory_order_relaxed);
    m_hThread = CreateThread(nullptr, 0, ThreadFunc, this, 0, nullptr);

    // スレッド優先度を最高にする（メイン処理が重くてもマウス入力だけは飛ばさない）
    if (m_hThread) {
        SetThreadPriority(m_hThread, THREAD_PRIORITY_HIGHEST);
    }
}

void RawInputWinFilter::stopThreadIfRunning()
{
    if (!m_runThread.load(std::memory_order_relaxed)) return;

    m_runThread.store(false, std::memory_order_relaxed);

    if (m_hiddenWnd) {
        PostMessage(m_hiddenWnd, WM_CLOSE, 0, 0);
    }

    if (m_hThread) {
        WaitForSingleObject(m_hThread, 500);
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }
    m_hiddenWnd = nullptr;
}

DWORD WINAPI RawInputWinFilter::ThreadFunc(LPVOID param)
{
    static_cast<RawInputWinFilter*>(param)->threadLoop();
    return 0;
}

void RawInputWinFilter::threadLoop()
{
    // メッセージ専用ウィンドウ (軽量・非表示)
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = TEXT("MelonPrimeRawWorker");
    RegisterClassEx(&wc);

    m_hiddenWnd = CreateWindowEx(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

    if (!m_hiddenWnd) return;

    // このスレッドの隠しウィンドウにRawInputを紐付ける
    registerRawToTarget(m_hiddenWnd);

    // 【最適化】MsgWaitForMultipleObjectsを使用した効率的な待機
    while (m_runThread.load(std::memory_order_relaxed))
    {
        // Wait for RawInput messages with 1ms timeout
        DWORD ret = MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_RAWINPUT | QS_POSTMESSAGE);

        if (ret == WAIT_OBJECT_0) {
            // Check for WM_CLOSE first
            MSG msg;
            while (PeekMessage(&msg, m_hiddenWnd, WM_CLOSE, WM_CLOSE, PM_REMOVE)) {
                goto cleanup;
            }

            // 【最適化】バッチ処理で複数のWM_INPUTを一度に処理
            processRawInputBatched();

            // Process any remaining messages individually
            while (PeekMessage(&msg, m_hiddenWnd, WM_INPUT, WM_INPUT, PM_REMOVE)) {
                processRawInput(reinterpret_cast<HRAWINPUT>(msg.lParam));
            }
        }
        else if (ret == WAIT_TIMEOUT) {
            // Timeout - check if we should exit
            continue;
        }
    }

cleanup:
    DestroyWindow(m_hiddenWnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    m_hiddenWnd = nullptr;
}

//=====================================================
// その他 API
//=====================================================
void RawInputWinFilter::setHotkeyVks(int id, const std::vector<UINT>& vks)
{
    if (id < 0 || (size_t)id >= kMaxHotkeyId) return;
    HotkeyMask& m = m_hkMask[id];
    m.vkMask[0] = m.vkMask[1] = m.vkMask[2] = m.vkMask[3] = 0;
    m.mouseMask = 0;
    m.hasMask = false;
    if (vks.empty()) return;
    for (UINT k : vks) {
        if (k >= VK_LBUTTON && k <= VK_XBUTTON2) {
            int bit = -1;
            switch (k) {
            case VK_LBUTTON: bit = 0; break;
            case VK_RBUTTON: bit = 1; break;
            case VK_MBUTTON: bit = 2; break;
            case VK_XBUTTON1: bit = 3; break;
            case VK_XBUTTON2: bit = 4; break;
            }
            if (bit >= 0) m.mouseMask |= (1 << bit);
        }
        else if (k < 256) {
            m.vkMask[k >> 6] |= (1ULL << (k & 63));
        }
    }
    m.hasMask = true;
}

//=====================================================
// 【最適化】ホットキー判定 - 一括ビット演算
//=====================================================
bool RawInputWinFilter::hotkeyDown(int hk) const noexcept {
    if ((unsigned)hk >= kMaxHotkeyId) return false;
    const HotkeyMask& m = m_hkMask[(unsigned)hk];
    if (!m.hasMask) return false;

    // マウスボタンを先にチェック（頻度が高い場合に有効）
    if (m.mouseMask && (m_state.mouseButtons.load(std::memory_order_relaxed) & m.mouseMask))
        return true;

    // 【最適化】4つのmaskをOR結合してから一括判定
    // 分岐予測ミスを削減し、パイプライン効率を向上
    const uint64_t anyMatch =
        (m.vkMask[0] & m_state.vkDown[0].load(std::memory_order_relaxed)) |
        (m.vkMask[1] & m_state.vkDown[1].load(std::memory_order_relaxed)) |
        (m.vkMask[2] & m_state.vkDown[2].load(std::memory_order_relaxed)) |
        (m.vkMask[3] & m_state.vkDown[3].load(std::memory_order_relaxed));

    return anyMatch != 0;
}

bool RawInputWinFilter::hotkeyPressed(int hk) noexcept {
    if ((unsigned)hk >= kMaxHotkeyId) return false;
    const bool now = hotkeyDown(hk);
    const size_t idx = (unsigned)hk >> 6;
    const uint64_t bit = 1ULL << (hk & 63);
    const bool prev = (m_hkPrev[idx] & bit) != 0;
    if (now != prev) {
        if (now) m_hkPrev[idx] |= bit; else m_hkPrev[idx] &= ~bit;
    }
    return (now && !prev);
}

bool RawInputWinFilter::hotkeyReleased(int hk) noexcept {
    if ((unsigned)hk >= kMaxHotkeyId) return false;
    const bool now = hotkeyDown(hk);
    const size_t idx = (unsigned)hk >> 6;
    const uint64_t bit = 1ULL << (hk & 63);
    const bool prev = (m_hkPrev[idx] & bit) != 0;
    if (now != prev) {
        if (now) m_hkPrev[idx] |= bit; else m_hkPrev[idx] &= ~bit;
    }
    return (!now && prev);
}

void RawInputWinFilter::fetchMouseDelta(int& outX, int& outY) {
    // 【最適化】1回のアトミック交換でX, Y両方を取得・リセット
    uint64_t val = m_mouseDeltaCombined.exchange(0, std::memory_order_relaxed);
    MouseDeltaPack p;
    p.combined = val;
    outX = p.s.x;
    outY = p.s.y;
}

void RawInputWinFilter::discardDeltas() {
    m_mouseDeltaCombined.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetAllKeys() {
    for (auto& a : m_state.vkDown) a.store(0, std::memory_order_relaxed);
    m_state.mouseButtons.store(0, std::memory_order_relaxed);
    memset(m_hkPrev, 0, sizeof(m_hkPrev));
}

void RawInputWinFilter::resetMouseButtons() {
    m_state.mouseButtons.store(0, std::memory_order_relaxed);
}

void RawInputWinFilter::resetHotkeyEdges() {
    for (size_t i = 0; i < kMaxHotkeyId; ++i) {
        bool down = hotkeyDown((int)i);
        const size_t idx = i >> 6;
        const uint64_t bit = 1ULL << (i & 63);
        if (down) m_hkPrev[idx] |= bit; else m_hkPrev[idx] &= ~bit;
    }
}

void RawInputWinFilter::clearAllBindings() {
    memset(m_hkMask.data(), 0, sizeof(m_hkMask));
    memset(m_hkPrev, 0, sizeof(m_hkPrev));
}

#endif // _WIN32
