#ifdef _WIN32
#include "MelonPrimeRawWorker.h"
#include "MelonPrimeInputState.h"
#include "MelonPrimeWinInternal.h"
#include <process.h>
#include <emmintrin.h>

namespace MelonPrime {

    ATOM RawWorker::s_windowClass = 0;
    std::atomic<LONG> RawWorker::s_classRefCount{ 0 };

    namespace {
        constexpr const wchar_t* kWindowClassName = L"MelonPrimeRawWorker_HID";
    }

    RawWorker::RawWorker(InputState& state) noexcept : m_state(state) {}

    RawWorker::~RawWorker() {
        stop();
    }

    void RawWorker::start() {
        bool expected = false;
        if (!m_runThread.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return;
        }

        m_hThread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, ThreadEntry, this, 0, nullptr));

        if (m_hThread) {
            SetThreadPriority(m_hThread, THREAD_PRIORITY_TIME_CRITICAL);
        }
        else {
            m_runThread.store(false, std::memory_order_release);
        }
    }

    void RawWorker::stop() noexcept {
        bool expected = true;
        if (!m_runThread.compare_exchange_strong(expected, false, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return;
        }

        if (m_hiddenWnd) PostMessageW(m_hiddenWnd, WM_NULL, 0, 0);

        if (m_hThread) {
            WaitForSingleObject(m_hThread, 2000);
            CloseHandle(m_hThread);
            m_hThread = nullptr;
        }
    }

    unsigned __stdcall RawWorker::ThreadEntry(void* param) noexcept {
        static_cast<RawWorker*>(param)->threadLoop();
        return 0;
    }

    void RawWorker::threadLoop() noexcept {
        const HINSTANCE hInstance = GetModuleHandleW(nullptr);

        if (s_classRefCount.fetch_add(1, std::memory_order_acq_rel) == 0) {
            WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = hInstance;
            wc.lpszClassName = kWindowClassName;
            s_windowClass = RegisterClassExW(&wc);
        }

        while (!s_windowClass && s_classRefCount.load(std::memory_order_acquire) > 0) {
            YieldProcessor();
        }

        if (!s_windowClass) {
            s_classRefCount.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }

        m_hiddenWnd = CreateWindowExW(0, kWindowClassName, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
        if (!m_hiddenWnd) {
            if (s_classRefCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                UnregisterClassW(kWindowClassName, hInstance);
                s_windowClass = 0;
            }
            return;
        }

        RegisterRawDevices(m_hiddenWnd);

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_INPUT) m_state.processRawInput(reinterpret_cast<HRAWINPUT>(msg.lParam));
        }

        const auto ntPeekMessage = WinInternal::fnNtUserPeekMessage;
        const auto ntMsgWait = WinInternal::fnNtUserMsgWaitForMultipleObjectsEx;
        constexpr DWORD kWakeFlags = QS_RAWINPUT | QS_POSTMESSAGE;
        constexpr int kSpinCount = 4000;

        while (m_runThread.load(std::memory_order_relaxed)) {
            m_state.processRawInputBatched();

            if (ntPeekMessage) {
                while (ntPeekMessage(&msg, nullptr, 0, 0, PM_REMOVE, FALSE)) {
                    if (msg.message == WM_QUIT) goto cleanup;
                    if (msg.message == WM_INPUT) m_state.processRawInput(reinterpret_cast<HRAWINPUT>(msg.lParam));
                }
            }
            else {
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) goto cleanup;
                    if (msg.message == WM_INPUT) m_state.processRawInput(reinterpret_cast<HRAWINPUT>(msg.lParam));
                }
            }

            for (int i = 0; i < kSpinCount; ++i) {
                _mm_pause();
                if (ntPeekMessage) {
                    if (ntPeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE, FALSE)) break;
                }
                else {
                    if (PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE)) break;
                }
            }

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
            { 0x01, 0x02, RIDEV_INPUTSINK, hwnd },
            { 0x01, 0x06, RIDEV_INPUTSINK, hwnd }
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

} // namespace MelonPrime
#endif // _WIN32