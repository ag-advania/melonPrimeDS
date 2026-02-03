#ifdef _WIN32
#include "MelonPrimeRawWorker.h"
#include "MelonPrimeInputState.h"
#include "MelonPrimeWinInternal.h"
#include <process.h>
#include <emmintrin.h> // for _mm_pause

// Static members
ATOM RawWorker::s_windowClass = 0;
std::atomic<LONG> RawWorker::s_classRefCount{ 0 };

namespace {
    constexpr const wchar_t* kWindowClassName = L"MelonPrimeRawWorker_HID";
}

RawWorker::RawWorker(InputState& state) noexcept
    : m_state(state)
{
}

RawWorker::~RawWorker() {
    stop();
}

void RawWorker::start() {
    bool expected = false;
    if (!m_runThread.compare_exchange_strong(expected, true,
        std::memory_order_acq_rel, std::memory_order_relaxed)) {
        return;
    }

    m_hThread = reinterpret_cast<HANDLE>(
        _beginthreadex(nullptr, 0, ThreadEntry, this, 0, nullptr));

    if (m_hThread) {
        // Elevate thread priority for input responsiveness
        // CPU Affinity code removed to reduce overhead
        SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL);
    }
    else {
        m_runThread.store(false, std::memory_order_release);
    }
}

void RawWorker::stop() noexcept {
    bool expected = true;
    if (!m_runThread.compare_exchange_strong(expected, false,
        std::memory_order_acq_rel, std::memory_order_relaxed)) {
        return;
    }

    // Wake up the blocked message loop
    if (m_hiddenWnd) {
        PostMessageW(m_hiddenWnd, WM_NULL, 0, 0);
    }

    if (m_hThread) {
        const DWORD waitResult = WaitForSingleObject(m_hThread, 2000);
#ifdef _DEBUG
        if (waitResult == WAIT_TIMEOUT) {
            OutputDebugStringW(L"RawWorker: Thread exit timeout\n");
        }
#else
        (void)waitResult;
#endif
        CloseHandle(m_hThread);
        m_hThread = nullptr;
    }
}

unsigned __stdcall RawWorker::ThreadEntry(void* param) noexcept {
    // Timer scope removed for performance/simplicity
    static_cast<RawWorker*>(param)->threadLoop();
    return 0;
}

void RawWorker::threadLoop() noexcept {
    const HINSTANCE hInstance = GetModuleHandleW(nullptr);

    // Register window class (thread-safe reference counting)
    if (s_classRefCount.fetch_add(1, std::memory_order_acq_rel) == 0) {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = hInstance;
        wc.lpszClassName = kWindowClassName;
        s_windowClass = RegisterClassExW(&wc);
    }

    // Wait for class registration if another thread is doing it
    while (!s_windowClass && s_classRefCount.load(std::memory_order_acquire) > 0) {
        YieldProcessor();
    }

    if (!s_windowClass) {
        s_classRefCount.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }

    // Create message-only window (no visible window, minimal overhead)
    m_hiddenWnd = CreateWindowExW(
        0, kWindowClassName, nullptr, 0,
        0, 0, 0, 0,
        HWND_MESSAGE, nullptr, hInstance, nullptr);

    if (!m_hiddenWnd) {
        if (s_classRefCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            UnregisterClassW(kWindowClassName, hInstance);
            s_windowClass = 0;
        }
        return;
    }

    RegisterRawDevices(m_hiddenWnd);

    // Drain stale messages
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_INPUT) {
            m_state.processRawInput(reinterpret_cast<HRAWINPUT>(msg.lParam));
        }
    }

    // Cache function pointer for hot path
    const auto ntPeekMessage = WinInternal::fnNtUserPeekMessage;
    // 追加: ntMsgWait をキャッシュ
    const auto ntMsgWait = WinInternal::fnNtUserMsgWaitForMultipleObjectsEx;

    constexpr DWORD kWakeFlags = QS_RAWINPUT | QS_POSTMESSAGE;

    // Number of spin iterations before sleeping
    // Kept 4000 as it's a good balance for low-latency vs CPU usage
    constexpr int kSpinCount = 4000;

    // Main loop
    while (m_runThread.load(std::memory_order_relaxed)) {
        // Batch processing is more efficient than per-message
        m_state.processRawInputBatched();

        // Process any remaining messages
        if (ntPeekMessage) {
            while (ntPeekMessage(&msg, nullptr, 0, 0, PM_REMOVE, FALSE)) {
                if (msg.message == WM_QUIT) goto cleanup;
                if (msg.message == WM_INPUT) {
                    m_state.processRawInput(reinterpret_cast<HRAWINPUT>(msg.lParam));
                }
            }
        }
        else {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) goto cleanup;
                if (msg.message == WM_INPUT) {
                    m_state.processRawInput(reinterpret_cast<HRAWINPUT>(msg.lParam));
                }
            }
        }

        // Hybrid Spinning:
        // Before sleeping, spin briefly to catch packets arriving immediately.
        for (int i = 0; i < kSpinCount; ++i) {
            _mm_pause(); // Hint to CPU that we are spinning (saves power/pipeline)

            // Non-removing check to see if we should break spin
            if (ntPeekMessage) {
                if (ntPeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE, FALSE)) break;
            }
            else {
                if (PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE)) break;
            }
        }

        // Block until new input arrives (power efficient) if spin didn't catch anything
        // 修正: NT APIがあればそちらを使用
        if (ntMsgWait) {
            ntMsgWait(0, nullptr, INFINITE, kWakeFlags, MWMO_INPUTAVAILABLE);
        }
        else {
            MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, kWakeFlags, MWMO_INPUTAVAILABLE);
        }
    }

cleanup:
    UnregisterRawDevices();
    DestroyWindow(m_hiddenWnd);
    m_hiddenWnd = nullptr;

    if (s_classRefCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        UnregisterClassW(kWindowClassName, hInstance);
        s_windowClass = 0;
    }
}

void RawWorker::RegisterRawDevices(HWND hwnd) noexcept {
    if (!hwnd) return;

    const RAWINPUTDEVICE rid[2] = {
        { 0x01, 0x02, RIDEV_INPUTSINK, hwnd },  // Mouse
        { 0x01, 0x06, RIDEV_INPUTSINK, hwnd }   // Keyboard
    };
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

void RawWorker::UnregisterRawDevices() noexcept {
    const RAWINPUTDEVICE rid[2] = {
        { 0x01, 0x02, RIDEV_REMOVE, nullptr },
        { 0x01, 0x06, RIDEV_REMOVE, nullptr }
    };
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
}

#endif // _WIN32