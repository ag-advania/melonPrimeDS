// MelonPrimeRawInputWinFilter.cpp
#ifdef _WIN32

#include "MelonPrimeRawInputWinFilter.h"
#include <cassert>
#include <cstdio>
#include <process.h>
#include <mmsystem.h>
#include <vector>
#include <algorithm>

// ライブラリリンク
#pragma comment(lib, "winmm.lib")

// =====================================================
// [Internal] NT / Undocumented API Definitions
// =====================================================
// ヘッダーを汚さないよう、CPP内でのみ定義

// Timer Resolution
typedef LONG(NTAPI* PFN_NtQueryTimerResolution)(PULONG MaximumTime, PULONG MinimumTime, PULONG CurrentTime);
typedef LONG(NTAPI* PFN_NtSetTimerResolution)(ULONG DesiredTime, BOOLEAN SetResolution, PULONG ActualTime);

// Raw Input / Message (Syscall wrappers in win32u.dll)
typedef UINT(WINAPI* NtUserGetRawInputData_t)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
typedef UINT(WINAPI* NtUserGetRawInputBuffer_t)(PRAWINPUT, PUINT, UINT);
typedef BOOL(WINAPI* NtUserPeekMessage_t)(LPMSG, HWND, UINT, UINT, UINT, BOOL);

// API Pointers
static NtUserGetRawInputData_t   fnNtUserGetRawInputData = nullptr;
static NtUserGetRawInputBuffer_t fnNtUserGetRawInputBuffer = nullptr;
static NtUserPeekMessage_t       fnNtUserPeekMessage = nullptr;

// API Loader
static void ResolveNtApis()
{
    static std::once_flag flag;
    std::call_once(flag, []() {
        // Windows 10/11: win32u.dll (Syscall stubs)
        // Legacy: user32.dll
        HMODULE h = LoadLibraryA("win32u.dll");
        if (!h) h = LoadLibraryA("user32.dll");
        if (!h) return;

        fnNtUserGetRawInputData = (NtUserGetRawInputData_t)GetProcAddress(h, "NtUserGetRawInputData");
        fnNtUserGetRawInputBuffer = (NtUserGetRawInputBuffer_t)GetProcAddress(h, "NtUserGetRawInputBuffer");
        fnNtUserPeekMessage = (NtUserPeekMessage_t)GetProcAddress(h, "NtUserPeekMessage");
        });
}

// =====================================================
// [Internal] High Resolution Timer Scope
// =====================================================
// スコープ滞在時のみシステムタイマー精度を上げる(0.5ms目標)
class HighResTimerScope {
public:
    HighResTimerScope() {
        HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
        if (hNtDll) {
            auto NtQuery = (PFN_NtQueryTimerResolution)GetProcAddress(hNtDll, "NtQueryTimerResolution");
            auto NtSet = (PFN_NtSetTimerResolution)GetProcAddress(hNtDll, "NtSetTimerResolution");
            if (NtQuery && NtSet) {
                ULONG minRes, maxRes, curRes;
                if (NtQuery(&maxRes, &minRes, &curRes) == 0) {
                    m_targetRes = maxRes; // 通常 0.5ms (5000 units)
                    if (NtSet(m_targetRes, TRUE, &m_currentRes) == 0) {
                        m_usingNtApi = true;
                        return;
                    }
                }
            }
        }
        // Fallback
        timeBeginPeriod(1);
        m_usingNtApi = false;
    }

    ~HighResTimerScope() {
        if (m_usingNtApi) {
            HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
            if (hNtDll) {
                auto NtSet = (PFN_NtSetTimerResolution)GetProcAddress(hNtDll, "NtSetTimerResolution");
                if (NtSet) {
                    ULONG temp;
                    NtSet(m_targetRes, FALSE, &temp);
                }
            }
        }
        else {
            timeEndPeriod(1);
        }
    }

private:
    bool m_usingNtApi = false;
    ULONG m_targetRes = 0;
    ULONG m_currentRes = 0;
};

// =====================================================
// Static Member Initialization
// =====================================================
RawInputWinFilter* RawInputWinFilter::s_instance = nullptr;
int RawInputWinFilter::s_refCount = 0;
std::mutex RawInputWinFilter::s_mutex;
std::once_flag RawInputWinFilter::s_initFlag;

RawInputWinFilter::BtnLutEntry RawInputWinFilter::s_btnLut[1024];
RawInputWinFilter::VkRemapEntry RawInputWinFilter::s_vkRemap[256];

void RawInputWinFilter::initializeTables() {
    ResolveNtApis();

    // Mouse Button LUT作成
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
        s_btnLut[i] = { d, u };
    }

    // VK Remapテーブル初期化
    memset(s_vkRemap, 0, sizeof(s_vkRemap));
    s_vkRemap[VK_CONTROL] = { VK_LCONTROL, VK_RCONTROL };
    s_vkRemap[VK_MENU] = { VK_LMENU, VK_RMENU };
    s_vkRemap[VK_SHIFT] = { VK_LSHIFT, VK_RSHIFT };
}

// =====================================================
// Singleton / Lifecycle
// =====================================================
RawInputWinFilter* RawInputWinFilter::Acquire(bool joy2KeySupport, HWND mainHwnd)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_instance) {
        s_instance = new RawInputWinFilter(joy2KeySupport, mainHwnd);
    }
    s_refCount++;
    return s_instance;
}

void RawInputWinFilter::Release()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_refCount > 0) {
        s_refCount--;
        if (s_refCount == 0) {
            delete s_instance;
            s_instance = nullptr;
        }
    }
}

RawInputWinFilter::RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd)
    : m_mainHwnd(mainHwnd)
{
    std::call_once(s_initFlag, &RawInputWinFilter::initializeTables);

    resetAllKeys();
    discardDeltas();
    clearAllBindings();

    m_joy2KeySupport.store(joy2KeySupport);

    if (joy2KeySupport) {
        if (m_mainHwnd) registerRawToTarget(m_mainHwnd);
    }
    else {
        startThreadIfNeeded();
    }
}

RawInputWinFilter::~RawInputWinFilter()
{
    stopThreadIfRunning();
}

// =====================================================
// Public Configuration
// =====================================================
void RawInputWinFilter::setJoy2KeySupport(bool enable)
{
    bool current = m_joy2KeySupport.load(std::memory_order_relaxed);
    if (current == enable) return;

    m_joy2KeySupport.store(enable);

    if (enable) {
        stopThreadIfRunning();
        if (m_mainHwnd) registerRawToTarget(m_mainHwnd);
    }
    else {
        startThreadIfNeeded();
    }
}

void RawInputWinFilter::setRawInputTarget(HWND hwnd)
{
    if (m_mainHwnd != hwnd) {
        m_mainHwnd = hwnd;
        if (m_joy2KeySupport.load(std::memory_order_relaxed) && m_mainHwnd) {
            registerRawToTarget(m_mainHwnd);
        }
    }
}

void RawInputWinFilter::registerRawToTarget(HWND targetHwnd)
{
    if (!targetHwnd) return;

    RAWINPUTDEVICE rid[2];
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02; // Mouse
    rid[0].dwFlags = RIDEV_INPUTSINK;
    rid[0].hwndTarget = targetHwnd;

    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x06; // Keyboard
    rid[1].dwFlags = RIDEV_INPUTSINK;
    rid[1].hwndTarget = targetHwnd;

    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

// =====================================================
// Main Processing Logic
// =====================================================

// メインスレッド用フィルタ (joy2KeySupport ON時)
// 添付ファイルの実装と同様に、バッチ処理を行わず、標準的なGetRawInputData処理のみを行うことで
// カクつき（スタッター）を防止します。
bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
    if (!m_joy2KeySupport.load(std::memory_order_relaxed)) return false;

    MSG* msg = static_cast<MSG*>(message);
    if (!msg) return false;

    // 非アクティブ時の入力リセット (堅牢性のため維持)
    if (msg->message == WM_ACTIVATEAPP) {
        if (msg->wParam == FALSE) { // Deactivated
            resetAllKeys();
        }
        return false;
    }

    if (msg->message != WM_INPUT) return false;

    // 【重要】添付ファイルに合わせて、バッチ処理(processRawInputBatched)は呼び出さず、
    //        今回届いた単発のメッセージのみを処理します。
    //        これによりメインスレッドのメッセージポンプを阻害せず、スムーズに動作します。
    processRawInput(reinterpret_cast<HRAWINPUT>(msg->lParam));

    return false;
}

FORCE_INLINE void RawInputWinFilter::processRawInput(HRAWINPUT hRaw) noexcept
{
    constexpr UINT kLocalBufSize = 1024;
    alignas(16) uint8_t rawBuf[kLocalBufSize];
    UINT size = kLocalBufSize;
    UINT res;

    // NT APIが使える場合は使用（なければ標準API）
    if (fnNtUserGetRawInputData) {
        res = fnNtUserGetRawInputData(hRaw, RID_INPUT, rawBuf, &size, sizeof(RAWINPUTHEADER));
    }
    else {
        res = GetRawInputData(hRaw, RID_INPUT, rawBuf, &size, sizeof(RAWINPUTHEADER));
    }

    if (res == (UINT)-1 || res == 0) return;

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(rawBuf);

    if (raw->header.dwType == RIM_TYPEMOUSE) {
        const RAWMOUSE& m = raw->data.mouse;
        // 相対移動のみ処理
        if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
            LONG dx = m.lLastX;
            LONG dy = m.lLastY;
            if (dx | dy) {
                // Atomic accumulate using CAS
                uint64_t cur = m_mouseDeltaCombined.load(std::memory_order_relaxed);
                uint64_t nxt;
                do {
                    MouseDeltaPack p;
                    p.combined = cur;
                    p.s.x += dx;
                    p.s.y += dy;
                    nxt = p.combined;
                } while (!m_mouseDeltaCombined.compare_exchange_weak(cur, nxt, std::memory_order_release, std::memory_order_relaxed));
            }
        }
        USHORT f = m.usButtonFlags & 0x03FF;
        if (f) {
            auto lut = s_btnLut[f];
            if (lut.downBits | lut.upBits) {
                uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
                uint8_t nxt = (cur | lut.downBits) & ~lut.upBits;
                if (cur != nxt) m_state.mouseButtons.store(nxt, std::memory_order_relaxed);
            }
        }
        return;
    }

    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& kb = raw->data.keyboard;
        UINT vk = kb.VKey;
        if (vk > 0 && vk < 255) {
            vk = remapVk(vk, kb.MakeCode, kb.Flags);
            bool isDown = !(kb.Flags & RI_KEY_BREAK);
            setVkBit(vk, isDown);
        }
    }
}

// ワーカースレッド用 (joy2KeySupport OFF時のみ使用)
// こちらはバッチ処理で高速に取り込む
void RawInputWinFilter::processRawInputBatched() noexcept
{
    // [MinGW修正] alignasを型指定の前に配置
    alignas(16) static thread_local uint8_t buffer[65536];

    while (true) {
        UINT size = sizeof(buffer);
        UINT count;

        if (fnNtUserGetRawInputBuffer) {
            count = fnNtUserGetRawInputBuffer((PRAWINPUT)buffer, &size, sizeof(RAWINPUTHEADER));
        }
        else {
            count = GetRawInputBuffer((PRAWINPUT)buffer, &size, sizeof(RAWINPUTHEADER));
        }

        if (count == 0 || count == (UINT)-1) break;

        // Coalescing Optimization
        int32_t accX = 0, accY = 0;
        uint8_t btnDown = 0, btnUp = 0;
        struct { uint64_t d; uint64_t u; } localKeys[4] = { 0 };
        bool hasKey = false;

        const RAWINPUT* raw = (const RAWINPUT*)buffer;
        for (UINT i = 0; i < count; ++i) {
            if (raw->header.dwType == RIM_TYPEMOUSE) {
                const RAWMOUSE& m = raw->data.mouse;
                if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                    accX += m.lLastX;
                    accY += m.lLastY;
                }
                USHORT f = m.usButtonFlags & 0x03FF;
                if (f) {
                    btnDown |= s_btnLut[f].downBits;
                    btnUp |= s_btnLut[f].upBits;
                }
            }
            else if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                const RAWKEYBOARD& kb = raw->data.keyboard;
                UINT vk = kb.VKey;
                if (vk > 0 && vk < 255) {
                    vk = remapVk(vk, kb.MakeCode, kb.Flags);
                    int idx = vk >> 6;
                    uint64_t bit = 1ULL << (vk & 63);
                    if (!(kb.Flags & RI_KEY_BREAK)) {
                        localKeys[idx].d |= bit;
                        localKeys[idx].u &= ~bit;
                    }
                    else {
                        localKeys[idx].u |= bit;
                        localKeys[idx].d &= ~bit;
                    }
                    hasKey = true;
                }
            }

            // [MinGW修正] マクロを使わず手動計算してQWORD未定義エラーを回避
            raw = reinterpret_cast<const RAWINPUT*>(
                reinterpret_cast<const uint8_t*>(raw) +
                ((raw->header.dwSize + sizeof(void*) - 1) & ~(sizeof(void*) - 1))
                );
        }

        if (accX | accY) {
            uint64_t cur = m_mouseDeltaCombined.load(std::memory_order_relaxed);
            uint64_t nxt;
            do {
                MouseDeltaPack p; p.combined = cur;
                p.s.x += accX; p.s.y += accY;
                nxt = p.combined;
            } while (!m_mouseDeltaCombined.compare_exchange_weak(cur, nxt, std::memory_order_release, std::memory_order_relaxed));
        }

        if (btnDown | btnUp) {
            uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
            uint8_t nxt = (cur | btnDown) & ~btnUp;
            if (cur != nxt) m_state.mouseButtons.store(nxt, std::memory_order_relaxed);
        }

        if (hasKey) {
            for (int i = 0; i < 4; ++i) {
                if (localKeys[i].d | localKeys[i].u) {
                    uint64_t cur = m_state.vkDown[i].load(std::memory_order_relaxed);
                    uint64_t nxt = (cur | localKeys[i].d) & ~localKeys[i].u;
                    if (cur != nxt) m_state.vkDown[i].store(nxt, std::memory_order_relaxed);
                }
            }
        }
    }
}

// =====================================================
// Worker Thread Logic
// =====================================================

void RawInputWinFilter::startThreadIfNeeded()
{
    if (m_runThread.load()) return;
    m_runThread.store(true);

    m_hThread = (HANDLE)_beginthreadex(nullptr, 0, [](void* p) -> unsigned {
        return RawInputWinFilter::ThreadFunc(p);
        }, this, 0, nullptr);

    if (m_hThread) {
        SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL);

        DWORD_PTR pMask, sMask;
        if (GetProcessAffinityMask(GetCurrentProcess(), &pMask, &sMask)) {
            DWORD_PTR lastCore = 0;
            for (int i = 63; i >= 0; --i) {
                if (pMask & (1ULL << i)) {
                    lastCore = (1ULL << i);
                    break;
                }
            }
            if (lastCore) SetThreadAffinityMask(m_hThread, lastCore);
        }
    }
}

void RawInputWinFilter::stopThreadIfRunning()
{
    if (!m_runThread.load()) return;
    m_runThread.store(false);

    if (m_hiddenWnd) PostMessageA(m_hiddenWnd, WM_NULL, 0, 0);

    if (m_hThread) {
        WaitForSingleObject(m_hThread, 1000);
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }
    m_hiddenWnd = nullptr;
}

DWORD WINAPI RawInputWinFilter::ThreadFunc(LPVOID param)
{
    auto* self = static_cast<RawInputWinFilter*>(param);
    HighResTimerScope timer;
    self->threadLoop();
    return 0;
}

void RawInputWinFilter::threadLoop()
{
    const char* clsName = "MelonPrimeRawWorker_NT";
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = clsName;
    RegisterClassExA(&wc);

    m_hiddenWnd = CreateWindowExA(0, clsName, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!m_hiddenWnd) return;

    registerRawToTarget(m_hiddenWnd);

    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {}

    while (m_runThread.load(std::memory_order_relaxed)) {

        // ワーカースレッドではバッチ処理を積極的に使用
        processRawInputBatched();

        if (fnNtUserPeekMessage) {
            while (fnNtUserPeekMessage(&msg, nullptr, 0, 0, PM_REMOVE, FALSE)) {
                if (msg.message == WM_QUIT) goto cleanup;
                if (msg.message == WM_INPUT) {
                    processRawInput((HRAWINPUT)msg.lParam);
                }
                else {
                    TranslateMessage(&msg);
                    DispatchMessageA(&msg);
                }
            }
        }
        else {
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) goto cleanup;
                if (msg.message == WM_INPUT) {
                    processRawInput((HRAWINPUT)msg.lParam);
                }
                else {
                    TranslateMessage(&msg);
                    DispatchMessageA(&msg);
                }
            }
        }

        // Wait efficiently
        MsgWaitForMultipleObjects(0, nullptr, FALSE, INFINITE, QS_RAWINPUT | QS_POSTMESSAGE | QS_INPUT);
    }

cleanup:
    DestroyWindow(m_hiddenWnd);
    UnregisterClassA(clsName, GetModuleHandleA(nullptr));
    m_hiddenWnd = nullptr;
}

// =====================================================
// Accessors / Helpers
// =====================================================

void RawInputWinFilter::setHotkeyVks(int id, const std::vector<UINT>& vks)
{
    if (id < 0 || (size_t)id >= kMaxHotkeyId) return;
    HotkeyMask& m = m_hkMask[id];
    memset(&m, 0, sizeof(HotkeyMask));

    if (vks.empty()) return;

    for (UINT k : vks) {
        if (k >= VK_LBUTTON && k <= VK_XBUTTON2) {
            int bit = -1;
            if (k == VK_LBUTTON) bit = 0;
            else if (k == VK_RBUTTON) bit = 1;
            else if (k == VK_MBUTTON) bit = 2;
            else if (k == VK_XBUTTON1) bit = 3;
            else if (k == VK_XBUTTON2) bit = 4;

            if (bit != -1) m.mouseMask |= (1 << bit);
        }
        else if (k < 256) {
            m.vkMask[k >> 6] |= (1ULL << (k & 63));
        }
    }
    m.hasMask = true;
}

bool RawInputWinFilter::hotkeyDown(int id) const noexcept {
    if ((unsigned)id >= kMaxHotkeyId) return false;
    const auto& m = m_hkMask[id];
    if (!m.hasMask) return false;

    if (m.mouseMask && (m_state.mouseButtons.load(std::memory_order_relaxed) & m.mouseMask))
        return true;

    for (int i = 0; i < 4; ++i) {
        if (m.vkMask[i] & m_state.vkDown[i].load(std::memory_order_relaxed)) return true;
    }
    return false;
}

bool RawInputWinFilter::hotkeyPressed(int id) noexcept {
    if ((unsigned)id >= kMaxHotkeyId) return false;
    bool cur = hotkeyDown(id);

    int word = id >> 6;
    uint64_t bit = 1ULL << (id & 63);

    bool prev = (m_hkPrev[word] & bit) != 0;
    if (cur != prev) {
        if (cur) m_hkPrev[word] |= bit;
        else     m_hkPrev[word] &= ~bit;
    }
    return (cur && !prev);
}

bool RawInputWinFilter::hotkeyReleased(int id) noexcept {
    if ((unsigned)id >= kMaxHotkeyId) return false;
    bool cur = hotkeyDown(id);
    int word = id >> 6;
    uint64_t bit = 1ULL << (id & 63);
    bool prev = (m_hkPrev[word] & bit) != 0;

    if (cur != prev) {
        if (cur) m_hkPrev[word] |= bit;
        else     m_hkPrev[word] &= ~bit;
    }
    return (!cur && prev);
}

void RawInputWinFilter::fetchMouseDelta(int& outX, int& outY) {
    uint64_t val = m_mouseDeltaCombined.exchange(0, std::memory_order_acquire);
    MouseDeltaPack p; p.combined = val;
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
        int word = i >> 6;
        uint64_t bit = 1ULL << (i & 63);
        if (down) m_hkPrev[word] |= bit;
        else      m_hkPrev[word] &= ~bit;
    }
}

void RawInputWinFilter::clearAllBindings() {
    memset(m_hkMask.data(), 0, sizeof(m_hkMask));
    memset(m_hkPrev, 0, sizeof(m_hkPrev));
}

#endif // _WIN32