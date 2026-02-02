// MelonPrimeRawInputWinFilter.cpp
#ifdef _WIN32

#include "MelonPrimeRawInputWinFilter.h"
#include <cassert>
#include <cstdio>
#include <process.h>
#include <mmsystem.h>
#include <vector>
#include <algorithm>

#pragma comment(lib, "winmm.lib")

// =====================================================
// NT API Definitions & Loaders (Undocumented / Low Level)
// =====================================================
typedef LONG(NTAPI* PFN_NtQueryTimerResolution)(PULONG MaximumTime, PULONG MinimumTime, PULONG CurrentTime);
typedef LONG(NTAPI* PFN_NtSetTimerResolution)(ULONG DesiredTime, BOOLEAN SetResolution, PULONG ActualTime);

// ユーザー提供のNtUser API定義
typedef UINT(WINAPI* NtUserGetRawInputData_t)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
typedef UINT(WINAPI* NtUserGetRawInputBuffer_t)(PRAWINPUT, PUINT, UINT);
typedef BOOL(WINAPI* NtUserPeekMessage_t)(LPMSG, HWND, UINT, UINT, UINT, BOOL);

static NtUserGetRawInputData_t   pNtGetRawInputData = nullptr;
static NtUserGetRawInputBuffer_t pNtGetRawInputBuffer = nullptr;
static NtUserPeekMessage_t       pNtPeekMessage = nullptr;

static void LoadNtUserAPIs()
{
    // 一度だけロード
    static std::once_flag flag;
    std::call_once(flag, []() {
        // win32u.dll は Windows 10/11 以降で syscall スタブを提供する
        HMODULE h = LoadLibraryA("win32u.dll");
        if (!h) return;

        pNtGetRawInputData = (NtUserGetRawInputData_t)GetProcAddress(h, "NtUserGetRawInputData");
        pNtGetRawInputBuffer = (NtUserGetRawInputBuffer_t)GetProcAddress(h, "NtUserGetRawInputBuffer");
        pNtPeekMessage = (NtUserPeekMessage_t)GetProcAddress(h, "NtUserPeekMessage");

        // デバッグ出力: ロード成功確認
        // if (pNtGetRawInputData) OutputDebugStringA("[MelonPrime] NtUserGetRawInputData loaded.\n");
        });
}

// =====================================================
// High Resolution Timer Scope (0.5ms Target)
// =====================================================
class HighResTimerScope {
public:
    HighResTimerScope() {
        HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
        if (hNtDll) {
            auto NtQueryTimerResolution = (PFN_NtQueryTimerResolution)GetProcAddress(hNtDll, "NtQueryTimerResolution");
            auto NtSetTimerResolution = (PFN_NtSetTimerResolution)GetProcAddress(hNtDll, "NtSetTimerResolution");

            if (NtQueryTimerResolution && NtSetTimerResolution) {
                ULONG minRes, maxRes, curRes;
                if (NtQueryTimerResolution(&maxRes, &minRes, &curRes) == 0) {
                    m_targetRes = maxRes; // 通常 5000 (0.5ms)
                    if (NtSetTimerResolution(m_targetRes, TRUE, &m_currentRes) == 0) {
                        m_usingNtApi = true;
                        return;
                    }
                }
            }
        }
        timeBeginPeriod(1);
        m_usingNtApi = false;
    }

    ~HighResTimerScope() {
        if (m_usingNtApi) {
            HMODULE hNtDll = GetModuleHandleA("ntdll.dll");
            if (hNtDll) {
                auto NtSetTimerResolution = (PFN_NtSetTimerResolution)GetProcAddress(hNtDll, "NtSetTimerResolution");
                if (NtSetTimerResolution) {
                    ULONG temp;
                    NtSetTimerResolution(m_targetRes, FALSE, &temp);
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
// Static Members
// =====================================================

RawInputWinFilter* RawInputWinFilter::s_instance = nullptr;
int RawInputWinFilter::s_refCount = 0;
std::mutex RawInputWinFilter::s_mutex;
std::once_flag RawInputWinFilter::s_initFlag;

RawInputWinFilter::BtnLutEntry RawInputWinFilter::s_btnLut[1024];
RawInputWinFilter::VkRemapEntry RawInputWinFilter::s_vkRemap[256];

void RawInputWinFilter::initializeTables() {
    LoadNtUserAPIs(); // APIロード

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
    memset(s_vkRemap, 0, sizeof(s_vkRemap));
    s_vkRemap[VK_CONTROL] = { VK_LCONTROL, VK_RCONTROL };
    s_vkRemap[VK_MENU] = { VK_LMENU, VK_RMENU };
}

// =====================================================
// Singleton Management
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

// =====================================================
// Constructor / Destructor
// =====================================================
RawInputWinFilter::RawInputWinFilter(bool joy2KeySupport, HWND mainHwnd)
    : m_mainHwnd(mainHwnd)
{
    std::call_once(s_initFlag, &RawInputWinFilter::initializeTables);

    for (auto& a : m_state.vkDown) a.store(0);
    m_state.mouseButtons.store(0);
    m_mouseDeltaCombined.store(0);

    memset(m_hkMask.data(), 0, sizeof(m_hkMask));
    memset(m_hkPrev, 0, sizeof(m_hkPrev));

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
// Methods
// =====================================================

void RawInputWinFilter::setJoy2KeySupport(bool enable)
{
    const bool prev = m_joy2KeySupport.load(std::memory_order_relaxed);
    if (prev == enable) return;

    m_joy2KeySupport.store(enable, std::memory_order_relaxed);

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
        if (m_joy2KeySupport.load(std::memory_order_relaxed)) {
            if (m_mainHwnd) registerRawToTarget(m_mainHwnd);
        }
    }
}

void RawInputWinFilter::registerRawToTarget(HWND targetHwnd)
{
    if (!targetHwnd) return;

    RAWINPUTDEVICE rid[2];
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x02;
    rid[0].dwFlags = RIDEV_INPUTSINK;
    rid[0].hwndTarget = targetHwnd;

    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x06;
    rid[1].dwFlags = RIDEV_INPUTSINK;
    rid[1].hwndTarget = targetHwnd;

    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

bool RawInputWinFilter::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
    if (!m_joy2KeySupport.load(std::memory_order_relaxed)) return false;

    MSG* msg = static_cast<MSG*>(message);
    if (!msg || msg->message != WM_INPUT) return false;

    processRawInput(reinterpret_cast<HRAWINPUT>(msg->lParam));
    return false;
}

FORCE_INLINE void RawInputWinFilter::processRawInput(HRAWINPUT hRaw) noexcept
{
    constexpr UINT kBufSize = 1024;
    alignas(16) uint8_t rawBuf[kBufSize];
    UINT size = kBufSize;
    UINT result;

    // OPTIMIZED: Use NtUserGetRawInputData (Syscall) if available to bypass User32 overhead
    if (pNtGetRawInputData) {
        result = pNtGetRawInputData(hRaw, RID_INPUT, rawBuf, &size, sizeof(RAWINPUTHEADER));
    }
    else {
        result = GetRawInputData(hRaw, RID_INPUT, rawBuf, &size, sizeof(RAWINPUTHEADER));
    }

    if (result == (UINT)-1) {
        return;
    }

    RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(rawBuf);
    const DWORD type = raw->header.dwType;

    if (type == RIM_TYPEMOUSE) {
        const RAWMOUSE& m = raw->data.mouse;
        if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
            const LONG dx_ = m.lLastX;
            const LONG dy_ = m.lLastY;
            if (dx_ | dy_) {
                uint64_t cur = m_mouseDeltaCombined.load(std::memory_order_relaxed);
                uint64_t nxt;
                do {
                    MouseDeltaPack p;
                    p.combined = cur;
                    p.s.x += dx_;
                    p.s.y += dy_;
                    nxt = p.combined;
                } while (!m_mouseDeltaCombined.compare_exchange_weak(cur, nxt, std::memory_order_release, std::memory_order_relaxed));
            }
        }
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

    if (type == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& kb = raw->data.keyboard;
        UINT vk = kb.VKey;
        const USHORT flags = kb.Flags;

        if (vk == 0 || vk >= 255) return;
        vk = remapVk(vk, kb.MakeCode, flags);
        bool isDown = !(flags & RI_KEY_BREAK);
        setVkBit(vk, isDown);
        return;
    }
}

// =====================================================
// Batch Processing
// =====================================================
void RawInputWinFilter::processRawInputBatched() noexcept
{
    constexpr UINT kBufferSize = 16384;
    alignas(16) uint8_t buffer[kBufferSize];

    while (true) {
        UINT size = kBufferSize;
        UINT count;

        // OPTIMIZED: Use NtUserGetRawInputBuffer if available
        if (pNtGetRawInputBuffer) {
            count = pNtGetRawInputBuffer(reinterpret_cast<RAWINPUT*>(buffer), &size, sizeof(RAWINPUTHEADER));
        }
        else {
            count = GetRawInputBuffer(reinterpret_cast<RAWINPUT*>(buffer), &size, sizeof(RAWINPUTHEADER));
        }

        if (count == 0 || count == (UINT)-1) {
            break;
        }

        int32_t accumX = 0, accumY = 0;
        uint8_t btnDown = 0, btnUp = 0;

        uint64_t localVkDown[4] = { 0 };
        uint64_t localVkUp[4] = { 0 };
        bool hasKeyChanges = false;

        const RAWINPUT* raw = reinterpret_cast<const RAWINPUT*>(buffer);
        for (UINT i = 0; i < count; ++i) {
            const DWORD type = raw->header.dwType;

            if (type == RIM_TYPEMOUSE) {
                const RAWMOUSE& m = raw->data.mouse;
                if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                    accumX += m.lLastX;
                    accumY += m.lLastY;
                }
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
                if (vk > 0 && vk < 255) {
                    vk = remapVk(vk, kb.MakeCode, kb.Flags);
                    const uint32_t widx = vk >> 6;
                    const uint64_t bit = 1ULL << (vk & 63);

                    if (!(kb.Flags & RI_KEY_BREAK)) {
                        localVkDown[widx] |= bit;
                        localVkUp[widx] &= ~bit;
                    }
                    else {
                        localVkUp[widx] |= bit;
                        localVkDown[widx] &= ~bit;
                    }
                    hasKeyChanges = true;
                }
            }
            raw = reinterpret_cast<const RAWINPUT*>(
                reinterpret_cast<const uint8_t*>(raw) +
                ((raw->header.dwSize + sizeof(void*) - 1) & ~(sizeof(void*) - 1))
                );
        }

        if (accumX | accumY) {
            uint64_t cur = m_mouseDeltaCombined.load(std::memory_order_relaxed);
            uint64_t nxt;
            do {
                union { struct { int32_t x, y; } s; uint64_t c; } p;
                p.c = cur;
                p.s.x += accumX;
                p.s.y += accumY;
                nxt = p.c;
            } while (!m_mouseDeltaCombined.compare_exchange_weak(cur, nxt, std::memory_order_release, std::memory_order_relaxed));
        }

        if (btnDown | btnUp) {
            uint8_t cur = m_state.mouseButtons.load(std::memory_order_relaxed);
            uint8_t nxt = (cur | btnDown) & ~btnUp;
            if (cur != nxt) m_state.mouseButtons.store(nxt, std::memory_order_relaxed);
        }

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
}

void RawInputWinFilter::startThreadIfNeeded()
{
    if (m_runThread.load(std::memory_order_relaxed)) return;

    m_runThread.store(true, std::memory_order_relaxed);
    m_hThread = CreateThread(nullptr, 0, ThreadFunc, this, 0, nullptr);

    if (m_hThread) {
        SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL);

        // CPU Affinity (Last Core Optimization)
        DWORD_PTR processAffinity, systemAffinity;
        if (GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity)) {
            DWORD_PTR mask = 1;
            DWORD_PTR lastCore = 0;
            for (int i = 0; i < 64; i++) {
                if (processAffinity & mask) lastCore = mask;
                mask <<= 1;
            }
            if (lastCore != 0) {
                SetThreadAffinityMask(m_hThread, lastCore);
            }
        }
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
    // High Precision Timer Scope
    HighResTimerScope timerScope;
    static_cast<RawInputWinFilter*>(param)->threadLoop();
    return 0;
}

void RawInputWinFilter::threadLoop()
{
    const TCHAR* clsName = TEXT("MelonPrimeRawWorker_NtHybrid");

    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = clsName;

    RegisterClassEx(&wc);

    m_hiddenWnd = CreateWindowEx(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

    if (!m_hiddenWnd) return;

    registerRawToTarget(m_hiddenWnd);

    MSG msg;
    // Initial flush using optimal available API
    if (pNtPeekMessage) {
        while (pNtPeekMessage(&msg, nullptr, 0, 0, PM_REMOVE, FALSE)) {}
    }
    else {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {}
    }

    while (m_runThread.load(std::memory_order_relaxed))
    {
        // 1. Batch process (Buffer) - Clears queue efficiently
        processRawInputBatched();

        // 2. Peek for stragglers or non-buffered inputs
        // OPTIMIZED: Use NtUserPeekMessage if available
        bool hasMsg = false;
        if (pNtPeekMessage) {
            // Note: 6th arg 'BOOL' typically controls yielding/internal state. FALSE is safe default.
            while (pNtPeekMessage(&msg, m_hiddenWnd, 0, 0, PM_REMOVE, FALSE)) {
                if (msg.message == WM_QUIT || msg.message == WM_CLOSE) {
                    goto cleanup;
                }

                if (msg.message == WM_INPUT) {
                    processRawInput(reinterpret_cast<HRAWINPUT>(msg.lParam));
                }
                else {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
        }
        else {
            // Fallback to standard User32 API
            while (PeekMessage(&msg, m_hiddenWnd, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT || msg.message == WM_CLOSE) {
                    goto cleanup;
                }

                if (msg.message == WM_INPUT) {
                    processRawInput(reinterpret_cast<HRAWINPUT>(msg.lParam));
                }
                else {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
        }

        // 3. Wait (0.5ms resolution capable)
        MsgWaitForMultipleObjects(0, nullptr, FALSE, INFINITE, QS_RAWINPUT | QS_POSTMESSAGE);
    }

cleanup:
    DestroyWindow(m_hiddenWnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    m_hiddenWnd = nullptr;
}

// =====================================================
// (Existing logic unchanged)
// =====================================================

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

bool RawInputWinFilter::hotkeyDown(int hk) const noexcept {
    if ((unsigned)hk >= kMaxHotkeyId) return false;
    const HotkeyMask& m = m_hkMask[(unsigned)hk];
    if (!m.hasMask) return false;

    if (m.mouseMask && (m_state.mouseButtons.load(std::memory_order_relaxed) & m.mouseMask))
        return true;

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
    uint64_t val = m_mouseDeltaCombined.exchange(0, std::memory_order_acquire);
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